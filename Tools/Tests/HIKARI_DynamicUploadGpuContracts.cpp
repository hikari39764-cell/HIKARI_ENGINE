#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_UploadSubmissionQueue.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteSnapshot.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"

#include <array>
#include <filesystem>
#include <DirectXTex.h>
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"
#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include "Internal/HIKARI_GpuContractDevice.h"
#include "Internal/HIKARI_ContractAssert.h"

namespace {
void VerifyScenePublication() {
    using namespace HIKARI;
    using namespace HIKARI::RENDER3D::RUNTIME;
    using namespace HIKARI::RENDER3D::GPUDRIVEN;
    ModelAsset model;
    model.SetName("publication");
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    PROCEDURAL::BuildProceduralMesh({}, model.meshes[0].primitives[0]);
    model.nodes.resize(1);
    model.nodes[0].meshIndex = 0;
    model.materials.resize(1);
    ComponentRegistry components;
    ComponentTypeInfo modelType;
    modelType.typeName = "ModelComponent";
    modelType.factory = [] { return std::make_unique<ModelComponent>(); };
    modelType.affectsRenderState = true;
    assert(components.Register(std::move(modelType)));
    World world;
    assert(world.BindComponentRegistry(&components));
    auto* object = world.CreateObject("model");
    object->AddComponent<ModelComponent>()->SetModelAsset(&model);
    RenderModelCache models;
    SceneRenderCache cache;
    SceneRenderCacheSync sync;
    sync.Sync(world, models, cache, 1);
    const SceneRenderObjectId id{object->GetRenderStableId()};
    assert(cache.Find(id) && cache.GetSurfaceInstances().size() == 1);
    auto transform = object->GetTransform();
    transform.position.x = 3;
    assert(object->SetLocalTransform(transform));
    sync.SyncDirty(world, models, cache, 2);
    assert(cache.Find(id)->desc.worldTransform.position.x == 3);
    assert(world.GetRenderDirtyObjects().empty());
    world.DestroyObject(object);
    sync.SyncDirty(world, models, cache, 3);
    assert(!cache.Find(id) && cache.GetSurfaceInstances().empty());

    // CPU publication fixture: a cluster identity is enough to check routing;
    // it does not claim to render or upload a cooked mesh.
    SceneRenderObjectDesc desc;
    desc.id = {1};
    desc.model = &model;
    desc.renderModel = models.GetOrCreate(model);
    desc.clusteredGeometryPath = "publication.hcmesh";
    desc.localBounds = desc.renderModel->localBounds;
    desc.worldTransform.scale = {0.1f, 0.1f, 0.1f};
    desc.worldBounds.min = {-0.05f, -0.05f, -0.05f};
    desc.worldBounds.max = {0.05f, 0.05f, 0.05f};
    cache.BeginSync(4);
    cache.Upsert(desc);
    cache.EndSync();
    cache.PreRenderSync();
    GpuSceneRegistry registry;
    registry.SyncForwardFromSceneCache({&cache});
    const auto& before = registry.GetSceneSource().GetPass(GpuDrivenPassKind::DepthPrepass);
    const auto depthFlag = static_cast<uint32_t>(SurfaceGpuSceneInstanceFlags::PassDepthPrepass);
    assert(before.instances && before.instances->size() == 1);
    assert((before.instances->front().flags & depthFlag) != 0);
    desc.worldTransform.position.x = 1;
    desc.worldBounds.min.x += 1;
    desc.worldBounds.max.x += 1;
    cache.BeginPatchSync(5);
    cache.Upsert(desc);
    cache.EndPatchSync();
    cache.PreRenderSync();
    registry.SyncForwardFromSceneCache({&cache});
    assert(registry.GetStats().syncMode == GpuSceneSyncMode::Patch);
    const auto& after = registry.GetSceneSource().GetPass(GpuDrivenPassKind::DepthPrepass);
    assert((after.instances->front().flags & depthFlag) != 0 && !after.dirtyRanges.empty());
    assert(registry.GetSceneSource().meshShaderJointPalettes != nullptr);
    assert(registry.GetSceneSource().meshShaderJointPalettes->matrices.empty());
    assert(after.instances->front().jointPaletteOffsetBytes == 0u &&
           after.instances->front().jointPaletteMatrixCount == 0u);
    std::puts("Scene publication: full/dirty/removal and small depth fallback preservation PASS");
}

void VerifyCompactJointPalettePublication() {
    using namespace HIKARI;
    using namespace RENDER3D::RUNTIME;
    using namespace RENDER3D::GPUDRIVEN;
    constexpr uint32_t kSurfaceCount = 2049u;
    constexpr uint32_t kJointCount = 2u;
    ModelAsset model;
    model.SetName("compact-joint-publication");
    model.meshes.resize(2u);
    model.meshes[0].primitives.resize(1u);
    model.meshes[1].primitives.resize(1u);
    PROCEDURAL::BuildProceduralMesh({}, model.meshes[0].primitives[0]);
    PROCEDURAL::BuildProceduralMesh({}, model.meshes[1].primitives[0]);
    MeshPrimitive& skinnedPrimitive = model.meshes[1].primitives[0];
    skinnedPrimitive.layout = VertexLayoutKind::SkinnedPNTTJW;
    skinnedPrimitive.skinnedVertices.reserve(skinnedPrimitive.staticVertices.size());
    for (const Vertex3D& vertex : skinnedPrimitive.staticVertices) {
        SkinnedVertex3D skinned{};
        skinned.position = vertex.position;
        skinned.normal = vertex.normal;
        skinned.tangent = vertex.tangent;
        skinned.uv0 = vertex.uv0;
        skinned.uv1 = vertex.uv1;
        skinned.color0 = vertex.color0;
        skinned.weights[0] = 1.0f;
        skinnedPrimitive.skinnedVertices.push_back(skinned);
    }
    model.skins.resize(1u);
    model.skins[0].joints.resize(kJointCount);
    model.skins[0].joints[0].nodeIndex = 1;
    model.skins[0].joints[1].nodeIndex = 2;
    model.skins[0].joints[0].inverseBindMatrix = MATH::Mat4::Identity();
    model.skins[0].joints[1].inverseBindMatrix = MATH::Mat4::Identity();
    model.nodes.resize(kSurfaceCount);
    model.nodes[0].meshIndex = 0;
    for (uint32_t nodeIndex = 1u; nodeIndex < kSurfaceCount; ++nodeIndex) {
        model.nodes[nodeIndex].meshIndex = 1;
        model.nodes[nodeIndex].skinIndex = 0;
    }

    RenderModelCache models;
    const RenderModelAsset* renderModel = models.GetOrCreate(model);
    assert(renderModel != nullptr && renderModel->valid && renderModel->surfaces.size() == kSurfaceCount);
    SceneRenderCache cache;
    SceneRenderObjectDesc desc;
    desc.id = {42u};
    desc.model = &model;
    desc.renderModel = renderModel;
    desc.localBounds = renderModel->localBounds;
    desc.worldBounds.min = {-1.0f, -1.0f, -1.0f};
    desc.worldBounds.max = {1.0f, 1.0f, 1.0f};
    desc.clusteredGeometryPath = "compact-joint-publication.hcmesh";
    cache.BeginSync(1u);
    cache.Upsert(desc);
    cache.EndSync();
    cache.PreRenderSync();
    assert(cache.GetSurfaceInstances().size() == kSurfaceCount);

    GpuSceneRegistry registry;
    registry.SyncForwardFromSceneCache({&cache});
    const auto& source = registry.GetSceneSource();
    assert(source.meshShaderJointPalettes != nullptr);
    assert(source.meshShaderJointPalettes->matrices.size() == size_t(kSurfaceCount - 1u) * kJointCount);
    const auto& pass = source.GetPass(GpuDrivenPassKind::ForwardOpaque);
    assert(pass.instances != nullptr);
    bool foundLastSkinnedSurface = false;
    bool foundStaticSurface = false;
    for (const SurfaceGpuSceneInstance& instance : *pass.instances) {
        if (instance.sourceSurfaceInstanceIndex == 0u) {
            foundStaticSurface = true;
            assert(instance.jointPaletteOffsetBytes == 0u && instance.jointPaletteMatrixCount == 0u);
        } else if (instance.sourceSurfaceInstanceIndex == kSurfaceCount - 1u) {
            foundLastSkinnedSurface = true;
            assert(instance.jointPaletteOffsetBytes == (kSurfaceCount - 2u) * kJointCount * sizeof(MATH::Mat4));
            assert(instance.jointPaletteMatrixCount == kJointCount);
        }
    }
    assert(foundStaticSurface && foundLastSkinnedSurface);
    std::puts("Compact joint publication: static empty, mixed offsets, and surface index 2048 routing PASS");
}

void VerifyJointPaletteUpload(HIKARI::TESTS::INTERNAL::GpuContractDevice& gpu) {
    using Microsoft::WRL::ComPtr;
    using namespace HIKARI;
    using namespace HIKARI::RENDER3D::GPUDRIVEN;
    constexpr UINT kReadCount = 4u;
    const char* shader = R"(
        ByteAddressBuffer scene : register(t0);
        RWStructuredBuffer<uint4> output : register(u0);
        cbuffer Query : register(b0) { uint offset; uint index; };
        [numthreads(1,1,1)] void main() { output[index] = scene.Load4(offset); }
    )";
    ComPtr<ID3DBlob> bytecode, errors, signature;
    assert(SUCCEEDED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", "cs_5_0",
                                D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &bytecode, &errors)));
    CD3DX12_ROOT_PARAMETER parameters[3];
    parameters[0].InitAsShaderResourceView(0);
    parameters[1].InitAsUnorderedAccessView(0);
    parameters[2].InitAsConstants(2, 0);
    const CD3DX12_ROOT_SIGNATURE_DESC description(3, parameters);
    assert(SUCCEEDED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDescription{};
    pipelineDescription.pRootSignature = root.Get();
    pipelineDescription.CS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDescription, IID_PPV_ARGS(&pipeline))));

    using ReadValue = std::array<uint32_t, 4>;
    const auto outputDescription =
        CD3DX12_RESOURCE_DESC::Buffer(kReadCount * sizeof(ReadValue), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readbackDescription = CD3DX12_RESOURCE_DESC::Buffer(kReadCount * sizeof(ReadValue));
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT), readHeap(D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12Resource> output, readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDescription,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(&output))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));

    GFX::GpuDeferredReleaseQueue retirement;
    GFX::Context context{};
    context.device = gpu.device.Get();
    context.queue = gpu.queue.Get();
    context.cmdList = gpu.commands.Get();
    context.deferredReleaseQueue = &retirement;
    context.frameIndex = 0u;
    context.currentFrameRetireFenceValue = 13u;
    ComPtr<ID3D12Fence> slotFence;
    assert(SUCCEEDED(gpu.device->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&slotFence))));
    context.frameFence = slotFence.Get();

    auto snapshotA = std::make_shared<GpuSceneJointPaletteSnapshot>();
    snapshotA->matrices.resize(2u, MATH::Mat4::Identity());
    snapshotA->matrices[0].m[0][0] = 11.0f;
    snapshotA->matrices[0].m[0][1] = 12.0f;
    snapshotA->matrices[0].m[0][2] = 13.0f;
    snapshotA->matrices[0].m[0][3] = 14.0f;
    snapshotA->matrices[1].m[0][0] = 21.0f;
    snapshotA->matrices[1].m[0][1] = 22.0f;
    snapshotA->matrices[1].m[0][2] = 23.0f;
    snapshotA->matrices[1].m[0][3] = 24.0f;
    auto snapshotB = std::make_shared<GpuSceneJointPaletteSnapshot>();
    snapshotB->matrices.resize(1u, MATH::Mat4::Identity());
    snapshotB->matrices[0].m[0][0] = 31.0f;
    snapshotB->matrices[0].m[0][1] = 32.0f;
    snapshotB->matrices[0].m[0][2] = 33.0f;
    snapshotB->matrices[0].m[0][3] = 34.0f;
    const GpuSceneJointPaletteSnapshotPtr publishedA = snapshotA;
    const GpuSceneJointPaletteSnapshotPtr publishedB = snapshotB;
    const D3D12_GPU_VIRTUAL_ADDRESS addressA = UploadGpuSceneJointPaletteSnapshot(context, publishedA);
    const D3D12_GPU_VIRTUAL_ADDRESS addressAReuse = UploadGpuSceneJointPaletteSnapshot(context, publishedA);
    const D3D12_GPU_VIRTUAL_ADDRESS addressB = UploadGpuSceneJointPaletteSnapshot(context, publishedB);
    const GpuSceneJointPaletteSnapshotPtr emptySnapshot{};
    const D3D12_GPU_VIRTUAL_ADDRESS emptyAddress = UploadGpuSceneJointPaletteSnapshot(context, emptySnapshot);
    const D3D12_GPU_VIRTUAL_ADDRESS emptyAddressReuse = UploadGpuSceneJointPaletteSnapshot(context, emptySnapshot);
    assert(addressA != 0u && addressA == addressAReuse && addressB != 0u && addressA != addressB);
    assert(emptyAddress != 0u && emptyAddress == emptyAddressReuse && emptyAddress != addressA &&
           emptyAddress != addressB);
    const auto stats = GetGpuSceneJointPaletteUploadStats(context);
    assert(stats.matrixPayloadBytes == 3u * sizeof(MATH::Mat4));
    // 蒙皮の無い束縛は常駐の単位行列を返し、upload も統計も増やさない。
    assert(stats.snapshotUploadCount == 2u && stats.snapshotReuseCount == 1u);

    gpu.ResetCommands();
    gpu.commands->SetComputeRootSignature(root.Get());
    gpu.commands->SetPipelineState(pipeline.Get());
    gpu.commands->SetComputeRootUnorderedAccessView(1u, output->GetGPUVirtualAddress());
    const auto dispatchRead = [&](D3D12_GPU_VIRTUAL_ADDRESS address, UINT offset, UINT index) {
        const UINT query[]{offset, index};
        gpu.commands->SetComputeRootShaderResourceView(0u, address);
        gpu.commands->SetComputeRoot32BitConstants(2u, 2u, query, 0u);
        gpu.commands->Dispatch(1u, 1u, 1u);
    };
    dispatchRead(addressA, 0u, 0u);
    dispatchRead(addressA, static_cast<UINT>(sizeof(MATH::Mat4)), 1u);
    dispatchRead(addressB, 0u, 2u);
    dispatchRead(emptyAddress, 0u, 3u);
    const auto outputBarrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                    D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.commands->ResourceBarrier(1u, &outputBarrier);
    gpu.commands->CopyResource(readback.Get(), output.Get());
    gpu.SubmitAndWait(13u);

    std::array<ReadValue, kReadCount> expected{};
    std::memcpy(expected[0].data(), snapshotA->matrices[0].m[0], sizeof(expected[0]));
    std::memcpy(expected[1].data(), snapshotA->matrices[1].m[0], sizeof(expected[1]));
    std::memcpy(expected[2].data(), snapshotB->matrices[0].m[0], sizeof(expected[2]));
    const MATH::Mat4 identity = MATH::Mat4::Identity();
    std::memcpy(expected[3].data(), identity.m[0], sizeof(expected[3]));
    void* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0u, nullptr, &mapped)));
    assert(std::memcmp(mapped, expected.data(), sizeof(expected)) == 0);
    readback->Unmap(0u, nullptr);

    context.currentFrameRetireFenceValue = 14u;
    assert(UploadGpuSceneJointPaletteSnapshot(context, publishedA) == 0u);
    assert(SUCCEEDED(slotFence->Signal(13u)));
    assert(UploadGpuSceneJointPaletteSnapshot(context, publishedA) != 0u);
    context.frameIndex = 1u;
    assert(UploadGpuSceneJointPaletteSnapshot(context, publishedA) != 0u);
    context.frameIndex = 2u;
    context.currentFrameRetireFenceValue = 15u;
    assert(UploadGpuSceneJointPaletteSnapshot(context, publishedA) != 0u);

    gpu.ResetCommands();
    gpu.SubmitAndWait(15u);
    context.frameFence = gpu.fence.Get();
    context.currentFrameRetireFenceValue = 15u;
    FinalizeGpuSceneJointPaletteUploads(context);
    retirement.Collect(15u);
    assert(retirement.GetPendingCount() == 0u);
    std::puts("Joint palette upload: immutable reuse, append/readback, empty binding, and slot fence guard PASS");
}

void VerifyTexturePublication(HIKARI::TESTS::INTERNAL::GpuContractDevice& gpu) {
    using namespace HIKARI;
    using namespace HIKARI::DXTEX;
    using Manager = DxTextureManager;
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    GFX::GpuDeferredReleaseQueue retirement;
    GFX::Context context{};
    context.device = gpu.device.Get();
    context.queue = gpu.queue.Get();
    context.deferredReleaseQueue = &retirement;
    context.currentFrameRetireFenceValue = 10;
    assert(Manager::Init(context, 16));
    const auto root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "texture-publication";
    std::filesystem::create_directories(root);
    std::array<uint8_t, 16> pixels{255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 16, 32, 48, 255};
    DirectX::Image image{2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 8, 16, pixels.data()};
    const auto png = root / L"texture-色.png", tga = root / "texture.tga", dds = root / "texture.dds";
    assert(SUCCEEDED(DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
                                            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png.c_str())));
    assert(SUCCEEDED(DirectX::SaveToTGAFile(image, DirectX::TGA_FLAGS_NONE, tga.c_str())));
    assert(SUCCEEDED(DirectX::SaveToDDSFile(image, DirectX::DDS_FLAGS_NONE, dds.c_str())));
    const auto check = [&](int handle, bool cube, const uint8_t* expected, size_t width, size_t height) {
        assert(Manager::IsTextureHandleValid(handle));
        DirectX::ScratchImage captured;
        assert(SUCCEEDED(DirectX::CaptureTexture(gpu.queue.Get(), Manager::GetResource(handle), cube, captured,
                                                 D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)));
        assert(captured.GetMetadata().width == width && captured.GetMetadata().height == height);
        for (size_t face = 0; face < (cube ? 6u : 1u); ++face) {
            const auto* result = captured.GetImage(0, face, 0);
            assert(result);
            for (size_t y = 0; y < height; ++y)
                assert(std::memcmp(result->pixels + y * result->rowPitch, expected + y * width * 4, width * 4) == 0);
        }
    };
    for (const auto& path : {png, tga, dds}) {
        const auto utf8 = TEXT::PathToUtf8(path);
        const int linear = Manager::LoadTextureLinear("test", utf8);
        check(linear, false, pixels.data(), 2, 2);
        assert(Manager::LoadTextureLinear("alias", utf8) == linear);
        const int srgb = Manager::LoadTextureSrgb("test", utf8);
        assert(srgb >= 0 && srgb != linear);
        assert(Manager::GetTextureFormat(srgb) == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        check(srgb, false, pixels.data(), 2, 2);
        Manager::InvalidateTextureCacheByPath(utf8);
        assert(Manager::FindLoadedTextureWithColorSpace("test", utf8, TextureColorSpace::Linear) < 0);
        Manager::ReleaseTexture(linear);
        Manager::ReleaseTexture(srgb);
    }
    const int generated = Manager::CreateRgbaTexture(
        "generated", 2, 2, std::vector<uint8_t>(pixels.begin(), pixels.end()), TextureColorSpace::Linear);
    check(generated, false, pixels.data(), 2, 2);
    assert(Manager::GetTextureMipCount(generated) == 2);
    Manager::ReleaseTexture(generated);
    const int cube = Manager::CreateSolidColorCubemap("cube", 0x123456ff, TextureColorSpace::Linear);
    const std::array<uint8_t, 4> cubePixel{0x12, 0x34, 0x56, 0xff};
    check(cube, true, cubePixel.data(), 1, 1);
    DirectX::ScratchImage cubeImage;
    assert(SUCCEEDED(DirectX::CaptureTexture(gpu.queue.Get(), Manager::GetResource(cube), true, cubeImage,
                                             D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)));
    const auto cubePath = root / "cube.dds";
    assert(SUCCEEDED(DirectX::SaveToDDSFile(cubeImage.GetImages(), cubeImage.GetImageCount(), cubeImage.GetMetadata(),
                                            DirectX::DDS_FLAGS_NONE, cubePath.c_str())));
    const int loadedCube = Manager::LoadCubemap("file", TEXT::PathToUtf8(cubePath), TextureColorSpace::Linear);
    check(loadedCube, true, cubePixel.data(), 1, 1);
    Manager::ReleaseTexture(loadedCube);
    Manager::ReleaseTextureDeferred(cube);
    assert(!Manager::IsTextureHandleValid(cube) && retirement.GetPendingCount() == 1);
    retirement.Collect(9);
    assert(Manager::GetUsedDescriptorCount() == 1);
    retirement.Collect(10);
    assert(Manager::GetUsedDescriptorCount() == 0);
    // 旧世代の回収が、新しい manager の同じ descriptor slot を解放してはならない。
    Manager::Finalize();
    assert(Manager::Init(context, 16));
    const int old = Manager::CreateSolidColorTexture("old", 0xffffffff);
    Manager::ReleaseTextureDeferred(old);
    Manager::Finalize();
    assert(Manager::Init(context, 16));
    const int current = Manager::CreateSolidColorTexture("current", 0xffffffff);
    assert(current == old);
    retirement.Collect(10);
    assert(Manager::IsTextureHandleValid(current) && Manager::GetUsedDescriptorCount() == 1);
    Manager::Finalize();
    CoUninitialize();
    std::puts("Texture uploads: WIC/TGA/DDS, cube, generated mips, cache and deferred lifetime PASS");
}

void VerifyAsyncUploadQueue(HIKARI::TESTS::INTERNAL::GpuContractDevice& gpu) {
    using Microsoft::WRL::ComPtr;
    HIKARI::GFX::UploadSubmissionQueue uploads;
    assert(uploads.Initialize(gpu.device.Get(), gpu.queue.Get(), {2, 128}));
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(64);
    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD), readHeap(D3D12_HEAP_TYPE_READBACK);
    std::vector<ComPtr<ID3D12Resource>> results;
    for (uint32_t value = 1; value <= 33; ++value) {
        ComPtr<ID3D12Resource> source, result;
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&source))));
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(
            &readHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result))));
        void* mapped = nullptr;
        assert(SUCCEEDED(source->Map(0, nullptr, &mapped)));
        std::fill_n(static_cast<uint32_t*>(mapped), 16, value);
        source->Unmap(0, nullptr);
        auto* commands = uploads.Begin();
        assert(commands);
        commands->CopyBufferRegion(result.Get(), 0, source.Get(), 0, 64);
        ID3D12Resource* retained[]{source.Get(), result.Get()};
        assert(uploads.Submit(retained, 64));
        // 呼出元は staging を即時解放。queue が fence まで保持する。
        source.Reset();
        results.push_back(std::move(result));
        assert(uploads.GetStats().pendingBytes <= 128);
    }
    assert(uploads.GetStats().submissions == 33);
    assert(uploads.Begin());
    uploads.Cancel();
    uploads.Reset();
    for (uint32_t i = 0; i < results.size(); ++i) {
        void* mapped = nullptr;
        assert(SUCCEEDED(results[i]->Map(0, nullptr, &mapped)));
        for (size_t element = 0; element < 16; ++element)
            assert(static_cast<const uint32_t*>(mapped)[element] == i + 1);
        results[i]->Unmap(0, nullptr);
    }
    std::puts("Async upload queue: staging release, bounded reuse, cancel and drain PASS");
}
void VerifyDirtyRanges() {
    HIKARI::GFX::DirtyUploadRanges ranges;
    ranges.Add(5, 8);
    ranges.Add(1, 3);
    ranges.Add(3, 5);
    assert(ranges.Ranges().size() == 1 && ranges.Ranges()[0].first == 1 && ranges.Ranges()[0].end == 8);
    ranges.Add(4, 6);
    ranges.Add(9, 9);
    assert(ranges.Ranges().size() == 1);
    ranges.Clear();
    for (size_t i = 200; i > 0; --i)
        ranges.Add(i * 4, i * 4 + 1);
    assert(ranges.Ranges().size() <= ranges.kMaximumRanges);
    for (size_t i = 1; i <= 200; ++i) {
        bool covered = false;
        for (const auto& range : ranges.Ranges())
            covered |= range.first <= i * 4 && range.end > i * 4;
        assert(covered);
    }
    for (size_t i = 1; i < ranges.Ranges().size(); ++i)
        assert(ranges.Ranges()[i - 1].end < ranges.Ranges()[i].first);
}
void VerifySurfaceSceneSnapshots(HIKARI::TESTS::INTERNAL::GpuContractDevice& gpu) {
    using HIKARI::RENDER3D::RUNTIME::SurfaceGpuSceneInstance;
    using Microsoft::WRL::ComPtr;
    constexpr UINT viewCount = 17, records = 257, edits = 5, snapshots = viewCount * edits;
    const char* shader = R"(
            ByteAddressBuffer scene : register(t0);
            RWStructuredBuffer<uint4> output : register(u0);
            cbuffer Query : register(b0) { uint offset; uint index; };
            [numthreads(1,1,1)] void main() { output[index] = scene.Load4(offset); }
        )";
    ComPtr<ID3DBlob> bytecode, errors, signature;
    assert(SUCCEEDED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", "cs_5_0",
                                D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &bytecode, &errors)));
    CD3DX12_ROOT_PARAMETER parameters[3];
    parameters[0].InitAsShaderResourceView(0);
    parameters[1].InitAsUnorderedAccessView(0);
    parameters[2].InitAsConstants(2, 0);
    const CD3DX12_ROOT_SIGNATURE_DESC description(3, parameters);
    assert(SUCCEEDED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDescription{};
    pipelineDescription.pRootSignature = root.Get();
    pipelineDescription.CS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDescription, IID_PPV_ARGS(&pipeline))));
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, HIKARI::GFX::kFrameResourceCount,
                                               D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&heap))));
    HIKARI::RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer scene;
    assert(scene.Initialize(gpu.device.Get(), heap->GetCPUDescriptorHandleForHeapStart(),
                            heap->GetGPUDescriptorHandleForHeapStart(),
                            gpu.device->GetDescriptorHandleIncrementSize(heapDescription.Type), records));
    const auto outputDescription =
        CD3DX12_RESOURCE_DESC::Buffer(snapshots * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readbackDescription = CD3DX12_RESOURCE_DESC::Buffer(snapshots * 16);
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT), readHeap(D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12Resource> output, readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDescription,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(&output))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));
    HIKARI::GFX::Context context{};
    context.device = gpu.device.Get();
    context.queue = gpu.queue.Get();
    context.cmdList = gpu.commands.Get();
    constexpr UINT offset = offsetof(SurfaceGpuSceneInstance, materialDataIndex);
    std::array<std::array<uint32_t, 4>, snapshots> expected{};
    for (uint64_t frame = 10; frame < 13; ++frame) {
        context.currentFrameRetireFenceValue = frame;
        HIKARI::GFX::PublishCurrentContext(context);
        gpu.ResetCommands();
        if (frame != 10) {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            gpu.commands->ResourceBarrier(1, &barrier);
        }
        gpu.commands->SetComputeRootSignature(root.Get());
        gpu.commands->SetPipelineState(pipeline.Get());
        gpu.commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
        for (UINT view = 0; view < viewCount; ++view) {
            scene.BeginFrame(0);
            std::array<SurfaceGpuSceneInstance, records> instances{};
            instances[1].materialDataIndex = UINT(frame * 1000 + view * 10);
            scene.Upload(instances.data(), instances.size());
            for (UINT edit = 0; edit < edits; ++edit) {
                if (edit != 0) {
                    ++instances[1].materialDataIndex;
                    assert(scene.PatchMaterialDataIndex(1, instances[1].materialDataIndex));
                    instances.back().materialDataIndex += 7;
                    assert(scene.PatchMaterialDataIndex(records - 1, instances.back().materialDataIndex));
                }
                const size_t previousBytes = scene.GetStats().committedBytes;
                assert(scene.CommitFrame(gpu.commands.Get()));
                if (edit != 0)
                    assert(scene.GetStats().committedBytes - previousBytes == 2 * sizeof(SurfaceGpuSceneInstance));
                const UINT sample = edit == 2 ? records / 2 : (edit % 2 ? records - 1 : 1);
                const UINT query[]{UINT(sample * sizeof(SurfaceGpuSceneInstance) + offset), view * edits + edit};
                std::memcpy(expected[query[1]].data(), reinterpret_cast<const uint8_t*>(&instances[sample]) + offset,
                            16);
                gpu.commands->SetComputeRootShaderResourceView(0, scene.GetGpuVirtualAddress());
                gpu.commands->SetComputeRoot32BitConstants(2, 2, query, 0);
                gpu.commands->Dispatch(1, 1, 1);
            }
        }
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
        gpu.commands->ResourceBarrier(1, &barrier);
        gpu.commands->CopyResource(readback.Get(), output.Get());
        gpu.SubmitAndWait(frame);
        void* mapped = nullptr;
        assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
        const auto* actual = static_cast<const std::array<uint32_t, 4>*>(mapped);
        for (UINT i = 0; i < snapshots; ++i) {
            if (actual[i] != expected[i])
                std::fprintf(stderr, "GPU scene snapshot %u: material %u, expected %u\n", i, actual[i][0],
                             expected[i][0]);
            assert(actual[i] == expected[i]);
        }
        readback->Unmap(0, nullptr);
    }
    HIKARI::GFX::ClearCurrentContext();
    std::puts(
        "GPU scene uploads: 17 views x 5 sparse commits x 3 delayed frames: exact dirty bytes and untouched records PASS");
}
} // namespace

int main() {
    VerifyDirtyRanges();
    VerifyScenePublication();
    VerifyCompactJointPalettePublication();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using Microsoft::WRL::ComPtr;
    HIKARI::TESTS::INTERNAL::GpuContractDevice gpu;
    VerifyAsyncUploadQueue(gpu);
    VerifyTexturePublication(gpu);
    constexpr UINT kSubmissions = 17u;
    constexpr UINT kBytes = kSubmissions * sizeof(float) * 4u;
    const char* shader = R"(
        cbuffer Snapshot : register(b0) { float4 value; };
        cbuffer Destination : register(b1) { uint index; };
        RWStructuredBuffer<float4> output : register(u0);
        [numthreads(1, 1, 1)] void main() { output[index] = value; }
    )";
    ComPtr<ID3DBlob> bytecode, errors;
    assert(SUCCEEDED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", "cs_5_0",
                                D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &bytecode, &errors)));
    std::array<CD3DX12_ROOT_PARAMETER, 3> parameters;
    parameters[0].InitAsConstantBufferView(0);
    parameters[1].InitAsConstants(1, 1);
    parameters[2].InitAsUnorderedAccessView(0);
    CD3DX12_ROOT_SIGNATURE_DESC rootDescription(static_cast<UINT>(parameters.size()), parameters.data());
    ComPtr<ID3DBlob> signature;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDescription{};
    pipelineDescription.pRootSignature = root.Get();
    pipelineDescription.CS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDescription, IID_PPV_ARGS(&pipeline))));
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    const auto outputDescription = CD3DX12_RESOURCE_DESC::Buffer(kBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readbackDescription = CD3DX12_RESOURCE_DESC::Buffer(kBytes);
    ComPtr<ID3D12Resource> output, readback;
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDescription,
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                         IID_PPV_ARGS(&output))));
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));

    HIKARI::DX::DynamicUploadBuffer upload;
    assert(upload.Init(gpu.device.Get(), 512u, HIKARI::GFX::UPLOAD_TRAFFIC::Owner::PassConstants));
    HIKARI::GFX::GpuDeferredReleaseQueue retirement;
    HIKARI::GFX::Context context{};
    context.deferredReleaseQueue = &retirement;
    std::array<D3D12_GPU_VIRTUAL_ADDRESS, kSubmissions> addresses{};
    for (uint64_t frame = 1u; frame <= 3u; ++frame) {
        gpu.ResetCommands();
        if (frame > 1u) {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            gpu.commands->ResourceBarrier(1, &barrier);
        }
        gpu.commands->SetComputeRootSignature(root.Get());
        gpu.commands->SetPipelineState(pipeline.Get());
        gpu.commands->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
        for (UINT submission = 0; submission < kSubmissions; ++submission) {
            // Cascade のように同じフレームで BeginFrame を繰り返しても古い領域を保持する。
            upload.ResetForFrame(frame);
            void* mapped = upload.Allocate(16u, addresses[submission]);
            assert(mapped != nullptr && addresses[submission] % 256u == 0u);
            for (UINT previous = 0; previous < submission; ++previous)
                assert(addresses[previous] != addresses[submission]);
            const std::array<float, 4> value{static_cast<float>(frame), static_cast<float>(submission),
                                             static_cast<float>(submission * submission),
                                             -static_cast<float>(submission)};
            std::memcpy(mapped, value.data(), sizeof(value));
            gpu.commands->SetComputeRootConstantBufferView(0, addresses[submission]);
            gpu.commands->SetComputeRoot32BitConstant(1, submission, 0);
            gpu.commands->Dispatch(1, 1, 1);
        }
        const auto copyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        gpu.commands->ResourceBarrier(1, &copyBarrier);
        gpu.commands->CopyResource(readback.Get(), output.Get());
        if (frame == 3u) {
            context.currentFrameRetireFenceValue = frame;
            upload.Finalize(&context);
            assert(!upload.IsInitialized() && retirement.GetPendingCount() >= 9u);
            retirement.Collect(frame - 1u);
            assert(retirement.GetPendingCount() >= 9u);
        }
        // 全 CPU 書き込みと最終フレームの破棄要求の後に初めて GPU を実行する。
        gpu.SubmitAndWait(frame);
        float* pixels = nullptr;
        const D3D12_RANGE readRange{0, kBytes};
        assert(SUCCEEDED(readback->Map(0, &readRange, reinterpret_cast<void**>(&pixels))));
        for (UINT i = 0; i < kSubmissions; ++i) {
            const std::array<float, 4> expected{static_cast<float>(frame), static_cast<float>(i),
                                                static_cast<float>(i * i), -static_cast<float>(i)};
            for (UINT c = 0; c < 4u; ++c)
                assert(pixels[i * 4u + c] == expected[c]);
        }
        readback->Unmap(0, nullptr);
        retirement.Collect(frame);
    }
    assert(retirement.GetPendingCount() == 0u);
    VerifySurfaceSceneSnapshots(gpu);
    VerifyJointPaletteUpload(gpu);
    std::puts(
        "Immutable uploads: 17 delayed GPU submissions x 3 frames, page growth and fence retirement PASS (no window)");
}
