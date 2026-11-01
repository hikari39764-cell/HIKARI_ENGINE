#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#include <d3dx12.h>
#include <limits>
#include <thread>
#include <DirectXPackedVector.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "RenderContracts/Shader/HIKARI_PostEffectShaderContract.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/HIKARI_TimeService.h"
#include "HIKARI/Project/ActionViews.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Vfx/Post/HIKARI_ActionPostProfile.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Internal/HIKARI_GpuContractDevice.h"

namespace {
void CheckActionPostSubmission() {
    using namespace HIKARI;
    VFX::ActionPostReactionService service;
    GameProjectActionPostFrameRequest low;
    low.sourceId = 9;
    low.priority = 1;
    low.values[0].x = 99;
    low.channelMasks[0] = 1;
    auto high = low;
    high.sourceId = 3;
    high.priority = 2;
    high.channelMasks[0] = 2;
    high.values[0].y = 7;
    assert(service.SubmitPostProfile("low", low, 10));
    assert(service.SubmitPostProfile("high", high, 10));
    auto loser = high;
    loser.sourceId = 8;
    loser.channelMasks[0] = 15;
    assert(service.SubmitPostProfile("loser", loser, 10));
    for (int fault = 0; fault < 6; ++fault) {
        auto bad = high;
        bad.priority = 100;
        if (fault == 0)
            bad.structSize = 8;
        if (fault == 1)
            bad.version = 2;
        if (fault == 2)
            bad.sourceId = 0;
        if (fault == 3)
            bad.reserved = 1;
        if (fault == 4)
            bad.channelMasks[15] = 16;
        if (fault == 5)
            bad.values[0].y = std::numeric_limits<float>::quiet_NaN();
        assert(!service.SubmitPostProfile("invalid", bad, 10));
    }
    assert(service.ClearPostProfile(low.sourceId, 10));
    std::optional<VFX::ActionPostReactionService::PostProfileFrame> winner;
    assert(service.FreezePostProfile(10, winner));
    assert(winner && winner->profileId == "high" && winner->request.channelMasks[0] == 2);
    assert(!service.SubmitPostProfile("late", low, 10));
    assert(!service.ClearPostProfile(high.sourceId, 10));
    assert(!service.FreezePostProfile(10, winner));
    assert(service.FreezePostProfile(11, winner) && !winner);
    assert(service.SubmitPostProfile("low", low, 12));
    assert(service.SubmitPostProfile("high", high, 12));
    assert(service.ClearPostProfile(high.sourceId, 12));
    assert(service.FreezePostProfile(12, winner) && !winner); // no loser replay
    assert(service.SubmitPostProfile("reset", high, 13));
    service.Reset();
    assert(service.FreezePostProfile(13, winner) && !winner);
    assert(service.SubmitPostProfile("first", low, 14));
    auto replacement = low;
    replacement.channelMasks[0] = 2;
    assert(service.SubmitPostProfile("replacement", replacement, 14));
    assert(service.FreezePostProfile(14, winner) && winner && winner->profileId == "replacement" &&
           winner->request.channelMasks[0] == 2);
    assert(service.SubmitPostProfile("expired", low, 15));
    service.Tick(0.016f, 16);
    assert(service.FreezePostProfile(16, winner) && !winner);

    World world, other;
    bool playing = true, runtimeCamera = true;
    RuntimePlayStateService play{&playing};
    GameplayCameraService camera{nullptr, &runtimeCamera};
    assert(world.Services().Register(service));
    assert(world.Services().Register(play));
    assert(world.Services().Register(camera));
    RuntimeSceneContext::SetCurrentWorld(&world);
    TIME::Reset();
    TIME::BeginFrame();
    const auto view = GameProjectSdkAccess::MakePresentationVfxView(world);
    assert(view.SubmitActionPostProfile("profile", high));
    bool wrongThread = true;
    std::thread thread([&] { wrongThread = view.SubmitActionPostProfile("profile", high); });
    thread.join();
    assert(!wrongThread);
    RuntimeSceneContext::SetCurrentWorld(&other);
    assert(!view.SubmitActionPostProfile("profile", high));
    RuntimeSceneContext::SetCurrentWorld(&world);
    TIME::SetPaused(true);
    assert(!view.SubmitActionPostProfile("profile", high));
    assert(view.ClearActionPost(high.sourceId));
    TIME::SetPaused(false);
    playing = false;
    assert(!view.SubmitActionPostProfile("profile", high));
    playing = true;
    runtimeCamera = false;
    assert(!view.SubmitActionPostProfile("profile", high));
    RuntimeSceneContext::SetCurrentWorld(nullptr);
    std::puts(
        "Action Post CPU: atomic arbitration, validation, freeze/late rejection, expiry, clear/reset, World/thread/pause passed.");
}

void CheckPostSurfaceDraw(HIKARI::TESTS::INTERNAL::GpuContractDevice& gpu,
                          std::shared_ptr<const HIKARI::POST::PostEffectDefinition> definition,
                          const HIKARI::AssetRegistry& registry) {
    using namespace HIKARI;
    using Microsoft::WRL::ComPtr;
    GFX::GpuDeferredReleaseQueue retirement;
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GFX::DESCRIPTOR::kSrvHeapCapacity,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    assert(SUCCEEDED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))));
    GFX::Context context{};
    context.device = gpu.device.Get();
    context.deviceGeneration = 1u;
    context.cmdList = gpu.commands.Get();
    context.queue = gpu.queue.Get();
    context.srvHeap = heap.Get();
    context.deferredReleaseQueue = &retirement;
    context.frameFence = gpu.fence.Get();
    context.currentFrameRetireFenceValue = 1u;
    context.backBufferWidth = 4;
    context.backBufferHeight = 1;
    GFX::PublishCurrentContext(context);
    RENDER3D::UpdateRenderResourceDescriptorPoolContext(context);
    POST::PostEffect::UpdateContext(context);
    const auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> result;
        const CD3DX12_HEAP_PROPERTIES properties(type);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(256u);
        assert(SUCCEEDED(gpu.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                             IID_PPV_ARGS(&result))));
        return result;
    };
    auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12Resource> mask;
    const CD3DX12_HEAP_PROPERTIES properties(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_UINT, 4u, 1u, 1u, 1u);
    assert(SUCCEEDED(gpu.device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mask))));
    void* mapped = nullptr;
    assert(SUCCEEDED(upload->Map(0u, nullptr, &mapped)));
    const uint32_t classifications[]{0u, 1u, 0u, 2u, 0u, 0x80000001u, 0u, 0u};
    std::memcpy(mapped, classifications, sizeof(classifications));
    upload->Unmap(0u, nullptr);
    gpu.ResetCommands();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{0u, {DXGI_FORMAT_R32G32_UINT, 4u, 1u, 1u, 256u}};
    CD3DX12_TEXTURE_COPY_LOCATION from(upload.Get(), footprint), to(mask.Get(), 0u);
    gpu.commands->CopyTextureRegion(&to, 0u, 0u, 0u, &from, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mask.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.commands->ResourceBarrier(1u, &barrier);
    auto maskDescriptor = RENDER3D::AllocateTexture2DSrvDescriptor(mask.Get(), DXGI_FORMAT_R32G32_UINT);
    assert(maskDescriptor.IsValid());
    const RENDER3D::RenderTextureView maskView{
        mask.Get(), maskDescriptor.gpu, DXGI_FORMAT_R32G32_UINT, 4u, 1u, 1u, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        true};
    {
        POST::PostEffect effect;
        assert(effect.LoadPixelShader(definition->passes[0], &registry, true));
        POST::QuadDrawer quad;
        assert(quad.Init(context));
        RenderTarget2D source;
        source.UpdateContext(context);
        assert(source.Init(4, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, false));
        source.BeginCapture(0, 0, 0, 0);
        source.EndCapture();
        POST::PostChain chain;
        chain.UpdateContext(context);
        chain.Add(&effect);
        POST::CommonParams params{};
        params.intensity = 1.f;
        params.resolutionX = 4.f;
        params.resolutionY = 1.f;
        auto* output = chain.Execute(source, quad, params, 0, 0, &maskView);
        assert(output != &source && retirement.GetPendingCount() >= 3u);
        assert(RENDER3D::GetRenderResourceDescriptorPoolStats().used == 4u);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            output->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        gpu.commands->ResourceBarrier(1u, &barrier);
        footprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        from = CD3DX12_TEXTURE_COPY_LOCATION(output->GetResource(), 0u);
        to = CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(), footprint);
        gpu.commands->CopyTextureRegion(&to, 0u, 0u, 0u, &from, nullptr);
        gpu.SubmitAndWait(1u);
        retirement.Collect(1u);
        assert(RENDER3D::GetRenderResourceDescriptorPoolStats().used == 1u);
        assert(SUCCEEDED(readback->Map(0u, nullptr, &mapped)));
        const auto* pixels = static_cast<const uint16_t*>(mapped);
        for (uint32_t pixel = 0u; pixel < 4u; ++pixel) {
            assert(pixels[pixel * 4u] == (pixel == 0u ? 0x3c00u : 0u));
            assert(pixels[pixel * 4u + 1u] == (pixel == 0u ? 0u : 0x3c00u));
        }
        readback->Unmap(0u, nullptr);
        chain.Finalize();

        gpu.ResetCommands();
        context.currentFrameRetireFenceValue = 2u;
        GFX::PublishCurrentContext(context);
        RENDER3D::UpdateRenderResourceDescriptorPoolContext(context);
        POST::PostProcessingStage stage;
        stage.UpdateContext(context);
        BloomSettings bloom;
        bloom.enabled = false;
        stage.SetBloomSettings(bloom);
        stage.UpdateCommonParams(0.016f, 4, 1);
        POST::PostParameterBlock globalValues{};
        globalValues[1].z = 2;
        const POST::ResolvedPostLayer globalLayer{"global", definition, globalValues};
        assert(stage.SetGlobalEffects({&globalLayer, 1u}, &registry));
        assert(stage.SubmitEffectParameter("global", 1, 4, {0, 0, 4, 0}, 1, 1));
        auto authored = *definition;
        VFX::ParamDesc gain;
        gain.key = "gain";
        gain.ref.slot = 0;
        gain.defaultValues[0] = 1;
        VFX::ParamDesc blue;
        blue.key = "blue";
        blue.ref.slot = 1;
        blue.ref.channel = 2;
        blue.defaultValues[0] = 5;
        authored.params = {gain, blue};
        POST::PostProfileAsset profile;
        profile.values["gain"] = {2, 0, 0, 0};
        GameProjectActionPostFrameRequest request;
        request.values[1].x = 99;
        request.channelMasks[1] = 1;
        const auto actionValues = POST::ResolveActionPostParameters(authored, profile, request);
        assert(actionValues[0].x == 2 && actionValues[1].z == 5 && actionValues[1].x == 99);
        const auto frame = TIME::GetFrameContext().frameIndex;
        assert(stage.SetActionPostFrame(42, frame, "action", definition, actionValues, registry));
        assert(!stage.RequiresActionSurfaceFeatureMask()); // not bound to a primary game view
        stage.SetActionPostView(42, true);
        assert(stage.RequiresActionSurfaceFeatureMask() && stage.RequiresSurfaceFeatureMask());
        stage.ClearActionPostFrame(43); // a different World cannot clear this frame
        assert(stage.RequiresActionSurfaceFeatureMask());
        output = stage.ResolveHdr(source, quad, false, "contract", true, nullptr, &maskView);
        assert(output && output != &source);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            output->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        gpu.commands->ResourceBarrier(1u, &barrier);
        from = CD3DX12_TEXTURE_COPY_LOCATION(output->GetResource(), 0u);
        to = CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(), footprint);
        gpu.commands->CopyTextureRegion(&to, 0u, 0u, 0u, &from, nullptr);
        gpu.SubmitAndWait(2u);
        retirement.Collect(2u);
        assert(SUCCEEDED(readback->Map(0u, nullptr, &mapped)));
        pixels = static_cast<const uint16_t*>(mapped);
        for (uint32_t pixel = 0; pixel < 4; ++pixel) {
            assert(DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel * 4]) == (pixel == 0 ? 4.f : 0.f));
            assert(DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel * 4 + 1]) == (pixel == 0 ? 0.f : 4.f));
            assert(DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel * 4 + 2]) == 17.f);
        }
        readback->Unmap(0u, nullptr);
        stage.ClearGlobalEffects();
        assert(stage.RequiresActionSurfaceFeatureMask() && !stage.RequiresSurfaceFeatureMask());
        assert(stage.ResolveHdr(source, quad, false, "contract", true, nullptr) == &source);
        assert(!stage.RequiresActionSurfaceFeatureMask()); // missing mask clears only Action
        assert(stage.SetActionPostFrame(42, frame, "action", definition, actionValues, registry));
        stage.SetActionPostView(43, true);
        assert(!stage.RequiresActionSurfaceFeatureMask());
        assert(stage.ResolveHdr(source, quad, false, "contract", true, nullptr, &maskView) == &source);
        assert(stage.SetActionPostFrame(42, frame, "action", definition, actionValues, registry));
        stage.SetActionPostView(42, false);
        assert(!stage.RequiresActionSurfaceFeatureMask());
        assert(stage.ResolveHdr(source, quad, false, "contract", true, nullptr, &maskView) == &source);
        assert(stage.SetActionPostFrame(42, frame, "action", definition, actionValues, registry));
        stage.SetActionPostView(42, true);
        TIME::BeginFrame();
        assert(!stage.RequiresActionSurfaceFeatureMask());
        stage.ClearActionPostFrame(42);
        stage.Shutdown();

        // 本番の Pixel を使い、線が解決前だけに入り、手前の面の内側に付くことを GPU で読む。
        for (const int renderHeight : {1080, 540}) {
            const uint64_t inkFence = renderHeight == 1080 ? 3u : 4u;
            gpu.ResetCommands();
            context.currentFrameRetireFenceValue = inkFence;
            GFX::PublishCurrentContext(context);
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(context);
            POST::PostEffect::UpdateContext(context);
            RenderTarget2D depthSource;
            depthSource.UpdateContext(context);
            assert(depthSource.Init(4, renderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true));
            depthSource.BeginCapture(2, 2, 2, .5f);
            const D3D12_RECT nearRect{0, 0, 2, renderHeight}, farRect{2, 0, 4, renderHeight};
            gpu.commands->ClearDepthStencilView(depthSource.GetDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, .2f, 0, 1,
                                                &nearRect);
            gpu.commands->ClearDepthStencilView(depthSource.GetDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, .9f, 0, 1,
                                                &farRect);
            depthSource.EndCapture();
            auto pixelDefinition = std::make_shared<POST::PostEffectDefinition>();
            std::string diagnostic;
            assert(POST::LoadPostEffectDefinition(POST::ResolveBuiltinPostEffectPath("engine.post.effect.pixel"),
                                                  *pixelDefinition, diagnostic));
            assert(pixelDefinition->passes.size() == 2u &&
                   pixelDefinition->passes[0].phase == POST::PostEffectPhase::PreTemporal &&
                   pixelDefinition->passes[1].phase == POST::PostEffectPhase::Display);
            auto pixelValues = POST::ResolvePostParameters(*pixelDefinition, nullptr);
            pixelValues[0].y = 1;
            pixelValues[1].x = 1;
            pixelValues[2] = {.5f, .5f, .5f, 0};
            POST::PostProcessingStage inkStage;
            inkStage.UpdateContext(context);
            inkStage.SetBloomSettings(bloom);
            auto exposure = inkStage.GetExposureState();
            exposure.exposure = 1;
            exposure.preExposure = 2;
            inkStage.SetExposureFrame(exposure);
            const POST::ResolvedPostLayer inkLayer{"pixel", pixelDefinition, pixelValues};
            assert(inkStage.SetGlobalEffects({&inkLayer, 1u}, &registry));
            assert(!inkStage.SetActionPostFrame(42, TIME::GetFrameContext().frameIndex, "pixel", pixelDefinition,
                                                pixelValues, registry));
            output = inkStage.ResolvePreTemporal(depthSource, quad, depthSource);
            assert(output && output != &depthSource);
            assert(inkStage.ResolveHdr(*output, quad, false, "contract", true, &depthSource) == output);
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                output->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
            gpu.commands->ResourceBarrier(1u, &barrier);
            from = CD3DX12_TEXTURE_COPY_LOCATION(output->GetResource(), 0u);
            to = CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(), footprint);
            const D3D12_BOX firstRow{0, 0, 0, 4, 1, 1};
            gpu.commands->CopyTextureRegion(&to, 0u, 0u, 0u, &from, &firstRow);
            // 解決前後の解像度が違っても、次のフレームに解決前の描画先を作り直さない。
            ComPtr<ID3D12Resource> retainedInk = output->GetResource();
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(output->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            gpu.commands->ResourceBarrier(1u, &barrier);
            auto* displayOutput = inkStage.ResolveLdr(source, DXGI_FORMAT_R8G8B8A8_UNORM, quad, nullptr, nullptr);
            assert(displayOutput && displayOutput->GetHeight() == 1);
            auto* nextInk = inkStage.ResolvePreTemporal(depthSource, quad, depthSource);
            assert(nextInk && nextInk->GetResource() == retainedInk.Get());
            gpu.SubmitAndWait(inkFence);
            retirement.Collect(inkFence);
            assert(SUCCEEDED(readback->Map(0u, nullptr, &mapped)));
            pixels = static_cast<const uint16_t*>(mapped);
            for (uint32_t pixel = 0; pixel < 4; ++pixel) {
                const float coverage = static_cast<float>(renderHeight) / 1080.f;
                const float expected = pixel == 1 ? 2.f + (std::pow(.5f, 2.2f) * 2.f - 2.f) * coverage : 2.f;
                assert(std::abs(DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel * 4]) - expected) < .002f);
                assert(DirectX::PackedVector::XMConvertHalfToFloat(pixels[pixel * 4 + 3]) == .5f);
            }
            readback->Unmap(0u, nullptr);
            inkStage.Shutdown();
            depthSource.Finalize();
        }
        std::puts("Post Geometry Ink GPU: pre-temporal routing, fractional coverage, HDR color and alpha passed.");
        source.Finalize();
        quad.Finalize();
    }
    RENDER3D::RetireRenderResourceDescriptor(maskDescriptor, context, "PostMaskContract");
    retirement.Collect(4u);
    RENDER3D::ShutdownRenderResourceDescriptorPool();
    POST::PostEffect::UpdateContext({});
    GFX::ClearCurrentContext();
}
} // namespace

void CheckProjectPostContracts(const std::filesystem::path& testRoot) {
    using namespace HIKARI;
    namespace SHADER = ASSETS::SHADER;
    namespace fs = std::filesystem;
    CheckActionPostSubmission();
    const auto root = testRoot / "ProjectPost";
    fs::create_directories(root / "Assets/Shaders/Post");
    fs::copy_file(fs::path(HIKARI_CONTRACT_SOURCE_ROOT) / "Tests/Fixtures/Projects/EmptyProject/Project.hikari",
                  root / "Project.hikari");
    const auto contract = SHADER::MakePostEffectShaderContract({60u, 10u, 0u});
    SHADER::ShaderSourceManifest manifest;
    manifest.programId = "Contract.ProjectPost";
    manifest.sourcePath = "Post/Classifier.hlsl";
    manifest.entries = {{"Main", SHADER::ShaderStage::Pixel, "main", "ps_6_0"}};
    manifest.includeRoots = {SHADER::ShaderIncludeRoot::Project, SHADER::ShaderIncludeRoot::EngineSdk};
    manifest.engineInterfaceId = contract.engineInterfaceId;
    manifest.engineInterfaceVersion = contract.engineInterfaceVersion;
    manifest.engineInterfaceHash = contract.engineInterfaceHash;
    manifest.rootLayoutId = contract.rootLayoutId;
    manifest.rootLayoutVersion = contract.rootLayoutVersion;
    manifest.rootLayoutHash = contract.rootLayoutHash;
    std::string text, error;
    assert(SHADER::SerializeCanonicalShaderSourceManifest(manifest, text, error));
    std::ofstream(root / "Assets/Shaders/Post/Classifier.hshader") << text;
    std::ofstream(root / "Assets/Shaders/Post/Classification.hlsli") << "static const uint kGroups = 1u;\n";
    std::ofstream(root / "Assets/Shaders/Post/Classifier.hlsl") << R"hlsl(
#include "Include/Post/HIKARI_PostEffectInterface.hlsli"
#include "Include/Post/HIKARI_PostSurfaceInputs.hlsli"
#include "Post/Classification.hlsli"
Texture2D<float4> sceneColor : register(t0);
SamplerState linearClamp : register(s0);
float4 main(HikariPostEffectInput input) : SV_TARGET {
    float3 color = sceneColor.Sample(linearClamp, input.uv).rgb;
    float3 classification = HikariPostHasGroup(input.uv, kGroups) ? float3(1,0,0) : float3(0,1,0);
    return float4(color * (1 + gUser[0].x) + classification * gIntensity + float3(0, 0, gUser[1].z), 1);
}
)hlsl";
    auto& content = CONTENT::GetEngineContentContext();
    assert(content.Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    AssetDatabase database;
    assert(database.Initialize(root) && database.ScanAssets(true));
    const auto shaderGuid = database.FindByPath("Assets/Shaders/Post/Classifier.hshader")->guid;
    if (!database.ImportAsset(shaderGuid)) {
        std::fprintf(stderr, "Project Post shader import failed: %s\n",
                     database.FindByGuid(shaderGuid)->lastImportMessage.c_str());
        assert(false);
    }
    nlohmann::json definition{{"formatVersion", 1},
                              {"name", "Project Classification"},
                              {"inputs", {"SurfaceFeatureMask"}},
                              {"passes", {{{"name", "Classification"}, {"shaderAssetGuid", shaderGuid.value}}}}};
    assert(SERIALIZATION::JSON::WriteJsonFile(root / "Assets/Classifier.hposteffect", definition));
    assert(database.ScanAssets(true));
    const auto effectGuid = database.FindByPath("Assets/Classifier.hposteffect")->guid;
    assert(database.ImportAsset(effectGuid));
    const auto& effectDependencies = database.FindByGuid(effectGuid)->artifactManifest.dependencies;
    assert(effectDependencies.size() == 1u && effectDependencies.front().guid == shaderGuid);
    POST::PostProfileAsset profile;
    profile.effectId = effectGuid.value;
    assert(POST::SavePostProfileAsset(root / "Assets/Classifier.hpostprofile", profile, error));
    assert(database.ScanAssets(true));
    const auto profileGuid = database.FindByPath("Assets/Classifier.hpostprofile")->guid;
    assert(database.ImportAsset(profileGuid));
    assert(database.FindByGuid(profileGuid)->artifactManifest.dependencies.front().guid == effectGuid);
    POST::PostEffectDefinition effect;
    assert(POST::ReadPostEffectDefinition(definition, effect, error));
    auto duplicated = definition;
    duplicated["passes"].push_back(definition["passes"][0]);
    assert(POST::ReadPostEffectDefinition(duplicated, effect, error));
    assert(POST::CollectPostEffectDependencies(effect).size() == 1u);
    // root の既定段階を継ぎつつ、同じ効果の pass を前後へ分けられる。
    auto staged = duplicated;
    staged["phase"] = "Display";
    staged["passes"][0]["phase"] = "PreTemporal";
    assert(POST::ReadPostEffectDefinition(staged, effect, error));
    assert(effect.passes[0].phase == POST::PostEffectPhase::PreTemporal &&
           effect.passes[1].phase == POST::PostEffectPhase::Display);
    for (const char* invalid :
         {"ambiguous", "badGuid", "missing", "unknownInput", "duplicateInput", "badPhase", "badPassPhase"}) {
        auto broken = definition;
        if (std::string_view(invalid) == "ambiguous")
            broken["passes"][0]["programId"] = "Builtin";
        if (std::string_view(invalid) == "badGuid")
            broken["passes"][0]["shaderAssetGuid"] = "not-a-guid";
        if (std::string_view(invalid) == "missing")
            broken["passes"][0].erase("shaderAssetGuid");
        if (std::string_view(invalid) == "unknownInput")
            broken["inputs"] = {"Unknown"};
        if (std::string_view(invalid) == "duplicateInput")
            broken["inputs"].push_back("SurfaceFeatureMask");
        if (std::string_view(invalid) == "badPhase")
            broken["phase"] = "Tonemapped";
        if (std::string_view(invalid) == "badPassPhase")
            broken["passes"][0]["phase"] = "Unknown";
        const auto originalName = effect.name;
        assert(!POST::ReadPostEffectDefinition(broken, effect, error) && effect.name == originalName);
    }
    // 全体後処理はシーンが参照する描画の様式の層が選ぶ。シーン → 様式 → 後処理 → 効果 → shader と辿れること。
    RENDERSTYLE::RenderStyleAsset style;
    style.post = {{profileGuid.value, true}};
    assert(RENDERSTYLE::SaveRenderStyleAsset(root / "Assets/Classifier.hrenderstyle", style, error));
    assert(database.ScanAssets(true));
    const auto styleGuid = database.FindByPath("Assets/Classifier.hrenderstyle")->guid;
    assert(database.ImportAsset(styleGuid));
    assert(database.FindByGuid(styleGuid)->artifactManifest.dependencies.front().guid == profileGuid);
    SceneDocument scene;
    scene.sceneName = "ProjectPost";
    scene.environment.renderStyleId = styleGuid.value;
    const auto usage = AnalyzeAssetUsage(scene, database);
    assert(usage.missingReferences.empty() && usage.IsUsed(styleGuid) && usage.IsUsed(profileGuid) &&
           usage.IsUsed(effectGuid) && usage.IsUsed(shaderGuid));
    assert(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(root / "Assets/Scene.hscene"), scene));
    // Runtime-only Action profiles must close through declared roots, independently of Global Post.
    scene.environment = {};
    assert(SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(root / "Assets/Scene.hscene"), scene));
    assert(database.ScanAssets(true));
    const auto sceneGuid = database.FindByPath("Assets/Scene.hscene")->guid;
    assert(database.ImportAsset(sceneGuid));
    ASSETS::COOKING::AssetCookOptions options;
    options.projectRoot = root;
    options.contentRoot = testRoot / "ProjectPostCooked";
    options.sceneGuids = {sceneGuid.value};
    options.startupSceneGuid = sceneGuid.value;
    options.assetRoots = {"Assets/Classifier.hpostprofile"};
    auto cooked = ASSETS::COOKING::AssetCooker{}.Cook(options);
    if (!cooked.success)
        std::fprintf(stderr, "%s\n", cooked.message.c_str());
    assert(cooked.success);
    AssetDatabase published;
    assert(published.Initialize(options.contentRoot) && published.IsCookedContent() && published.ScanAssets(false));
    AssetRegistry registry;
    assert(AssetRegistryBuilder{}.RebuildRegistry(published, registry));
    POST::PostAssetStore store;
    store.Configure(&published);
    const auto publishedProfile = store.LoadProfile(profileGuid.value, error);
    if (!publishedProfile)
        std::fprintf(stderr, "Cooked Post profile load failed: %s\n", error.c_str());
    assert(publishedProfile && publishedProfile->effectId == effectGuid.value);
    const auto publishedEffect = store.LoadEffect(effectGuid.value, error);
    if (!publishedEffect)
        std::fprintf(stderr, "Cooked Post effect load failed: %s\n", error.c_str());
    assert(publishedEffect && publishedEffect->requiresSurfaceFeatureMask);
    TESTS::INTERNAL::GpuContractDevice gpu;
    GFX::Context context{};
    context.device = gpu.device.Get();
    context.deviceGeneration = 1u;
    POST::PostEffect::UpdateContext(context);
    POST::PostEffect pass;
    assert(!pass.LoadPixelShader(publishedEffect->passes[0], &registry, false));
    assert(pass.LoadPixelShader(publishedEffect->passes[0], &registry, true));
    CheckPostSurfaceDraw(gpu, publishedEffect, registry);
    POST::PostEffect::UpdateContext(context);
    POST::PostProcessingStage stage;
    stage.UpdateContext(context);
    const POST::ResolvedPostLayer publishedLayer{effectGuid.value, publishedEffect, {}};
    assert(stage.SetGlobalEffects({&publishedLayer, 1u}, &registry));
    assert(stage.RequiresSurfaceFeatureMask());
    assert(stage.SetActionPostFrame(42, TIME::GetFrameContext().frameIndex, effectGuid.value, publishedEffect, {},
                                    registry));
    stage.SetActionPostView(42, true);
    assert(stage.RequiresActionSurfaceFeatureMask());
    registry.Clear();
    assert(!stage.RequiresActionSurfaceFeatureMask());
    assert(!stage.SetActionPostFrame(42, TIME::GetFrameContext().frameIndex, effectGuid.value, publishedEffect, {},
                                     registry));
    assert(!stage.SetGlobalEffects({&publishedLayer, 1u}, &registry));
    assert(!stage.RequiresSurfaceFeatureMask());
    stage.Shutdown();
    POST::PostEffect::UpdateContext({});
    content.Close();
    std::puts(
        "Project Post: strict schema, shader import, dependency closure, cooked runtime load and stale registry rejection passed.");
}
