#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <d3dx12.h>
#include "Assets/Material/Compiler/HIKARI_StandardPbrProgramSource.h"
#include "Assets/Vfx/HIKARI_VfxPrewarm.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Texture/HIKARI_HtexFormat.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/HIKARI_ParticleGpuDrivenPass.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleShaderInterop.h"
#include "Internal/HIKARI_GpuContractDevice.h"

namespace {
void CheckParticleAssetPrewarm() {
    using namespace HIKARI;
    using namespace RENDER3D::PARTICLES;
    using Microsoft::WRL::ComPtr;
    assert(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    TESTS::INTERNAL::GpuContractDevice gpu;
    const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GFX::DESCRIPTOR::kSrvHeapCapacity,
                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    GFX::Context graphics{};
    graphics.device = gpu.device.Get();
    graphics.deviceGeneration = 1;
    graphics.cmdList = gpu.commands.Get();
    graphics.queue = gpu.queue.Get();
    graphics.srvHeap = heap.Get();
    RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphics);
    assert(DXTEX::DxTextureManager::Init(graphics));
    RENDER3D::MATERIAL::GpuMaterialRegistry materials;
    assert(materials.Initialize(64, GFX::kFrameResourceCount));
    // 標準材質は registry の標準 PBR の出どころ（Assets のコンパイラ）から作る。起動する側の代わりにここで渡す。
    RENDER3D::MATERIAL::ResetRuntimeMaterialProgramRegistry(ASSETS::MATERIAL::MakeStandardPbrProgramSource());
    AssetRegistry assets;
    ParticleRenderInput input{};
    input.sourceIdentity = 42;
    input.deliverySequence = 1;
    const ParticleRenderFrameSource source{&input, &assets};
    ParticleGpuDrivenPass pass;
    gpu.ResetCommands();
    // VFX のない場面にはプールを作らない。登録後の空フレームでは再生せず資源だけを準備する。
    assert(pass.PrepareFrame(graphics, &source, materials));
    assert(!pass.GetStats().ready);
    const auto root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "particle-prewarm";
    std::filesystem::create_directories(root);
    const auto addTexture = [&](const char* guid, const char* filename) {
        const auto path = root / filename;
        HtexTexture texture{};
        texture.width = texture.height = 1;
        texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.colorSpace = TextureAssetColorSpace::Srgb;
        HtexSubresource pixel{};
        pixel.width = pixel.height = 1;
        pixel.rowPitch = pixel.slicePitch = 4;
        pixel.data = {255, 255, 255, 255};
        texture.subresources.push_back(pixel);
        std::string error;
        assert(WriteHtexFile(path, texture, error));
        auto descriptor = std::make_unique<TextureAssetDescriptor>();
        descriptor->id = AssetGuid{guid};
        descriptor->type = AssetType::Texture;
        descriptor->sourcePath = path.string();
        assert(assets.RegisterDescriptor(std::move(descriptor)));
        return path.string();
    };
    constexpr const char* usedGuid = "engine.vfx.texture.particle_hard";
    constexpr const char* unusedGuid = "engine.vfx.texture.star_soft";
    const auto usedPath = addTexture(usedGuid, "used.htex");
    const auto unusedPath = addTexture(unusedGuid, "unused.htex");
    auto program = std::make_shared<VFX::VfxProgram>();
    program->emitters.emplace_back().render.colorTextureAssetGuid = usedGuid;
    // 別の壊れた依存があっても、使用可能な特効の予熱は完了させる。
    program->emitters.emplace_back().render.colorTextureAssetGuid = "test.missing.texture";
    const auto registerProgram = [&] {
        auto descriptor = std::make_unique<VfxAssetDescriptor>();
        descriptor->id = AssetGuid{"test.prewarm." + std::to_string(program->emitters.size())};
        descriptor->type = AssetType::VfxEffect;
        descriptor->program = std::make_shared<const VFX::VfxProgram>(*program);
        descriptor->prewarm = VFX::CollectVfxPrewarmMaterials(*descriptor->program);
        assert(assets.RegisterDescriptor(std::move(descriptor)));
    };
    const auto findTexture = [](const char* guid, const std::string& path) {
        return DXTEX::DxTextureManager::FindLoadedTextureWithColorSpace(
            "material_asset/standard:" + std::string(guid) + "/baseColor", path, DXTEX::TextureColorSpace::Srgb);
    };
    registerProgram();
    assert(pass.PrepareFrame(graphics, &source, materials));
    if (!pass.GetStats().ready) {
        for (const auto& record : CORE::ReadLogHistory())
            std::fprintf(stderr, "%s\n", record.message.c_str());
        for (const auto& error : DEBUGLOG::GetRecentRenderErrors(16))
            std::fprintf(stderr, "%s\n", error.c_str());
    }
    assert(pass.GetStats().ready && pass.GetStats().allocatedParticles == 0);
    assert(pass.GetStats().spawnedParticles == 0 && !pass.HasDrawWork());
    assert(input.emitters.empty() && input.deliverySequence == 1);
    const int textureHandle = findTexture(usedGuid, usedPath);
    assert(textureHandle >= 0 && findTexture(unusedGuid, unusedPath) < 0);
    const UINT descriptorsBefore = DXTEX::DxTextureManager::GetUsedDescriptorCount();
    // 同じ登録世代で繰り返しても貼図とプールを作り直さず、未使用の内蔵貼図も読み込まない。
    assert(pass.PrepareFrame(graphics, &source, materials));
    assert(findTexture(usedGuid, usedPath) == textureHandle);
    assert(DXTEX::DxTextureManager::GetUsedDescriptorCount() == descriptorsBefore);
    // 最初の本当の事件だけが粒子を生成し、空フレームの予熱は生成数に混ざらない。
    TIME::Reset();
    input.frameIndex = TIME::BeginFrame().frameIndex;
    auto& emission = input.emitters.emplace_back();
    emission.key = {42, 1, 1, 0};
    emission.resetSimulation = true;
    emission.spawnCount = 4;
    emission.simulation.capacity = 64;
    emission.simulation.lifetime.minimum = emission.simulation.lifetime.maximum = 1;
    emission.render.colorTextureAssetGuid = usedGuid;
    materials.BeginFrame();
    assert(pass.PrepareFrame(graphics, &source, materials));
    assert(pass.GetStats().spawnedParticles == 4 && pass.GetStats().activeEmitters == 1);
    const auto allocatedParticles = pass.GetStats().allocatedParticles;
    assert(allocatedParticles >= 64);
    assert(DXTEX::DxTextureManager::GetUsedDescriptorCount() == descriptorsBefore);
    gpu.SubmitAndWait(1);
    gpu.ResetCommands();
    input.emitters.clear();
    input.frameIndex = TIME::BeginFrame().frameIndex;
    ++input.deliverySequence;
    program->emitters.emplace_back().render.colorTextureAssetGuid = unusedGuid;
    registerProgram();
    assert(pass.PrepareFrame(graphics, &source, materials));
    assert(findTexture(unusedGuid, unusedPath) >= 0);
    assert(findTexture(usedGuid, usedPath) == textureHandle && !pass.HasDrawWork());
    assert(pass.GetStats().allocatedParticles == allocatedParticles);
    gpu.SubmitAndWait(2);
    pass.Shutdown();
    DXTEX::DxTextureManager::Finalize();
    RENDER3D::ShutdownRenderResourceDescriptorPool();
    std::puts("Particle prewarm: empty scene, cold dependencies, reuse and asset revision PASS");
}

void CheckRuntimeRingBounds() {
    using namespace HIKARI;
    using Microsoft::WRL::ComPtr;
    TESTS::INTERNAL::GpuContractDevice gpu;
    // Execute the production frustum function, including its current-frame shape state.
    const std::string shader = R"(
#include "Render3D_ParticleGpuDrivenDraw.hlsl"
RWStructuredBuffer<uint> results : register(u0);
[numthreads(4, 1, 1)] void CheckBounds(uint3 id : SV_DispatchThreadID) {
    gShapeState = (GpuParticleShapeState)0;
    const float radius = (id.x == 0 || id.x == 2) ? 27.0 : 1.0;
    gShapeState.renderShapeParamsA = float4(0, radius, radius - 0.8, 2.0943951);
    Particle particle = (Particle)0;
    particle.positionAge = float4(id.x == 3 ? 0 : (id.x == 2 ? 31 : 20), 0, 0.5, 0);
    particle.scaleRotation = float4(1, 1, 0, 1);
    results[id.x] = ParticleInFrustum(particle) ? 1u : 0u;
})";
    const auto sourcePath =
        std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) / "HIKARI/Shaders/RuntimeRingBoundsContract.hlsl";
    ComPtr<ID3DBlob> code;
    assert(GFX::CompileShaderSourceSm6(shader.data(), shader.size(), sourcePath.c_str(), "CheckBounds",
                                       GFX::ShaderStage::Compute, &code));
    std::array<CD3DX12_ROOT_PARAMETER, 3> roots;
    roots[0].InitAsConstantBufferView(0);
    roots[1].InitAsConstantBufferView(1);
    roots[2].InitAsUnorderedAccessView(0);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(roots.size()), roots.data());
    ComPtr<ID3DBlob> signature, error;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = root.Get();
    pipelineDesc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline))));
    const auto buffer = [&](UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> resource;
        const CD3DX12_HEAP_PROPERTIES heap(type);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes, type == D3D12_HEAP_TYPE_DEFAULT
                                                                   ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                                   : D3D12_RESOURCE_FLAG_NONE);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                             IID_PPV_ARGS(&resource))));
        return resource;
    };
    auto constants = buffer(512, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto output = buffer(16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto readback = buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<std::byte, 512> values{};
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(values.data(), identity.data(), sizeof(identity));
    RENDER3D::PARTICLES::INTERNAL::GpuParticleDrawConstants draw{};
    draw.renderShapeMode = 2;
    draw.renderShapeParamsB.x = 7.3303829f;
    draw.depthParams = {0, 0, 1, 1};
    std::memcpy(values.data() + 256, &draw, sizeof(draw));
    void* mapped = nullptr;
    assert(SUCCEEDED(constants->Map(0, nullptr, &mapped)));
    std::memcpy(mapped, values.data(), values.size());
    constants->Unmap(0, nullptr);
    gpu.ResetCommands();
    auto* commands = gpu.commands.Get();
    commands->SetComputeRootSignature(root.Get());
    commands->SetPipelineState(pipeline.Get());
    commands->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    commands->SetComputeRootConstantBufferView(1, constants->GetGPUVirtualAddress() + 256);
    commands->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
    commands->Dispatch(1, 1, 1);
    const auto ready = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->ResourceBarrier(1, &ready);
    commands->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, 16);
    gpu.SubmitAndWait(1);
    assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
    constexpr std::array<uint32_t, 4> expected{1, 0, 0, 1};
    assert(std::memcmp(mapped, expected.data(), sizeof(expected)) == 0);
    readback->Unmap(0, nullptr);
    std::puts("Runtime ring bounds GPU: 27m offscreen center retained; small/far rings culled PASS");
}
} // namespace

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    CheckParticleAssetPrewarm();
    CheckRuntimeRingBounds();
    using namespace HIKARI;
    using namespace RENDER3D::PARTICLES::INTERNAL;
    using Microsoft::WRL::ComPtr;
    TESTS::INTERNAL::GpuContractDevice gpu;
    constexpr uint32_t emitters = 33;
    std::vector<GpuParticle> particles;
    std::array<GpuParticleCompactConstants, emitters> draws{};
    std::array<std::vector<uint32_t>, emitters> expected;
    for (uint32_t emitter = 0; emitter < emitters; ++emitter) {
        auto& draw = draws[emitter];
        draw.particleOffset = draw.aliveIndexBase = uint32_t(particles.size());
        draw.particleCapacity = std::array<uint32_t, 5>{1, 63, 64, 65, 127}[emitter % 5];
        draw.drawIndex = emitter;
        draw.vertexCount = 6 + 3 * emitter;
        for (uint32_t i = 0; i < draw.particleCapacity; ++i) {
            GpuParticle particle;
            const bool alive = emitter % 3 != 0 && (emitter % 3 == 2 || i % 4 == 0);
            particle.velocityLifetime.w = alive ? 1.0f : 0.0f;
            if (alive)
                expected[emitter].push_back(uint32_t(particles.size()));
            particles.push_back(particle);
        }
    }
    std::array<CD3DX12_ROOT_PARAMETER, 5> roots;
    for (UINT i = 0; i < 4; ++i)
        roots[i].InitAsUnorderedAccessView(i);
    roots[4].InitAsConstants(sizeof(GpuParticleCompactConstants) / 4, 1);
    const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(UINT(roots.size()), roots.data());
    ComPtr<ID3DBlob> signature, error;
    assert(SUCCEEDED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)));
    ComPtr<ID3D12RootSignature> root;
    assert(SUCCEEDED(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                     IID_PPV_ARGS(&root))));
    std::array<ComPtr<ID3D12PipelineState>, 3> pipelines;
    constexpr std::array entries{"ResetDrawCS", "CompactDrawCS", "BuildDrawArgsCS"};
    const auto shader =
        std::filesystem::path(HIKARI_CONTRACT_SOURCE_ROOT) / "HIKARI/Shaders/Render3D_ParticleGpuDrivenCompute.hlsl";
    for (size_t i = 0; i < pipelines.size(); ++i) {
        ComPtr<ID3DBlob> code;
        assert(GFX::CompileShaderFileSm6(shader.c_str(), entries[i], GFX::ShaderStage::Compute, &code));
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
        assert(SUCCEEDED(gpu.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelines[i]))));
    }
    const auto buffer = [&](UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> resource;
        const CD3DX12_HEAP_PROPERTIES heap(type);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes, type == D3D12_HEAP_TYPE_DEFAULT
                                                                   ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                                   : D3D12_RESOURCE_FLAG_NONE);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                             IID_PPV_ARGS(&resource))));
        return resource;
    };
    const std::array<UINT64, 4> sizes{particles.size() * sizeof(GpuParticle), particles.size() * 4, emitters * 4,
                                      emitters * 32};
    for (uint32_t batched = 0; batched < 2; ++batched) {
        std::array<ComPtr<ID3D12Resource>, 4> resources;
        for (size_t i = 0; i < resources.size(); ++i)
            resources[i] = buffer(sizes[i], D3D12_HEAP_TYPE_DEFAULT,
                                  i == 0 ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto upload = buffer(sizes[0], D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto readback =
            buffer(sizes[1] + sizes[2] + sizes[3], D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped = nullptr;
        assert(SUCCEEDED(upload->Map(0, nullptr, &mapped)));
        std::memcpy(mapped, particles.data(), size_t(sizes[0]));
        upload->Unmap(0, nullptr);
        gpu.ResetCommands();
        auto* commands = gpu.commands.Get();
        commands->CopyBufferRegion(resources[0].Get(), 0, upload.Get(), 0, sizes[0]);
        const auto ready = CD3DX12_RESOURCE_BARRIER::Transition(resources[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commands->ResourceBarrier(1, &ready);
        commands->SetComputeRootSignature(root.Get());
        for (UINT i = 0; i < 4; ++i)
            commands->SetComputeRootUnorderedAccessView(i, resources[i]->GetGPUVirtualAddress());
        const auto dispatch = [&](uint32_t phase, uint32_t emitter) {
            const auto& draw = draws[emitter];
            commands->SetComputeRoot32BitConstants(4, sizeof(draw) / 4, &draw, 0);
            commands->Dispatch(phase == 1 ? (draw.particleCapacity + 63) / 64 : 1, 1, 1);
        };
        const auto barrier = [&](uint32_t phase) {
            const std::array<D3D12_RESOURCE_BARRIER, 2> barriers{CD3DX12_RESOURCE_BARRIER::UAV(resources[2].Get()),
                                                                 CD3DX12_RESOURCE_BARRIER::UAV(resources[3].Get())};
            if (phase < 2)
                commands->ResourceBarrier(phase == 0 ? 2 : 1, barriers.data());
        };
        if (batched) {
            for (uint32_t phase = 0; phase < 3; ++phase) {
                commands->SetPipelineState(pipelines[phase].Get());
                for (uint32_t emitter = 0; emitter < emitters; ++emitter)
                    dispatch(phase, emitter);
                barrier(phase);
            }
        } else {
            for (uint32_t emitter = 0; emitter < emitters; ++emitter)
                for (uint32_t phase = 0; phase < 3; ++phase) {
                    commands->SetPipelineState(pipelines[phase].Get());
                    dispatch(phase, emitter);
                    barrier(phase);
                }
        }
        UINT64 offset = 0;
        for (size_t i = 1; i < resources.size(); ++i) {
            const auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
                resources[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1, &transition);
            commands->CopyBufferRegion(readback.Get(), offset, resources[i].Get(), 0, sizes[i]);
            offset += sizes[i];
        }
        gpu.SubmitAndWait(batched + 1);
        assert(SUCCEEDED(readback->Map(0, nullptr, &mapped)));
        const auto* values = static_cast<const uint32_t*>(mapped);
        const auto* counts = values + sizes[1] / 4;
        const auto* arguments = counts + emitters;
        for (uint32_t emitter = 0; emitter < emitters; ++emitter) {
            assert(counts[emitter] == expected[emitter].size());
            std::vector<uint32_t> indices(values + draws[emitter].aliveIndexBase,
                                          values + draws[emitter].aliveIndexBase + counts[emitter]);
            std::sort(indices.begin(), indices.end());
            assert(indices == expected[emitter]);
            const auto* args = arguments + emitter * 8;
            assert(args[0] == (counts[emitter] + 31) / 32 && args[1] == 1 && args[2] == 1 && args[3] == 0);
            assert(args[4] == draws[emitter].vertexCount && args[5] == counts[emitter] && args[6] == 0 && args[7] == 0);
        }
        readback->Unmap(0, nullptr);
    }
    std::puts("Particle compaction GPU: 33 emitters, empty/sparse/full and partial groups PASS; barrier calls 66 -> 2");
}
