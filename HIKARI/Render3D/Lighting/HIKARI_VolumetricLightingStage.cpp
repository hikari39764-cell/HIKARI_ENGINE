#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Assets/Shader/HIKARI_VolumetricShaderContract.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Water/HIKARI_WaterVisibleDepth.h"
#include "HIKARI/Project/LocalFogTypes.h"

namespace HIKARI::RENDER3D::VOLUMETRIC {

namespace {
using Microsoft::WRL::ComPtr;
constexpr DXGI_FORMAT kVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct alignas(16) VolumetricConstants {
    MATH::Mat4 invViewProj{};
    MATH::Mat4 prevViewProj{};
    MATH::Mat4 lightViewProj[SHADOW::kMaxDirectionalShadowCascades]{};
    MATH::Vec4 shadowCascadeSplits{};
    MATH::Vec4 shadowCascadeParams{};
    MATH::Vec4 shadowSampling{};
    MATH::Vec4 shadowCascadeDepthBiasScales{};
    MATH::Vec4 cameraPos{};
    MATH::Vec4 prevCameraPos{};
    MATH::Vec4 fogColorDensity{};
    MATH::Vec4 fogDistances{};
    MATH::Vec4 directionalDirIntensity{};
    MATH::Vec4 directionalColorShadow{};
    MATH::Vec4 ambientColorIntensity{};
    MATH::Vec4 volumetricOptics{};
    MATH::Vec4 volumeSize{};
    MATH::Vec4 frameParams{};
    MATH::Vec4 debugAndLocalLightDepth{};
    // 空気遠近。x 体積の最大距離 [m]、y 1 なら空気遠近を掛ける、z 1 なら霧の体積を掛ける。
    MATH::Vec4 aerialPerspective{};
    // xy froxel 内の横の標本位置 [-0.5, 0.5]、z 層内の奥行きの標本位置 [0, 1)、w 1 なら環境光は動的 GI の体積から読む。
    MATH::Vec4 fogJitter{0.0f, 0.0f, 0.5f, 0.0f};
    MATH::Vec4 ambientVolumeSize{};
    // 高さ霧の密度の揺らぎ。x 強さ、y 1/大きさ [1/m]、zw 未使用。offset は二つのノイズの風の移動（ノイズ単位、周期で折る）。
    MATH::Vec4 fogNoise{};
    MATH::Vec4 fogNoiseOffset{};
    LIGHTPROBE::IrradianceVolumeGpuData giVolumes[LIGHTPROBE::kDynamicGiVolumeCount]{};
    uint32_t localLightCount = 0u;
    uint32_t localLightClusterCountX = 0u;
    uint32_t localLightClusterCountY = 0u;
    uint32_t localLightClusterCountZ = 0u;
    uint32_t localLightTileSizePixels = 64u;
    uint32_t localLightClusterIndexCount = 0u;
    float localLightNearDistance = 0.1f;
    float localLightFarDistance = 100.0f;
    LIGHTING::LocalShadowGpuData localShadow{};
    LocalFogVolumeGpu localFogVolumes[kGameProjectLocalFogMaximumVolumes]{};
};

static_assert(sizeof(VolumetricConstants) == 6624u);

struct VolumeResource {
    ComPtr<ID3D12Resource> texture{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
};

// 注入側（GatherAmbientCS / InjectCS / IntegrateCS）の root の引数。HIKARI_VolumetricShaderContract.h の Fog と同じ並び。
enum InjectRoot : UINT {
    kInjectConstants,
    kInjectSceneDepth,
    kInjectShadowMap,
    kInjectHistory,
    kInjectOutput,
    kInjectLights,
    kInjectLightClusterHeaders,
    kInjectLightClusterIndices,
    kInjectLocalShadow,
    kInjectAmbient,
    kInjectInjected,
    kInjectTexturePool,
    kInjectRootCount
};
// 合成側（CompositeCS）の root の引数。
enum CompositeRoot : UINT {
    kCompositeConstants,
    kCompositeDepth,
    kCompositeIntegrated,
    kCompositeWaterDepth,
    kCompositeSceneColor,
    kCompositeAerialPerspective,
    kCompositeRootCount
};
// 環境光の格子は froxel の横 4x4 と奥行き 2 層ごとに一点。probe 間隔より十分細かい。
constexpr uint32_t kAmbientCellFroxels = 4u;
// 高さ霧の揺らぎのノイズの周期（ノイズ単位）と細かい方の周波数の倍率（Render3D_VolumetricLighting.hlsl と同じ）。
constexpr uint32_t kFogNoisePeriod = 256u;
constexpr double kFogNoiseDetailFrequency = 2.37;
// 揺らぎが流れる速さの地上の風に対する割合。風と同じ速さでは晴天（3 m/s）でも 5 m の濃淡が 1 秒足らずで光柱を
// 横切り、屋根の穴から差す強い光柱で部屋全体が明滅した。霧は物陰や屋内で風が弱まるので、ゆっくり漂う程度にする。
constexpr double kFogNoiseWindScale = 0.2;

struct StageState {
    GFX::Context context{};
    // このビューで霧の体積を作るか。無ければ合成は空気遠近だけを掛ける。
    bool fogInjected = false;
    // 霧の環境光を動的 GI の probe から集めるか。
    bool giAmbient = false;
    D3D12_GPU_DESCRIPTOR_HANDLE aerialPerspectiveSrv{};
    ComPtr<ID3D12RootSignature> computeRoot{};
    ComPtr<ID3D12RootSignature> compositeRoot{};
    ComPtr<ID3D12PipelineState> gatherPso{};
    ComPtr<ID3D12PipelineState> injectPso{};
    ComPtr<ID3D12PipelineState> integratePso{};
    ComPtr<ID3D12PipelineState> compositePso{};
    UINT descriptorStride = 0;
    uint32_t ambientWidth = 0, ambientHeight = 0, ambientDepth = 0;
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads;
    D3D12_GPU_VIRTUAL_ADDRESS constantAddress = 0;
    // 注入（今と前フレームを交互に書く）、積分、環境光。
    VolumeResource injected[2]{};
    VolumeResource integrated{};
    VolumeResource ambient{};
    RenderResourceView sceneColorUav{};
    RenderResourceView sceneDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE waterDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE localShadowSrv{};
    WATER::WaterVisibleDepthView waterDepthView{};
    ID3D12Resource* sceneColorResource = nullptr;
    ID3D12Resource* sceneDepthResource = nullptr;
    VolumetricLightingStats stats{};
    uint32_t writeIndex = 0;
    uint64_t preparedFrameIndex = 0;
    uint64_t executedFrameIndex = (std::numeric_limits<uint64_t>::max)();
    ID3D12Resource* executedSceneColorResource = nullptr;
    float fogNearDistance = 0.0f;
    float fogFarDistance = 0.0f;
    uint64_t historyFrameIndex = 0;
    TEMPORAL::TemporalViewLease preparedView{}, historyView{};
    RenderDebugView debugView = RenderDebugView::None;
    LIGHTING::LocalLightListGpuBindings localLights{};
    bool pipelineReady = false;
    bool resourcesReady = false;
    bool prepared = false;
    bool volumeHistoryValid = false;
    uint64_t lastSettingsSignature = 0u;
};

StageState& State() {
    static StageState state{};
    return state;
}

// froxel の大きさと奥行き。霧は無限に続くので、体積で扱う距離 [m] も画質で決める（その先は空と大気が受け持つ）。
struct QualityPolicy {
    uint32_t froxelPixelSize = 16;
    uint32_t sliceCount = 64;
    float farDistance = 160.0f;
};

QualityPolicy ResolveQualityPolicy(VolumetricLightingQuality quality) {
    switch (quality) {
    case VolumetricLightingQuality::Low: return {24u, 48u, 96.0f};
    case VolumetricLightingQuality::High: return {12u, 80u, 256.0f};
    case VolumetricLightingQuality::Balanced:
    default: return {16u, 64u, 160.0f};
    }
}

uint32_t ResolveDebugMode(RenderDebugView view) {
    switch (view) {
    case RenderDebugView::VolumetricScattering: return 1u;
    case RenderDebugView::VolumetricTransmittance: return 2u;
    case RenderDebugView::VolumetricDepthSlice: return 3u;
    default: return 0u;
    }
}

// 履歴を捨てるべき不連続な変化だけを数える。太陽・天気・霧の濃さなど連続に変わる値は含めない。
uint64_t HashVolumetricSettings(const SceneEnvironment& environment, VolumetricLightingQuality quality,
                                uint64_t localLightingContentHash) noexcept {
    uint64_t hash = HASH::kFnv1a64OffsetBasis;
    const auto Add = [&hash](const auto& value) { hash = HASH::Fnv1aBytes(hash, &value, sizeof(value)); };
    Add(localLightingContentHash);
    Add(environment.fog.enabled);
    Add(environment.fog.volumetric);
    Add(environment.shadows.enabled);
    Add(RENDER3D::GetRenderQualitySettings().directionalShadow.resolution);
    Add(RENDER3D::GetRenderQualitySettings().directionalShadow.cascadeCount);
    Add(environment.clouds.enabled);
    Add(quality);
    return hash == 0u ? 1u : hash;
}

void ReleaseView(RenderResourceView& view) {
    if (view.IsValid()) {
        (void)ReleaseRenderResourceDescriptor(view);
        view = {};
    }
}

void RetireView(StageState& state, RenderResourceView& view, const char* debugName) {
    if (!view.IsValid())
        return;
    const RenderResourceView retired = view;
    view = {};
    if (state.context.deferredReleaseQueue != nullptr && state.context.currentFrameRetireFenceValue != 0) {
        state.context.deferredReleaseQueue->Enqueue(
            state.context.currentFrameRetireFenceValue, [retired]() { (void)ReleaseRenderResourceDescriptor(retired); },
            debugName != nullptr ? debugName : "VolumetricLighting.Descriptor");
        return;
    }
    (void)ReleaseRenderResourceDescriptor(retired);
}

void ReleaseVolume(VolumeResource& volume) {
    ReleaseView(volume.srv);
    ReleaseView(volume.uav);
    volume.texture.Reset();
    volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
}

void RetireVolume(StageState& state, VolumeResource& volume) {
    RetireView(state, volume.srv, "VolumetricLighting.VolumeSRV");
    RetireView(state, volume.uav, "VolumetricLighting.VolumeUAV");
    GFX::RetireD3D12ObjectForFrame(volume.texture, state.context, "VolumetricLighting.Volume");
    volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
}

bool CreateRootSignature(ID3D12Device* device, bool injection, ID3D12RootSignature** output) {
    std::array<CD3DX12_DESCRIPTOR_RANGE, kInjectRootCount> ranges{};
    std::array<CD3DX12_ROOT_PARAMETER, kInjectRootCount> params{};
    const auto table = [&](UINT index, D3D12_DESCRIPTOR_RANGE_TYPE type, UINT shaderRegister, UINT count = 1u) {
        ranges[index].Init(type, count, shaderRegister);
        params[index].InitAsDescriptorTable(1, &ranges[index]);
    };
    UINT parameterCount = 0;
    if (injection) {
        params[kInjectConstants].InitAsConstantBufferView(0);
        table(kInjectSceneDepth, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0);
        table(kInjectShadowMap, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1);
        table(kInjectHistory, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
        table(kInjectOutput, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0);
        params[kInjectLights].InitAsShaderResourceView(0, 5);
        params[kInjectLightClusterHeaders].InitAsShaderResourceView(1, 5);
        params[kInjectLightClusterIndices].InitAsShaderResourceView(2, 5);
        table(kInjectLocalShadow, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7);
        table(kInjectAmbient, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3);
        table(kInjectInjected, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10);
        table(kInjectTexturePool, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 20, GFX::DESCRIPTOR::kUserSrvCount);
        parameterCount = kInjectRootCount;
    } else {
        params[kCompositeConstants].InitAsConstantBufferView(0);
        table(kCompositeDepth, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
        table(kCompositeIntegrated, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5);
        table(kCompositeWaterDepth, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6);
        table(kCompositeSceneColor, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1);
        table(kCompositeAerialPerspective, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 9);
        parameterCount = kCompositeRootCount;
    }
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers{};
    samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                     D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    samplers[1].Init(1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                     D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 16,
                     D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
    const CD3DX12_ROOT_SIGNATURE_DESC desc(parameterCount, params.data(), injection ? 2u : 1u, samplers.data());
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, desc, output), "VolumetricLighting::CreateRootSignature");
}

bool EnsurePipeline(StageState& state) {
    if (state.pipelineReady)
        return true;
    ID3D12Device* device = state.context.device;
    if (!device)
        return false;
    if (!CreateRootSignature(device, true, state.computeRoot.GetAddressOf()) ||
        !CreateRootSignature(device, false, state.compositeRoot.GetAddressOf())) {
        return false;
    }
    namespace SHADER = ASSETS::SHADER;
    const auto deviceCaps = GFX::QueryShaderDeviceCapabilities(device);
    const SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                       deviceCaps.featureMask};
    const auto createPso = [&](const char* entry, ID3D12RootSignature* root, ComPtr<ID3D12PipelineState>& pso) {
        const auto shader = SHADER::ResolveVolumetricShaderEntry(SHADER::VolumetricShaderProgram::Fog, entry,
                                                                 SHADER::ShaderStage::Compute, caps);
        if (!shader) {
            DEBUGLOG::PushRenderError(std::string("Volumetric fog: cooked shader unavailable or incompatible: ") +
                                      entry + ": " + shader.diagnostic);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root;
        desc.CS = {shader.lease.GetDxil().data(), shader.lease.GetDxil().size()};
        return HIKARI_DX_CHECK(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())),
                               "VolumetricLighting::CreateComputePSO");
    };
    if (!createPso("GatherAmbientCS", state.computeRoot.Get(), state.gatherPso) ||
        !createPso("InjectCS", state.computeRoot.Get(), state.injectPso) ||
        !createPso("IntegrateCS", state.computeRoot.Get(), state.integratePso) ||
        !createPso("CompositeCS", state.compositeRoot.Get(), state.compositePso))
        return false;
    state.descriptorStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    state.pipelineReady = true;
    return true;
}

bool CreateVolume(StageState& state, VolumeResource& volume, uint32_t width, uint32_t height, uint32_t depth) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = 1;
    desc.Format = kVolumeFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (!HIKARI_DX_CHECK(state.context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                       IID_PPV_ARGS(volume.texture.GetAddressOf())),
                         "VolumetricLighting::CreateVolume"))
        return false;
    volume.srv = AllocateTexture3DSrvDescriptor(volume.texture.Get(), kVolumeFormat);
    volume.uav = AllocateTexture3DUavDescriptor(volume.texture.Get(), kVolumeFormat, 0, 0, depth);
    volume.state = D3D12_RESOURCE_STATE_GENERIC_READ;
    return volume.srv.IsValid() && volume.uav.IsValid();
}

bool EnsureResources(StageState& state, uint32_t renderWidth, uint32_t renderHeight, uint32_t pixelSize,
                     uint32_t sliceCount) {
    const uint32_t width = (renderWidth + pixelSize - 1u) / pixelSize;
    const uint32_t height = (renderHeight + pixelSize - 1u) / pixelSize;
    if (state.resourcesReady && state.stats.renderWidth == renderWidth && state.stats.renderHeight == renderHeight &&
        state.stats.froxelWidth == width && state.stats.froxelHeight == height && state.stats.froxelDepth == sliceCount)
        return true;

    const uint32_t ambientWidth = (width + kAmbientCellFroxels - 1u) / kAmbientCellFroxels;
    const uint32_t ambientHeight = (height + kAmbientCellFroxels - 1u) / kAmbientCellFroxels;
    const uint32_t ambientDepth = (std::max)(sliceCount / 2u, 2u);
    for (VolumeResource& volume : state.injected)
        RetireVolume(state, volume);
    RetireVolume(state, state.integrated);
    RetireVolume(state, state.ambient);
    if (!CreateVolume(state, state.injected[0], width, height, sliceCount) ||
        !CreateVolume(state, state.injected[1], width, height, sliceCount) ||
        !CreateVolume(state, state.integrated, width, height, sliceCount) ||
        !CreateVolume(state, state.ambient, ambientWidth, ambientHeight, ambientDepth)) {
        state.resourcesReady = false;
        return false;
    }
    state.ambientWidth = ambientWidth;
    state.ambientHeight = ambientHeight;
    state.ambientDepth = ambientDepth;
    state.stats.renderWidth = renderWidth;
    state.stats.renderHeight = renderHeight;
    state.stats.froxelWidth = width;
    state.stats.froxelHeight = height;
    state.stats.froxelDepth = sliceCount;
    state.stats.froxelPixelSize = pixelSize;
    state.stats.workingSetBytes = static_cast<uint64_t>(width) * height * sliceCount * 8u * 3u +
                                  static_cast<uint64_t>(ambientWidth) * ambientHeight * ambientDepth * 8u;
    ++state.stats.resizeCount;
    ++state.stats.historyResetCount;
    state.volumeHistoryValid = false;
    state.resourcesReady = true;
    return true;
}

bool EnsureSceneViews(StageState& state, const RenderTextureView& color, const RenderTextureView& depth) {
    if (!color.valid || !color.resource || !depth.IsSampleable() || !depth.resource || color.width != depth.width ||
        color.height != depth.height)
        return false;
    const auto colorDesc = color.resource->GetDesc(), depthDesc = depth.resource->GetDesc();
    if (colorDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || colorDesc.Width != color.width ||
        colorDesc.Height != color.height || depthDesc.Width != depth.width || depthDesc.Height != depth.height ||
        colorDesc.SampleDesc.Count != 1 || depthDesc.SampleDesc.Count != 1 ||
        (colorDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        return false;
    if (state.sceneColorResource == color.resource && state.sceneDepthResource == depth.resource &&
        state.sceneColorUav.IsValid() && state.sceneDepthSrv.IsValid())
        return true;
    RetireView(state, state.sceneColorUav, "VolumetricLighting.SceneColorUAV");
    RetireView(state, state.sceneDepthSrv, "VolumetricLighting.SceneDepthSRV");
    state.sceneColorResource = color.resource;
    state.sceneDepthResource = depth.resource;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{color.format};
    if (FAILED(state.context.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
        (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) == 0 ||
        (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0)
        return false;
    state.sceneColorUav = AllocateTexture2DUavDescriptor(state.sceneColorResource, color.format);
    state.sceneDepthSrv = AllocateTexture2DSrvDescriptor(state.sceneDepthResource, DXGI_FORMAT_R32_FLOAT);
    return state.sceneColorUav.IsValid() && state.sceneDepthSrv.IsValid();
}

void TransitionVolume(ID3D12GraphicsCommandList* cmd, VolumeResource& volume, D3D12_RESOURCE_STATES next) {
    if (volume.state == next)
        return;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(volume.texture.Get(), volume.state, next);
    cmd->ResourceBarrier(1, &barrier);
    volume.state = next;
}
} // namespace

bool PrepareVolumetricLightingFrame(const GFX::Context& context, const TEMPORAL::TemporalFrameState& frame,
                                    const RenderTextureView& sceneColor, const RenderTextureView& sceneDepth,
                                    const SceneEnvironment& environment,
                                    const LIGHTING::LocalLightListGpuBindings& localLights,
                                    VolumetricLightingQuality quality, RenderDebugView debugView,
                                    const SHADOW::LocalLightShadowFrame* localShadows,
                                    std::span<const LocalFogVolumeGpu> localFogVolumes) {
    StageState& state = State();
    const bool globalFog = environment.fog.enabled && (environment.fog.volumetric || !localFogVolumes.empty()) &&
                           std::isfinite(environment.fog.density) && environment.fog.density > 0.0f;
    // 霧が無くても、大気の空なら合成で空気遠近を掛ける。霧の体積は霧がある時だけ作る。
    const bool fogRequested = globalFog || !localFogVolumes.empty();
    const SKYRENDERER::AerialPerspectiveView& aerial = SKYRENDERER::GetAerialPerspectiveView();
    state.stats.requested = fogRequested || aerial.valid;
    state.fogInjected = false;
    state.giAmbient = false;
    state.aerialPerspectiveSrv = {};
    state.stats.active = false;
    state.stats.sceneTargetsReady = false;
    state.stats.localShadowFaces = 0;
    state.prepared = false;
    state.localShadowSrv = {};
    state.constantAddress = 0;
    state.waterDepthView = {};
    state.waterDepthSrv = {};
    state.stats.waterDepthValid = false;
    state.stats.waterDepthFallback = true;
    if (!state.stats.requested || localFogVolumes.size() > kGameProjectLocalFogMaximumVolumes || !frame.camera.valid ||
        !frame.view.IsValid() || !context.deviceGeneration || !context.device || !context.cmdList || !context.srvHeap ||
        sceneColor.width != frame.renderWidth || sceneColor.height != frame.renderHeight) {
        return false;
    }
    if (state.context.device &&
        (state.context.device != context.device || state.context.deviceGeneration != context.deviceGeneration ||
         state.context.srvHeap != context.srvHeap))
        return false;
    state.context = context;
    UpdateRenderResourceDescriptorPoolContext(context);
    const auto descriptorPool = GetRenderResourceDescriptorPoolStats();
    if (!descriptorPool.contextAvailable || !descriptorPool.contextCompatible ||
        !EnsureSceneViews(state, sceneColor, sceneDepth))
        return false;
    state.stats.sceneTargetsReady = true;
    const QualityPolicy policy = ResolveQualityPolicy(quality);
    const uint32_t pixelSize = policy.froxelPixelSize;
    const uint32_t slices = policy.sliceCount;
    if (!EnsurePipeline(state) || !EnsureResources(state, frame.renderWidth, frame.renderHeight, pixelSize, slices)) {
        return false;
    }
    if ((frame.resetHistory || !state.historyView.Matches(frame.view) ||
         state.historyFrameIndex + 1 != frame.frameIndex) &&
        state.volumeHistoryValid) {
        state.volumeHistoryValid = false;
        ++state.stats.historyResetCount;
    }

    VolumetricConstants constants{};
    if (localShadows && localShadows->data.faceCount && IsTextureResourceValid(localShadows->atlas)) {
        state.localShadowSrv = GetTextureResourceSrvGpuHandle(localShadows->atlas);
        if (state.localShadowSrv.ptr)
            constants.localShadow = localShadows->data;
    }
    state.stats.localShadowFaces = constants.localShadow.faceCount;
    // The volume is indexed in the jittered render grid, so both depth
    // reconstruction and history reprojection must use that same grid.
    constants.invViewProj = frame.camera.invViewProj;
    constants.prevViewProj = frame.camera.prevViewProj;
    const auto& cascadeMatrices = SHADOW::GetDirectionalCascadeLightViewProj();
    for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW::kMaxDirectionalShadowCascades; ++cascadeIndex) {
        constants.lightViewProj[cascadeIndex] = cascadeMatrices[cascadeIndex];
    }
    constants.shadowCascadeSplits = SHADOW::GetDirectionalCascadeSplitDistances();
    constants.shadowCascadeDepthBiasScales = SHADOW::GetDirectionalCascadeDepthBiasScales();
    const RENDER3D::DirectionalShadowQualitySettings& shadowQuality =
        RENDER3D::GetRenderQualitySettings().directionalShadow;
    constants.shadowCascadeParams = {static_cast<float>(SHADOW::GetDirectionalCascadeCount()),
                                     std::clamp(shadowQuality.cascadeBlend, 0.0f, 0.3f), 0.5f, 0.5f};
    constants.cameraPos = frame.camera.cameraPos;
    const float shadowAtlasTexel = SHADOW::GetShadowAtlasTexelSize();
    constants.shadowSampling = {(std::max)(shadowQuality.depthBias, 0.0f), shadowAtlasTexel, shadowAtlasTexel, 0.0f};
    constants.prevCameraPos = frame.camera.prevCameraPos;
    // 体積の shader は fogColor を散乱アルベドとして使う。散乱光は太陽と空の光から求める。
    const MATH::Vec3& albedo = environment.fog.albedo;
    constants.fogColorDensity = {albedo.x, albedo.y, albedo.z, globalFog ? environment.fog.density : 0.0f};
    std::copy(localFogVolumes.begin(), localFogVolumes.end(), constants.localFogVolumes);
    // 揺らぎは地上の風の積分した移動（kFogNoiseWindScale 倍）で流す。細かい方は 1.6 倍の速さで流れ、二つの重なりが
    // 形を変える。移動量は倍精度でノイズの周期へ折り、float の精度を保つ。
    if (environment.fog.noise > 0.0f) {
        const double scale = std::clamp(static_cast<double>(environment.fog.noiseScale), 1.0, 500.0);
        const double windX = std::isfinite(environment.frame.clouds.windDisplacementX)
                                 ? environment.frame.clouds.windDisplacementX * kFogNoiseWindScale
                                 : 0.0;
        const double windZ = std::isfinite(environment.frame.clouds.windDisplacementZ)
                                 ? environment.frame.clouds.windDisplacementZ * kFogNoiseWindScale
                                 : 0.0;
        const auto wrap = [](double value) {
            const double period = static_cast<double>(kFogNoisePeriod);
            return static_cast<float>(value - std::floor(value / period) * period);
        };
        constants.fogNoise = {std::clamp(environment.fog.noise, 0.0f, 1.0f), static_cast<float>(1.0 / scale), 0.0f,
                              0.0f};
        constants.fogNoiseOffset = {wrap(-windX / scale), wrap(-windZ / scale),
                                    wrap(-windX * 1.6 * kFogNoiseDetailFrequency / scale),
                                    wrap(-windZ * 1.6 * kFogNoiseDetailFrequency / scale)};
    }
    const float nearDistance = (std::max)(0.01f, environment.fog.startDistance);
    constants.fogDistances = {nearDistance, (std::max)(nearDistance + 1.0f, policy.farDistance),
                              (std::max)(0.0f, environment.fog.heightFalloff),
                              std::clamp(environment.fog.anisotropy, -0.8f, 0.8f)};
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    // 霧に雲の影の模様は付けない（雲の影は場面の面だけが受け、空の画素に影を出さない）。雲を抜ける主光源の割合は
    // 空全体の平均を一様に掛ける。霧を照らす空の光は、雲があれば雲の下から見上げた空（雲を含む）。晴天の空のままだと
    // 曇天や雨で霧が白く光る。
    const bool clouded = CLOUDS::HasVisibleClouds(environment);
    const CLOUDS::CloudShadingEstimate cloudShading =
        clouded ? CLOUDS::EstimateCloudShading(environment) : CLOUDS::CloudShadingEstimate{};
    constants.directionalDirIntensity = {-solar.sunDirection.x, -solar.sunDirection.y, -solar.sunDirection.z,
                                         solar.sunRadiance * cloudShading.sunVisibility};
    constants.directionalColorShadow = {solar.sunColor.x, solar.sunColor.y, solar.sunColor.z, 0.0f};
    const MATH::Vec3 ambient =
        clouded ? cloudShading.ambientRadiance : SKYRENDERER::EstimateSkyAmbientRadiance(environment);
    constants.ambientColorIntensity = {ambient.x, ambient.y, ambient.z, 1.0f};
    constants.volumetricOptics = {
        std::isfinite(environment.fog.baseHeight) ? environment.fog.baseHeight : 0.0f,
        std::clamp(environment.fog.scatteringIntensity, 0.0f, 8.0f),
        std::clamp(environment.fog.ambientScatteringStrength, 0.0f, 8.0f),
        std::clamp(environment.fog.sunShaftStrength, 0.0f, 8.0f),
    };

    uint64_t settingsSignature = HashVolumetricSettings(environment, quality, localLights.view.lightingContentHash);
    // 内容が変わったフレームは以前の媒体を混ぜない。静的編集では履歴を保持する。
    settingsSignature = HASH::Fnv1aBytes(settingsSignature, localFogVolumes.data(), localFogVolumes.size_bytes());
    if (state.lastSettingsSignature != 0u && state.lastSettingsSignature != settingsSignature &&
        state.volumeHistoryValid) {
        state.volumeHistoryValid = false;
        ++state.stats.historyResetCount;
    }
    state.lastSettingsSignature = settingsSignature;
    const bool historyValid = state.volumeHistoryValid && frame.historyValid && !frame.resetHistory;
    constants.volumeSize = {static_cast<float>(state.stats.froxelWidth), static_cast<float>(state.stats.froxelHeight),
                            static_cast<float>(state.stats.froxelDepth), historyValid ? 1.0f : 0.0f};
    const bool shadowed = SHADOW::IsDirectionalShadowEnabled() && environment.shadows.enabled &&
                          SHADOW::GetDirectionalShadowSrv().ptr != 0;
    // 体積はこのビューの P で書き、前フレームの体積は P/P_prev で今の空間へ換算する。
    const MATH::Vec4& viewExposure = RENDER3D::COLOR::GetActiveViewExposure();
    constants.frameParams = {historyValid ? std::clamp(environment.fog.temporalWeight, 0.0f, 0.98f) : 0.0f,
                             viewExposure.w, shadowed ? 1.0f : 0.0f, viewExposure.x};
    // 履歴を混ぜるフレームだけ froxel 内の標本位置を動かし、froxel 全体の平均へ収束させる（奥行きの層の段と影の縁の
    // 階段を消す）。位置は R3 の低食い違い列（shader が froxel ごとの乱数で回す）。履歴の無いフレームは中心で取る。
    if (constants.frameParams.x > 0.0f) {
        const double phase = static_cast<double>(frame.frameIndex % 4096u);
        const auto fraction = [](double value) { return static_cast<float>(value - std::floor(value)); };
        constants.fogJitter = {fraction(0.5 + phase * 0.8191725134) - 0.5f, fraction(0.5 + phase * 0.6710436067) - 0.5f,
                               fraction(0.5 + phase * 0.5497004779), 0.0f};
    }
    // 霧へ届く環境光。動的 GI があれば probe から集め（覆わない所は空の光）、壁や屋根に囲まれた所の霧を空の光で
    // 光らせない。無ければ一様な空の光。
    const auto giVolumes = LIGHTPROBE::GetDynamicGiVolumes();
    for (size_t index = 0; index < giVolumes.size() && index < std::size(constants.giVolumes); ++index) {
        if (giVolumes[index].valid) {
            constants.giVolumes[index] = LIGHTPROBE::BuildIrradianceVolumeGpuData(giVolumes[index], 1.0f);
            state.giAmbient = fogRequested;
        }
    }
    constants.fogJitter.w = state.giAmbient ? 1.0f : 0.0f;
    constants.ambientVolumeSize = {static_cast<float>(state.ambientWidth), static_cast<float>(state.ambientHeight),
                                   static_cast<float>(state.ambientDepth), 0.0f};
    constants.localLightCount = localLights.view.lightCount;
    constants.localLightClusterCountX = localLights.view.clusterCountX;
    constants.localLightClusterCountY = localLights.view.clusterCountY;
    constants.localLightClusterCountZ = localLights.view.clusterCountZ;
    constants.localLightTileSizePixels = localLights.view.tileSizePixels;
    constants.localLightClusterIndexCount = localLights.view.clusterIndexCount;
    constants.localLightNearDistance = localLights.view.nearDistance;
    constants.localLightFarDistance = localLights.view.farDistance;
    constants.debugAndLocalLightDepth = {static_cast<float>(ResolveDebugMode(debugView)), localLights.view.depthScale,
                                         localLights.view.depthBias, static_cast<float>(localFogVolumes.size())};
    constants.aerialPerspective = {aerial.valid ? aerial.maxDistance : 0.0f, aerial.valid ? 1.0f : 0.0f,
                                   fogRequested ? 1.0f : 0.0f, 0.0f};
    state.aerialPerspectiveSrv = aerial.valid ? aerial.srv : D3D12_GPU_DESCRIPTOR_HANDLE{};
    state.fogInjected = fogRequested;
    state.preparedFrameIndex = frame.frameIndex;
    state.executedFrameIndex = (std::numeric_limits<uint64_t>::max)();
    state.executedSceneColorResource = nullptr;
    state.fogNearDistance = constants.fogDistances.x;
    state.fogFarDistance = constants.fogDistances.y;
    state.preparedView = frame.view;
    auto& uploads = state.uploads[context.frameIndex % GFX::kFrameResourceCount];
    if (!uploads.IsInitialized() && !uploads.Init(context.device, sizeof(VolumetricConstants)))
        return false;
    uploads.ResetForFrame(context.currentFrameRetireFenceValue);
    const auto constantUpload = uploads.Allocate(sizeof(constants));
    if (!constantUpload)
        return false;
    std::memcpy(constantUpload.cpuAddress, &constants, sizeof(constants));
    state.constantAddress = constantUpload.gpuAddress;
    state.writeIndex = static_cast<uint32_t>(frame.frameIndex & 1u);
    state.debugView = debugView;
    state.localLights = localLights;
    state.stats.active = true;
    state.stats.historyValid = historyValid;
    state.stats.shadowed = shadowed;
    state.stats.localLightCount = localLights.view.lightCount;
    state.stats.pointLightCount = localLights.view.pointLightCount;
    state.stats.spotLightCount = localLights.view.spotLightCount;
    state.stats.qualityLevel = static_cast<uint32_t>(quality);
    state.stats.temporalWeight = constants.frameParams.x;
    state.prepared = true;
    // 前向きの霧は霧の体積がある時だけ読む。
    return fogRequested;
}

void SetVolumetricVisibleWaterDepth(const WATER::WaterVisibleDepthView& view) {
    StageState& state = State();
    state.waterDepthView = view;
}

RenderTarget2D* ExecuteVolumetricLightingStage(RenderTarget2D& scene) {
    StageState& state = State();
    if (state.executedFrameIndex == state.preparedFrameIndex &&
        state.executedSceneColorResource == scene.GetResource()) {
        return &scene;
    }
    if (!state.prepared || !state.stats.active)
        return &scene;
    // allocation は Forward より前に完了済み。ここで別の target を受け入れない。
    if (state.sceneColorResource != scene.GetResource() || state.sceneDepthResource != scene.GetDepthResource() ||
        !state.constantAddress || !scene.BeginDepthShaderRead()) {
        state.prepared = state.stats.active = state.volumeHistoryValid = false;
        return &scene;
    }

    // SceneDepth remains authoritative and read-only. The water producer
    // must have run before MeshRenderer::EndFrame and publishes only a
    // borrowed SRV for this frame/view; every failure path keeps opaque
    // depth. Execute never re-enters MeshRenderer.
    state.waterDepthSrv = state.sceneDepthSrv.gpu;
    state.stats.waterDepthValid = false;
    state.stats.waterDepthFallback = true;
    const WATER::WaterVisibleDepthView& waterView = state.waterDepthView;
    if (waterView.IsValid() && waterView.frameIndex == state.preparedFrameIndex &&
        waterView.viewToken == scene.GetDepthResource() && waterView.width == state.stats.renderWidth &&
        waterView.height == state.stats.renderHeight) {
        state.waterDepthSrv = waterView.srv;
        state.stats.waterDepthValid = true;
        state.stats.waterDepthFallback = false;
    }

    ID3D12GraphicsCommandList* cmd = state.context.cmdList;
    ID3D12DescriptorHeap* heap = state.context.srvHeap;
    if (!cmd || !heap) {
        scene.EndDepthShaderRead();
        state.prepared = state.stats.active = state.volumeHistoryValid = false;
        return &scene;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS constantAddress = state.constantAddress;
    VolumeResource& output = state.injected[state.writeIndex];
    VolumeResource& history = state.injected[state.writeIndex ^ 1u];
    const auto uavBarrier = [cmd](VolumeResource& volume) {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(volume.texture.Get());
        cmd->ResourceBarrier(1, &barrier);
    };

    GFX::PIX::ScopedGpuEvent event(cmd, GFX::PIX::kColorPost, "VolumetricLighting");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(cmd, GFX::GPU_PROFILE::Pass::VolumetricLighting);
    cmd->SetDescriptorHeaps(1, &heap);
    if (state.fogInjected) {
        cmd->SetComputeRootSignature(state.computeRoot.Get());
        cmd->SetComputeRootConstantBufferView(kInjectConstants, constantAddress);
        cmd->SetComputeRootDescriptorTable(kInjectSceneDepth, state.sceneDepthSrv.gpu);
        // 環境光の採取（動的 GI の atlas）と注入（局所光の配光・クッキー）が読む。
        cmd->SetComputeRootDescriptorTable(
            kInjectTexturePool, GFX::DESCRIPTOR::GpuAt(heap, state.descriptorStride, GFX::DESCRIPTOR::kUserSrvBegin));
        if (state.giAmbient) {
            TransitionVolume(cmd, state.ambient, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetPipelineState(state.gatherPso.Get());
            cmd->SetComputeRootDescriptorTable(kInjectOutput, state.ambient.uav.gpu);
            cmd->Dispatch((state.ambientWidth + 7u) / 8u, (state.ambientHeight + 7u) / 8u, state.ambientDepth);
            uavBarrier(state.ambient);
            TransitionVolume(cmd, state.ambient, D3D12_RESOURCE_STATE_GENERIC_READ);
        }
        TransitionVolume(cmd, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->SetPipelineState(state.injectPso.Get());
        const D3D12_GPU_DESCRIPTOR_HANDLE shadow =
            state.stats.shadowed ? SHADOW::GetDirectionalShadowSrv() : state.sceneDepthSrv.gpu;
        cmd->SetComputeRootDescriptorTable(kInjectShadowMap, shadow);
        cmd->SetComputeRootDescriptorTable(kInjectHistory, history.srv.gpu);
        cmd->SetComputeRootDescriptorTable(kInjectAmbient, state.ambient.srv.gpu);
        cmd->SetComputeRootDescriptorTable(kInjectOutput, output.uav.gpu);
        cmd->SetComputeRootDescriptorTable(kInjectLocalShadow,
                                           state.localShadowSrv.ptr ? state.localShadowSrv : state.sceneDepthSrv.gpu);
        if (state.localLights.IsValid()) {
            cmd->SetComputeRootShaderResourceView(kInjectLights, state.localLights.lights);
            cmd->SetComputeRootShaderResourceView(kInjectLightClusterHeaders, state.localLights.clusterHeaders);
            cmd->SetComputeRootShaderResourceView(kInjectLightClusterIndices, state.localLights.clusterIndices);
        }
        cmd->Dispatch((state.stats.froxelWidth + 7u) / 8u, (state.stats.froxelHeight + 7u) / 8u,
                      state.stats.froxelDepth);
        uavBarrier(output);
        TransitionVolume(cmd, output, D3D12_RESOURCE_STATE_GENERIC_READ);

        TransitionVolume(cmd, state.integrated, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->SetPipelineState(state.integratePso.Get());
        cmd->SetComputeRootDescriptorTable(kInjectInjected, output.srv.gpu);
        cmd->SetComputeRootDescriptorTable(kInjectOutput, state.integrated.uav.gpu);
        cmd->Dispatch((state.stats.froxelWidth + 7u) / 8u, (state.stats.froxelHeight + 7u) / 8u, 1u);
        uavBarrier(state.integrated);
        TransitionVolume(cmd, state.integrated, D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    scene.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetDescriptorHeaps(1, &heap);
    cmd->SetComputeRootSignature(state.compositeRoot.Get());
    cmd->SetPipelineState(state.compositePso.Get());
    cmd->SetComputeRootConstantBufferView(kCompositeConstants, constantAddress);
    cmd->SetComputeRootDescriptorTable(kCompositeDepth, state.sceneDepthSrv.gpu);
    cmd->SetComputeRootDescriptorTable(kCompositeIntegrated, state.integrated.srv.gpu);
    cmd->SetComputeRootDescriptorTable(kCompositeWaterDepth,
                                       state.waterDepthSrv.ptr != 0 ? state.waterDepthSrv : state.sceneDepthSrv.gpu);
    cmd->SetComputeRootDescriptorTable(kCompositeSceneColor, state.sceneColorUav.gpu);
    cmd->SetComputeRootDescriptorTable(kCompositeAerialPerspective, state.aerialPerspectiveSrv.ptr != 0u
                                                                        ? state.aerialPerspectiveSrv
                                                                        : state.integrated.srv.gpu);
    cmd->Dispatch((state.stats.renderWidth + 7u) / 8u, (state.stats.renderHeight + 7u) / 8u, 1u);
    const auto sceneUavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(scene.GetResource());
    cmd->ResourceBarrier(1, &sceneUavBarrier);
    scene.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    scene.EndDepthShaderRead();

    state.volumeHistoryValid = state.fogInjected;
    state.historyView = state.preparedView;
    state.historyFrameIndex = state.preparedFrameIndex;
    state.executedFrameIndex = state.preparedFrameIndex;
    state.executedSceneColorResource = scene.GetResource();
    ++state.stats.dispatchCount;
    return &scene;
}

bool GetVolumetricFogSamplingView(uint64_t frameIndex, VolumetricFogSamplingView& outView) noexcept {
    outView = {};
    StageState& state = State();
    if (!state.stats.active || !state.fogInjected || state.executedFrameIndex != frameIndex ||
        state.preparedFrameIndex != frameIndex || !state.integrated.srv.IsValid() ||
        state.integrated.srv.gpu.ptr == 0u || state.stats.froxelDepth == 0u) {
        return false;
    }
    outView.integratedVolumeSrv = state.integrated.srv.gpu;
    outView.nearDistance = state.fogNearDistance;
    outView.farDistance = state.fogFarDistance;
    outView.depthSlices = state.stats.froxelDepth;
    outView.frameIndex = frameIndex;
    outView.valid = true;
    return true;
}

bool IsVolumetricLightingActive() { return State().prepared && State().stats.active; }

const VolumetricLightingStats& GetVolumetricLightingStats() { return State().stats; }

void ShutdownVolumetricLightingStage() {
    StageState& state = State();
    for (VolumeResource& volume : state.injected)
        ReleaseVolume(volume);
    ReleaseVolume(state.integrated);
    ReleaseVolume(state.ambient);
    ReleaseView(state.sceneColorUav);
    ReleaseView(state.sceneDepthSrv);
    for (auto& uploads : state.uploads)
        uploads.Finalize(&state.context);
    state.constantAddress = 0;
    state.gatherPso.Reset();
    state.injectPso.Reset();
    state.integratePso.Reset();
    state.compositePso.Reset();
    state.descriptorStride = 0;
    state.ambientWidth = state.ambientHeight = state.ambientDepth = 0;
    state.giAmbient = false;
    state.computeRoot.Reset();
    state.compositeRoot.Reset();
    state.sceneColorResource = nullptr;
    state.sceneDepthResource = nullptr;
    state.waterDepthSrv = {};
    state.waterDepthView = {};
    state.stats = {};
    state.writeIndex = 0;
    state.preparedFrameIndex = 0;
    state.executedFrameIndex = (std::numeric_limits<uint64_t>::max)();
    state.executedSceneColorResource = nullptr;
    state.fogNearDistance = state.fogFarDistance = 0.0f;
    state.historyFrameIndex = 0;
    state.preparedView = state.historyView = {};
    state.lastSettingsSignature = 0u;
    state.debugView = RenderDebugView::None;
    state.pipelineReady = false;
    state.resourcesReady = false;
    state.prepared = false;
    state.localShadowSrv = {};
    state.volumeHistoryValid = false;
    state.context = {};
}

} // namespace HIKARI::RENDER3D::VOLUMETRIC
