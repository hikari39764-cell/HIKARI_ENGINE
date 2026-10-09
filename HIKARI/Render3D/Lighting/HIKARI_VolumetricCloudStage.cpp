#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Assets/Shader/HIKARI_VolumetricShaderContract.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_Lightning.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::CLOUDS {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kCloudColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kCloudDepthFormat = DXGI_FORMAT_R32G32_FLOAT;
// 形状と細部は 1 チャンネル、天気図は雲域・雲型・対流セル（どれも一様分布の値）。
constexpr DXGI_FORMAT kVolumeNoiseFormat = DXGI_FORMAT_R8_UNORM;
constexpr DXGI_FORMAT kWeatherNoiseFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t kShapeSize = 128u;
constexpr uint32_t kDetailSize = 32u;
// 形状と細部の 3D ノイズの mip 段数（箱フィルタで縮める）。遠くは粗い段を読み、細かな凹凸のちらつきを出さない。
constexpr uint32_t kShapeMips = 6u;
constexpr uint32_t kDetailMips = 5u;
constexpr uint32_t kMaxNoiseMips = 6u;
constexpr uint32_t kWeatherSize = 1024u;
constexpr uint32_t kRtvDescriptorCount = 6u;
constexpr uint32_t kNoiseThreadGroupSize = 8u;
// ノイズの一様化に使う GPU のヒストグラム（4 チャンネル × 1024 段の度数 + 1025 段の累積分布）。
constexpr uint32_t kNoiseHistogramBins = 1024u;
constexpr uint32_t kNoiseHistogramChannels = 4u;
constexpr uint32_t kNoiseHistogramUints =
    kNoiseHistogramChannels * kNoiseHistogramBins + kNoiseHistogramChannels * (kNoiseHistogramBins + 1u);
// Render3D_VolumetricCloudNoise.hlsl の entry と同じ順。
enum class NoisePass : uint32_t {
    ClearHistogram,
    HistogramShape,
    HistogramDetail,
    HistogramWeather,
    ScanHistogram,
    GenerateShape,
    GenerateDetail,
    GenerateWeather,
    Downsample,
    Count
};
constexpr uint32_t kPassSrvCount = 9u;
// 雲の形の周期（形状・細部・天気図）[m]。形状ノイズは水平の周期で、鉛直はシェーダーが 1/2 の周期で読む
// （HIKARI_CLOUD_SHAPE_VERTICAL_SCALE）。雲の中の塊は水平で周期の 1/4〜1/16（600〜2400m）、天気図の対流セルは
// 6.7km・2.5km・1km。対流セルに合わせ、大きな雲が同じ大きさの小さな泡の集まりに見えず、塊が高さより幅広くなる。
constexpr double kShapePeriod = 9600.0;
// 細部ノイズの周期 [m]。縁を侵食する泡は周期の 1/2〜1/8（240〜60m）。320m だと 100m 前後の同じ大きさの泡が雲頂一面に
// 並ぶ。
constexpr double kDetailPeriod = 480.0;
constexpr double kWeatherPeriod = 40000.0;
// 雲のまわりの空気遠近。雲は混合層（霞の層）の上端にできるので、雲の描画だけ Mie を 3 倍、スケールハイトを雲底まで
// 引き上げる。遠くの雲の列ほど地平線の霞に溶け、手前の列との奥行きが出て粒の壁にならない（空そのものの LUT は変えない）。
constexpr float kCloudHazeScale = 3.0f;
constexpr float kCloudHazeHeightPerBase = 1.0f;
// 濃さ 1 の消散係数 [1/m] と、雲の発達でノイズを持ち上げる速さ [m/s]。雲の中の濃さは縁から内側へ 0.3〜0.6 程度に
// 育つので、雲の中の消散係数は積雲の実測（0.03〜0.06、視程 20〜30m）になる。
constexpr float kReferenceExtinction = 0.1f;
constexpr double kEvolutionRiseSpeed = 1.5;
// 雲底の高さの風は地上風（天気の windSpeed、10m 高）の 2.5 倍。晴天の 3m/s では雲が止まって見える。
// 細部は形状より速く流れて塊が形を変え、雲域と対流セルはゆっくり流れる（Horizon Zero Dawn と同じ層ごとの速さ）。
constexpr double kCloudWindScale = 2.5;
constexpr double kDetailWindScale = 1.3;
constexpr double kWeatherWindScale = 0.6;
// 地表へ落とす雲の影の表の範囲（カメラから光源に垂直な方向へ ±）[m]。
constexpr float kSolarExtent = 8000.0f;
// 環境光へ渡す雲の空（天空 LUT と同じ写像）の解像度。
constexpr uint32_t kSkyMapWidth = 128u;
constexpr uint32_t kSkyMapHeight = 64u;
constexpr DXGI_FORMAT kSkyMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// 雲の空の版を進める間隔 [s]。風で流れる雲を環境光へこの間隔で反映する。
constexpr double kSkyVersionSeconds = 2.0;

struct alignas(16) VolumetricCloudConstants {
    MATH::Mat4 inverseViewProj{};
    MATH::Mat4 previousViewProj{};
    MATH::Vec4 camera{};
    MATH::Vec4 historySize{};
    MATH::Vec4 shell{};
    MATH::Vec4 typeBase{};
    MATH::Vec4 typeThickness{};
    MATH::Vec4 weather{};
    MATH::Vec4 density{};
    MATH::Vec4 shapePhase{};
    MATH::Vec4 detailPhase{};
    MATH::Vec4 weatherPhase{};
    MATH::Vec4 lightDirection{};
    MATH::Vec4 lightColor{};
    MATH::Vec4 temporal{};
    MATH::Vec4 quality{};
    MATH::Vec4 aerial{};
    MATH::Vec4 windDelta{};
    MATH::Vec4 fallbackAmbient{};
    MATH::Vec4 airRayleigh{};
    MATH::Vec4 airMie{};
    MATH::Vec4 airUnderClouds{};
    MATH::Vec4 lightning{};
    MATH::Vec4 look{};
    MATH::Vec4 wind{};
};
static_assert(sizeof(VolumetricCloudConstants) == 496u);
static_assert(offsetof(VolumetricCloudConstants, camera) == 128u);
static_assert(offsetof(VolumetricCloudConstants, fallbackAmbient) == 384u);

struct alignas(16) NoiseConstants {
    uint32_t size[3] = {1u, 1u, 1u};
    uint32_t padding = 0u;
};
static_assert(sizeof(NoiseConstants) == 16u);

struct CloudTexture {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    uint32_t width = 0u;
    uint32_t height = 0u;

    bool IsValid() const noexcept {
        return resource != nullptr && srv.IsValid() && rtv.ptr != 0u && width != 0u && height != 0u;
    }
};

struct NoiseTexture {
    ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    // 3D ノイズの mip ごとの書き込み先（[0] は uav と同じ段）と、縮小元として読む 1 段だけの SRV。
    std::array<RenderResourceView, kMaxNoiseMips> mipUav{};
    std::array<RenderResourceView, kMaxNoiseMips> mipSrv{};
    uint32_t mips = 1u;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t depth = 1u;

    bool IsValid() const noexcept {
        return resource != nullptr && srv.IsValid() && uav.IsValid() && width != 0u && height != 0u && depth != 0u;
    }
};

// 画質ごとの追跡の設定。雲の外は距離 × stepPerDistance × 2.5 の粗い歩幅で探し、雲の中は画素の幅（最小 minimumStep）
// で進む。消散 0.1/m で 1 歩の光学的厚さが 1 前後になる 10m 前後が基準（HikariCloudMarchRay）。
struct QualityPolicy {
    uint32_t resolutionDivisor = 2u;
    uint32_t maxSteps = 64u;
    uint32_t lightSamples = 10u;
    float minimumStep = 12.0f;
    float stepPerDistance = 0.008f;
    // 個々の雲を追跡する最大距離 [m]。長いほど地平線の雲の列が壁になる。
    float maxDistance = 50000.0f;
    // 細部ノイズを使う距離。遠くまで保たないと、遠くの対流セルが丸屋根の粒に見える。
    float detailDistance = 28000.0f;
    float currentWeight = 0.08f;
    // 地表の影の表の列数（16km 四方）と光源方向の段数。Balanced で 1 列 83m。
    uint32_t solarResolution = 192u;
    uint32_t solarSteps = 20u;
    uint32_t qualityIndex = 1u;
};

QualityPolicy ResolveQualityPolicy(VolumetricCloudQuality quality) noexcept {
    switch (quality) {
    case VolumetricCloudQuality::Low: return {4u, 48u, 9u, 24.0f, 0.012f, 40000.0f, 14000.0f, 0.12f, 96u, 16u, 0u};
    // High は履歴を全解像度で持つ（追跡はその 2x2 ごとに 1 画素）。撮影・カットシーン向け。
    case VolumetricCloudQuality::High: return {1u, 128u, 12u, 8.0f, 0.006f, 70000.0f, 40000.0f, 0.06f, 320u, 24u, 2u};
    case VolumetricCloudQuality::Balanced:
    default: return {};
    }
}

struct StageState {
    GFX::Context context{};
    ID3D12Device* device = nullptr;

    ComPtr<ID3D12RootSignature> passRoot{};
    ComPtr<ID3D12RootSignature> noiseRoot{};
    ComPtr<ID3D12RootSignature> computeRoot{};
    ComPtr<ID3D12PipelineState> solarPso{};
    ComPtr<ID3D12PipelineState> skyPso{};
    ComPtr<ID3D12PipelineState> tracePso{};
    ComPtr<ID3D12PipelineState> reprojectPso{};
    ComPtr<ID3D12PipelineState> compositePso{};
    std::array<ComPtr<ID3D12PipelineState>, size_t(NoisePass::Count)> noisePso{};

    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
    D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = 0;

    ComPtr<ID3D12DescriptorHeap> rtvHeap{};
    UINT rtvDescriptorSize = 0u;

    NoiseTexture shapeNoise{};
    NoiseTexture detailNoise{};
    NoiseTexture weatherNoise{};
    ComPtr<ID3D12Resource> noiseHistogram{};
    NoiseTexture solarAtlas{};
    NoiseTexture skyMap{};
    uint64_t skyMapFence = 0u;
    uint64_t skyVersion = 0u;
    TextureResourceHandle solarTexture{};
    CloudSolarVisibilityFrame solarFrame{};

    CloudTexture currentColor{};
    CloudTexture currentDepth{};
    std::array<CloudTexture, 2u> historyColor{};
    std::array<CloudTexture, 2u> historyDepth{};

    VolumetricCloudStageStats stats{};

    uint32_t historyReadIndex = 0u;
    uint32_t historyWriteIndex = 1u;
    uint64_t lastFrameIndex = 0u;
    RenderViewId lastViewId{};
    RenderViewPurpose lastPurpose = RenderViewPurpose::Game;
    uint32_t lastQualityIndex = 0u;
    double lastWindX = 0.0;
    double lastWindZ = 0.0;
    bool historyValid = false;
    bool noiseReady = false;
    bool pipelineReady = false;
    uint64_t pipelineRetryFence = 0;
    bool resourcesReady = false;
    bool hasExecutedFrame = false;
};

StageState& State() {
    static StageState state{};
    return state;
}

float SanitizeFiniteRange(float value, float fallback, float minimum, float maximum) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

bool IsPrimaryPurpose(RenderViewPurpose purpose) noexcept {
    return purpose == RenderViewPurpose::Game || purpose == RenderViewPurpose::EditorScene;
}

// 周期 period のノイズを、風で流れた量 displacement だけずらす位相（倍精度で剰余を取ってから float にする）。
float NoisePhase(double displacement, double period) noexcept {
    const double phase = std::fmod(-displacement, period);
    return static_cast<float>(phase < 0.0 ? phase + period : phase);
}

void RetireView(StageState& state, RenderResourceView& view, const char* debugName) {
    if (!view.IsValid()) {
        return;
    }
    const RenderResourceView retired = view;
    view = {};
    if (state.context.deferredReleaseQueue != nullptr && state.context.currentFrameRetireFenceValue != 0u) {
        state.context.deferredReleaseQueue->Enqueue(
            state.context.currentFrameRetireFenceValue, [retired]() { (void)ReleaseRenderResourceDescriptor(retired); },
            debugName != nullptr ? debugName : "VolumetricCloudStage.Descriptor");
        return;
    }
    (void)ReleaseRenderResourceDescriptor(retired);
}

void RetireResource(StageState& state, ComPtr<ID3D12Resource>& resource, const char* debugName) {
    if (resource) {
        GFX::RetireD3D12ObjectForFrame(resource, state.context,
                                       debugName != nullptr ? debugName : "VolumetricCloudStage.Resource");
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE RtvAt(const StageState& state, uint32_t index) noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    if (state.rtvHeap == nullptr || index >= kRtvDescriptorCount) {
        return handle;
    }
    handle = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * static_cast<SIZE_T>(state.rtvDescriptorSize);
    return handle;
}

void RetireCloudTexture(StageState& state, CloudTexture& texture, const char* debugName) {
    RetireView(state, texture.srv, debugName);
    RetireResource(state, texture.resource, debugName);
    texture = {};
}

void RetireNoiseTexture(StageState& state, NoiseTexture& texture, const char* debugName) {
    RetireView(state, texture.srv, debugName);
    RetireView(state, texture.uav, debugName);
    for (uint32_t mip = 1u; mip < texture.mips; ++mip)
        RetireView(state, texture.mipUav[mip], debugName);
    for (uint32_t mip = 0u; mip + 1u < texture.mips; ++mip)
        RetireView(state, texture.mipSrv[mip], debugName);
    RetireResource(state, texture.resource, debugName);
    texture = {};
}

void RetireCloudTargets(StageState& state) {
    RetireCloudTexture(state, state.currentColor, "VolumetricCloudStage.CurrentColor");
    RetireCloudTexture(state, state.currentDepth, "VolumetricCloudStage.CurrentDepth");
    for (uint32_t index = 0u; index < 2u; ++index) {
        RetireCloudTexture(state, state.historyColor[index], "VolumetricCloudStage.HistoryColor");
        RetireCloudTexture(state, state.historyDepth[index], "VolumetricCloudStage.HistoryDepth");
    }
}

void InvalidateHistory(StageState& state) noexcept {
    if (state.historyValid) {
        ++state.stats.historyResetCount;
    }
    state.historyValid = false;
    state.stats.historyValid = false;
}

bool CreateRtvHeap(StageState& state) {
    if (state.rtvHeap != nullptr) {
        return true;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = kRtvDescriptorCount;
    if (!HIKARI_DX_CHECK(state.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(state.rtvHeap.GetAddressOf())),
                         "VolumetricCloudStage::CreateRtvHeap")) {
        return false;
    }
    state.rtvDescriptorSize = state.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    state.rtvHeap->SetName(L"HIKARI.VolumetricCloud.RTVHeap");
    return state.rtvDescriptorSize != 0u;
}

bool CreateCloudTexture(StageState& state, CloudTexture& texture, uint32_t width, uint32_t height, DXGI_FORMAT format,
                        uint32_t rtvIndex, const wchar_t* debugName) {
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc =
        CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1u, 1u, 1u, 0u, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;
    if (!HIKARI_DX_CHECK(state.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                                               IID_PPV_ARGS(texture.resource.GetAddressOf())),
                         "VolumetricCloudStage::CreateCloudTexture")) {
        return false;
    }
    texture.resource->SetName(debugName);
    texture.srv = AllocateTexture2DSrvDescriptor(texture.resource.Get(), format, 0u, 1u);
    texture.rtv = RtvAt(state, rtvIndex);
    if (!texture.srv.IsValid() || texture.rtv.ptr == 0u) {
        RetireView(state, texture.srv, "VolumetricCloudStage.CloudSRV");
        texture.resource.Reset();
        texture.rtv = {};
        return false;
    }
    state.device->CreateRenderTargetView(texture.resource.Get(), nullptr, texture.rtv);
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    texture.width = width;
    texture.height = height;
    return true;
}

bool CreateNoiseTexture(StageState& state, NoiseTexture& texture, uint32_t width, uint32_t height, uint32_t depth,
                        DXGI_FORMAT format, const wchar_t* debugName, uint32_t mips = 1u) {
    D3D12_RESOURCE_DESC desc =
        depth > 1u ? CD3DX12_RESOURCE_DESC::Tex3D(format, width, height, static_cast<UINT16>(depth),
                                                  static_cast<UINT16>(mips), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                   : CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1u, 1u, 1u, 0u,
                                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (!HIKARI_DX_CHECK(state.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                               IID_PPV_ARGS(texture.resource.GetAddressOf())),
                         "VolumetricCloudStage::CreateNoiseTexture")) {
        return false;
    }
    texture.resource->SetName(debugName);
    texture.mips = depth > 1u ? mips : 1u;
    if (depth > 1u) {
        texture.srv = AllocateTexture3DSrvDescriptor(texture.resource.Get(), format, 0u, mips);
        texture.uav = AllocateTexture3DUavDescriptor(texture.resource.Get(), format, 0u, 0u, depth);
        bool viewsValid = true;
        for (uint32_t mip = 1u; mip < mips; ++mip) {
            texture.mipUav[mip] =
                AllocateTexture3DUavDescriptor(texture.resource.Get(), format, mip, 0u, (std::max)(depth >> mip, 1u));
            texture.mipSrv[mip - 1u] = AllocateTexture3DSrvDescriptor(texture.resource.Get(), format, mip - 1u, 1u);
            viewsValid = viewsValid && texture.mipUav[mip].IsValid() && texture.mipSrv[mip - 1u].IsValid();
        }
        if (!viewsValid) {
            RetireNoiseTexture(state, texture, "VolumetricCloudStage.NoiseMip");
            return false;
        }
        texture.mipUav[0] = texture.uav;
    } else {
        texture.srv = AllocateTexture2DSrvDescriptor(texture.resource.Get(), format, 0u, 1u);
        texture.uav = AllocateTexture2DUavDescriptor(texture.resource.Get(), format, 0u);
    }
    if (!texture.srv.IsValid() || !texture.uav.IsValid()) {
        RetireView(state, texture.srv, "VolumetricCloudStage.NoiseSRV");
        RetireView(state, texture.uav, "VolumetricCloudStage.NoiseUAV");
        texture.resource.Reset();
        return false;
    }
    texture.state = D3D12_RESOURCE_STATE_COMMON;
    texture.width = width;
    texture.height = height;
    texture.depth = depth;
    return true;
}

bool CreateNoiseHistogram(StageState& state) {
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(uint64_t(kNoiseHistogramUints) * sizeof(uint32_t),
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (!HIKARI_DX_CHECK(state.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                               IID_PPV_ARGS(state.noiseHistogram.GetAddressOf())),
                         "VolumetricCloudStage::CreateNoiseHistogram")) {
        return false;
    }
    state.noiseHistogram->SetName(L"HIKARI.VolumetricCloud.NoiseHistogram");
    return true;
}

void RetireNoiseResources(StageState& state, const char* reason) {
    RetireNoiseTexture(state, state.shapeNoise, "VolumetricCloudStage.ShapeNoise");
    RetireNoiseTexture(state, state.detailNoise, "VolumetricCloudStage.DetailNoise");
    RetireNoiseTexture(state, state.weatherNoise, "VolumetricCloudStage.WeatherNoise");
    RetireResource(state, state.noiseHistogram, reason);
}

bool EnsureNoiseResources(StageState& state) {
    if (state.shapeNoise.IsValid() && state.detailNoise.IsValid() && state.weatherNoise.IsValid() &&
        state.noiseHistogram != nullptr) {
        return true;
    }
    RetireNoiseResources(state, "VolumetricCloudStage.NoiseHistogram");
    if (!CreateNoiseTexture(state, state.shapeNoise, kShapeSize, kShapeSize, kShapeSize, kVolumeNoiseFormat,
                            L"HIKARI.VolumetricCloud.ShapeNoise", kShapeMips) ||
        !CreateNoiseTexture(state, state.detailNoise, kDetailSize, kDetailSize, kDetailSize, kVolumeNoiseFormat,
                            L"HIKARI.VolumetricCloud.DetailNoise", kDetailMips) ||
        !CreateNoiseTexture(state, state.weatherNoise, kWeatherSize, kWeatherSize, 1u, kWeatherNoiseFormat,
                            L"HIKARI.VolumetricCloud.WeatherMap") ||
        !CreateNoiseHistogram(state)) {
        ++state.stats.resourceFailureCount;
        RetireNoiseResources(state, "VolumetricCloudStage.NoiseHistogram");
        return false;
    }
    state.noiseReady = false;
    state.stats.noiseReady = false;
    return true;
}

// 履歴（再構成）は描画解像度の 1/divisor。追跡はさらにその 2x2 ごとに 1 画素だけを毎フレーム場所を変えて行い
// （Horizon Zero Dawn の交互更新と同じ）、残りは前フレームの履歴を投影して埋める。
bool EnsureCloudTargets(StageState& state, uint32_t renderWidth, uint32_t renderHeight, uint32_t divisor) {
    const uint32_t historyWidth = (renderWidth + divisor - 1u) / divisor;
    const uint32_t historyHeight = (renderHeight + divisor - 1u) / divisor;
    const uint32_t traceWidth = (historyWidth + 1u) / 2u;
    const uint32_t traceHeight = (historyHeight + 1u) / 2u;
    if (state.resourcesReady && state.stats.renderWidth == renderWidth && state.stats.renderHeight == renderHeight &&
        state.stats.historyWidth == historyWidth && state.stats.historyHeight == historyHeight) {
        return true;
    }
    RetireCloudTargets(state);
    // RTV は heap 内の CPU ハンドル。実行中のコマンドが書き換えた記述子を見ないよう heap ごと退役させる。
    if (state.rtvHeap != nullptr) {
        GFX::RetireD3D12ObjectForFrame(state.rtvHeap, state.context, "VolumetricCloudStage.RtvHeap.Resize");
        state.rtvDescriptorSize = 0u;
    }
    if (!CreateRtvHeap(state) ||
        !CreateCloudTexture(state, state.currentColor, traceWidth, traceHeight, kCloudColorFormat, 0u,
                            L"HIKARI.VolumetricCloud.CurrentColor") ||
        !CreateCloudTexture(state, state.currentDepth, traceWidth, traceHeight, kCloudDepthFormat, 1u,
                            L"HIKARI.VolumetricCloud.CurrentDepth") ||
        !CreateCloudTexture(state, state.historyColor[0], historyWidth, historyHeight, kCloudColorFormat, 2u,
                            L"HIKARI.VolumetricCloud.HistoryColor0") ||
        !CreateCloudTexture(state, state.historyDepth[0], historyWidth, historyHeight, kCloudDepthFormat, 3u,
                            L"HIKARI.VolumetricCloud.HistoryDepth0") ||
        !CreateCloudTexture(state, state.historyColor[1], historyWidth, historyHeight, kCloudColorFormat, 4u,
                            L"HIKARI.VolumetricCloud.HistoryColor1") ||
        !CreateCloudTexture(state, state.historyDepth[1], historyWidth, historyHeight, kCloudDepthFormat, 5u,
                            L"HIKARI.VolumetricCloud.HistoryDepth1")) {
        ++state.stats.resourceFailureCount;
        RetireCloudTargets(state);
        state.resourcesReady = false;
        InvalidateHistory(state);
        return false;
    }
    state.stats.renderWidth = renderWidth;
    state.stats.renderHeight = renderHeight;
    state.stats.traceWidth = traceWidth;
    state.stats.traceHeight = traceHeight;
    state.stats.historyWidth = historyWidth;
    state.stats.historyHeight = historyHeight;
    state.stats.workingSetBytes =
        uint64_t(traceWidth) * traceHeight * 16ull + uint64_t(historyWidth) * historyHeight * 32ull +
        uint64_t(kShapeSize) * kShapeSize * kShapeSize * 8ull / 7ull +
        uint64_t(kDetailSize) * kDetailSize * kDetailSize * 8ull / 7ull + uint64_t(kWeatherSize) * kWeatherSize * 4ull;
    ++state.stats.resizeCount;
    state.resourcesReady = true;
    InvalidateHistory(state);
    return true;
}

template <class Texture>
void TransitionTexture(ID3D12GraphicsCommandList* cmd, Texture& texture, D3D12_RESOURCE_STATES nextState) {
    if (cmd == nullptr || texture.resource == nullptr || texture.state == nextState) {
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, nextState);
    cmd->ResourceBarrier(1u, &barrier);
    texture.state = nextState;
}

bool CreatePassRootSignature(StageState& state) {
    std::array<D3D12_DESCRIPTOR_RANGE, kPassSrvCount> ranges{};
    std::array<D3D12_ROOT_PARAMETER, kPassSrvCount + 1u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[0].Descriptor.ShaderRegister = 0u;
    for (uint32_t index = 0u; index < ranges.size(); ++index) {
        ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[index].NumDescriptors = 1u;
        ranges[index].BaseShaderRegister = index;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        parameters[index + 1u].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index + 1u].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[index + 1u].DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    std::array<D3D12_STATIC_SAMPLER_DESC, 3u> samplers{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0u;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1] = samplers[0];
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ShaderRegister = 1u;
    samplers[2] = samplers[1];
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[2].ShaderRegister = 2u;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(state.device, rootDesc, state.passRoot.GetAddressOf()),
                           "VolumetricCloudStage::CreatePassRootSignature");
}

// ノイズ生成のルート。b0 大きさ、u0 体積（mip 1 段）、u1 天気図、t0 縮小元の mip、u2 一様化のヒストグラム。
bool CreateNoiseRootSignature(StageState& state) {
    std::array<D3D12_DESCRIPTOR_RANGE, 3u> ranges{};
    std::array<D3D12_ROOT_PARAMETER, 5u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[0].Descriptor.ShaderRegister = 0u;
    for (uint32_t index = 0u; index < ranges.size(); ++index) {
        ranges[index].RangeType = index < 2u ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[index].NumDescriptors = 1u;
        ranges[index].BaseShaderRegister = index < 2u ? index : 0u;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        parameters[index + 1u].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index + 1u].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[index + 1u].DescriptorTable.NumDescriptorRanges = 1u;
        parameters[index + 1u].DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[4].Descriptor.ShaderRegister = 2u;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.data();
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(state.device, rootDesc, state.noiseRoot.GetAddressOf()),
                           "VolumetricCloudStage::CreateNoiseRootSignature");
}

// 計算パス（影の表と雲の空）のルート。b0 雲、b1 影の表、t0..t2 ノイズ、u0 出力、t8 天空 LUT。
bool CreateComputeRootSignature(StageState& state) {
    std::array<CD3DX12_DESCRIPTOR_RANGE, 5> ranges;
    std::array<CD3DX12_ROOT_PARAMETER, 7> parameters;
    parameters[0].InitAsConstantBufferView(0);
    parameters[1].InitAsConstantBufferView(1);
    for (UINT index = 0; index < 3; ++index) {
        ranges[index].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, index);
        parameters[index + 2].InitAsDescriptorTable(1, &ranges[index]);
    }
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    parameters[5].InitAsDescriptorTable(1, &ranges[3]);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
    parameters[6].InitAsDescriptorTable(1, &ranges[4]);
    const std::array samplers{
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                    D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                    D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP)};
    const CD3DX12_ROOT_SIGNATURE_DESC desc(UINT(parameters.size()), parameters.data(), UINT(samplers.size()),
                                           samplers.data());
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(state.device, desc, state.computeRoot.GetAddressOf()),
                           "VolumetricCloudStage::CreateComputeRootSignature");
}

bool PrepareFrameUploads(StageState& state) {
    if (state.context.frameIndex >= GFX::kFrameResourceCount || state.context.currentFrameRetireFenceValue == 0)
        return false;
    auto& uploads = state.uploads[state.context.frameIndex];
    if (!uploads.IsInitialized() && !uploads.Init(state.device, 8192))
        return false;
    uploads.ResetForFrame(state.context.currentFrameRetireFenceValue);
    return true;
}

bool EnsurePipeline(StageState& state) {
    if (state.pipelineReady) {
        return true;
    }
    if (state.context.currentFrameRetireFenceValue < state.pipelineRetryFence)
        return false;
    state.pipelineRetryFence = state.context.currentFrameRetireFenceValue + 60;
    if (state.device == nullptr || GFX::QueryShaderDeviceCapabilities(state.device).shaderModel < 60u ||
        !CreatePassRootSignature(state) || !CreateNoiseRootSignature(state) || !CreateComputeRootSignature(state)) {
        return false;
    }
    namespace SHADER = ASSETS::SHADER;
    using Program = SHADER::VolumetricShaderProgram;
    const auto deviceCaps = GFX::QueryShaderDeviceCapabilities(state.device);
    const SHADER::ShaderRuntimeDeviceCapabilities caps{deviceCaps.shaderModel, deviceCaps.rootSignatureVersion,
                                                       deviceCaps.featureMask};
    const auto resolve = [&](Program program, const char* entry, SHADER::ShaderStage stage) {
        return SHADER::ResolveVolumetricShaderEntry(program, entry, stage, caps);
    };
    const auto vertexShader = resolve(Program::CloudPass, "FullscreenVS", SHADER::ShaderStage::Vertex);
    const auto traceShader = resolve(Program::CloudPass, "TraceCloudPS", SHADER::ShaderStage::Pixel);
    const auto reprojectShader = resolve(Program::CloudPass, "ReprojectCloudPS", SHADER::ShaderStage::Pixel);
    const auto compositeShader = resolve(Program::CloudPass, "CompositeCloudPS", SHADER::ShaderStage::Pixel);
    const auto solarShader = resolve(Program::CloudPass, "SunTransmittanceCS", SHADER::ShaderStage::Compute);
    const auto skyShader = resolve(Program::CloudPass, "TraceCloudSkyCS", SHADER::ShaderStage::Compute);
    // NoisePass と同じ順。
    const std::array noiseShader{resolve(Program::CloudNoise, "ClearCloudHistogramCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "HistogramCloudShapeCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "HistogramCloudDetailCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "HistogramCloudWeatherCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "ScanCloudHistogramCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "GenerateCloudShapeCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "GenerateCloudDetailCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "GenerateCloudWeatherCS", SHADER::ShaderStage::Compute),
                                 resolve(Program::CloudNoise, "DownsampleCloudVolumeCS", SHADER::ShaderStage::Compute)};
    static_assert(noiseShader.size() == size_t(NoisePass::Count));
    const bool noiseResolved =
        std::all_of(noiseShader.begin(), noiseShader.end(), [](const auto& shader) { return bool(shader); });
    if (!vertexShader || !traceShader || !reprojectShader || !compositeShader || !solarShader || !skyShader ||
        !noiseResolved) {
        DEBUGLOG::PushRenderError("[VolumetricCloudStage][ERROR] cooked cloud shader unavailable or incompatible.");
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = state.passRoot.Get();
    desc.VS = {vertexShader.lease.GetDxil().data(), vertexShader.lease.GetDxil().size()};
    desc.PS = {traceShader.lease.GetDxil().data(), traceShader.lease.GetDxil().size()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 2u;
    desc.RTVFormats[0] = kCloudColorFormat;
    desc.RTVFormats[1] = kCloudDepthFormat;
    desc.SampleDesc.Count = 1u;
    if (!HIKARI_DX_CHECK(state.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(state.tracePso.GetAddressOf())),
                         "VolumetricCloudStage::CreateTracePSO")) {
        return false;
    }
    desc.PS = {reprojectShader.lease.GetDxil().data(), reprojectShader.lease.GetDxil().size()};
    if (!HIKARI_DX_CHECK(
            state.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(state.reprojectPso.GetAddressOf())),
            "VolumetricCloudStage::CreateReprojectPSO")) {
        return false;
    }
    // 合成は乗算済みの雲を場面の上へ重ねる（ONE / INV_SRC_ALPHA）。深度は読むだけ。
    desc.PS = {compositeShader.lease.GetDxil().data(), compositeShader.lease.GetDxil().size()};
    desc.NumRenderTargets = 1u;
    desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (!HIKARI_DX_CHECK(
            state.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(state.compositePso.GetAddressOf())),
            "VolumetricCloudStage::CreateCompositePSO")) {
        return false;
    }
    for (uint32_t index = 0u; index < noiseShader.size(); ++index) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC noiseDesc{};
        noiseDesc.pRootSignature = state.noiseRoot.Get();
        noiseDesc.CS = {noiseShader[index].lease.GetDxil().data(), noiseShader[index].lease.GetDxil().size()};
        if (!HIKARI_DX_CHECK(state.device->CreateComputePipelineState(
                                 &noiseDesc, IID_PPV_ARGS(state.noisePso[index].GetAddressOf())),
                             "VolumetricCloudStage::CreateNoisePSO")) {
            return false;
        }
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
    computeDesc.pRootSignature = state.computeRoot.Get();
    computeDesc.CS = {solarShader.lease.GetDxil().data(), solarShader.lease.GetDxil().size()};
    if (!HIKARI_DX_CHECK(
            state.device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(state.solarPso.GetAddressOf())),
            "VolumetricCloudStage::CreateSolarPSO")) {
        return false;
    }
    computeDesc.CS = {skyShader.lease.GetDxil().data(), skyShader.lease.GetDxil().size()};
    if (!HIKARI_DX_CHECK(
            state.device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(state.skyPso.GetAddressOf())),
            "VolumetricCloudStage::CreateSkyPSO")) {
        return false;
    }
    state.pipelineReady = true;
    state.stats.pipelineReady = true;
    return true;
}

bool GenerateNoise(StageState& state) {
    if (state.noiseReady) {
        return true;
    }
    ID3D12GraphicsCommandList* cmd = state.context.cmdList;
    ID3D12DescriptorHeap* heaps[] = {state.context.srvHeap};
    cmd->SetDescriptorHeaps(1u, heaps);
    cmd->SetComputeRootSignature(state.noiseRoot.Get());
    cmd->SetComputeRootUnorderedAccessView(4u, state.noiseHistogram->GetGPUVirtualAddress());
    const auto groups = [](uint32_t size) { return (size + kNoiseThreadGroupSize - 1u) / kNoiseThreadGroupSize; };
    const auto run = [&](NoisePass pass, std::array<uint32_t, 3> size, std::array<uint32_t, 3> dispatch,
                         D3D12_GPU_DESCRIPTOR_HANDLE volume, D3D12_GPU_DESCRIPTOR_HANDLE source) {
        const NoiseConstants constants{{size[0], size[1], size[2]}, 0u};
        const auto allocation = state.uploads[state.context.frameIndex].Allocate(sizeof(constants));
        if (!allocation)
            return false;
        std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
        cmd->SetPipelineState(state.noisePso[size_t(pass)].Get());
        cmd->SetComputeRootConstantBufferView(0u, allocation.gpuAddress);
        // 体積の出力（u0）・天気図の出力（u1）・縮小元（t0）は shader の対応するものだけが使う。
        cmd->SetComputeRootDescriptorTable(1u, volume);
        cmd->SetComputeRootDescriptorTable(2u, state.weatherNoise.uav.gpu);
        cmd->SetComputeRootDescriptorTable(3u, source);
        cmd->Dispatch(dispatch[0], dispatch[1], dispatch[2]);
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(state.noiseHistogram.Get());
        cmd->ResourceBarrier(1u, &barrier);
        return true;
    };
    // 一様化: ヒストグラムを消し、生の値を数え、チャンネルごとに累積してから、生の値を作り直して写す。
    const auto equalize = [&](NoisePass histogram, NoisePass generate, std::array<uint32_t, 3> size, uint32_t channels,
                              D3D12_GPU_DESCRIPTOR_HANDLE volume) {
        const std::array<uint32_t, 3> dispatch{groups(size[0]), groups(size[1]), size[2]};
        const D3D12_GPU_DESCRIPTOR_HANDLE source = state.weatherNoise.srv.gpu;
        return run(NoisePass::ClearHistogram, size, {(kNoiseHistogramUints + 255u) / 256u, 1u, 1u}, volume, source) &&
               run(histogram, size, dispatch, volume, source) &&
               run(NoisePass::ScanHistogram, size, {channels, 1u, 1u}, volume, source) &&
               run(generate, size, dispatch, volume, source);
    };
    // 3D ノイズは最も細かい段を作ってから、1 段ずつ箱フィルタで縮める。読み終えた段から順に読み取り状態へ移す。
    const auto generateVolume = [&](NoiseTexture& target, NoisePass histogram, NoisePass generate) {
        TransitionTexture(cmd, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!equalize(histogram, generate, {target.width, target.height, target.depth}, 1u, target.uav.gpu))
            return false;
        for (uint32_t mip = 1u; mip < target.mips; ++mip) {
            const std::array barriers{
                CD3DX12_RESOURCE_BARRIER::UAV(target.resource.Get()),
                CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, mip - 1u)};
            cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            const uint32_t size = (std::max)(target.width >> mip, 1u);
            if (!run(NoisePass::Downsample, {size, size, size}, {groups(size), groups(size), size},
                     target.mipUav[mip].gpu, target.mipSrv[mip - 1u].gpu))
                return false;
        }
        const std::array barriers{
            CD3DX12_RESOURCE_BARRIER::UAV(target.resource.Get()),
            CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, target.mips - 1u)};
        cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        target.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        return true;
    };
    if (!generateVolume(state.shapeNoise, NoisePass::HistogramShape, NoisePass::GenerateShape) ||
        !generateVolume(state.detailNoise, NoisePass::HistogramDetail, NoisePass::GenerateDetail))
        return false;
    TransitionTexture(cmd, state.weatherNoise, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!equalize(NoisePass::HistogramWeather, NoisePass::GenerateWeather,
                  {state.weatherNoise.width, state.weatherNoise.height, 1u}, kNoiseHistogramChannels,
                  state.shapeNoise.uav.gpu))
        return false;
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(state.weatherNoise.resource.Get());
    cmd->ResourceBarrier(1u, &barrier);
    TransitionTexture(cmd, state.weatherNoise, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    state.noiseReady = true;
    state.stats.noiseReady = true;
    return true;
}

// 層雲（0）・積雲（0.5）・積乱雲（1）の値を雲型で補間する。
float Lerp3(float t, float a, float b, float c) noexcept {
    return t < 0.5f ? a + (b - a) * (t * 2.0f) : b + (c - b) * (t * 2.0f - 1.0f);
}

// 雲の殻（下端と上端の高度）。今の雲型の前後 ±0.15 の範囲で最も低い雲底と最も高い雲頂を取る。
MATH::Vec2 ResolveShell(const MATH::Vec4& typeBase, const MATH::Vec4& typeThickness, float type, float precipitation) {
    float bottom = std::numeric_limits<float>::max();
    float top = 0.0f;
    for (const float sample : {type - 0.15f, type, type + 0.15f}) {
        const float t = std::clamp(sample, 0.0f, 1.0f);
        const float base = Lerp3(t, typeBase.x, typeBase.y, typeBase.z) - typeBase.w * precipitation;
        bottom = (std::min)(bottom, base);
        top = (std::max)(top, base + Lerp3(t, typeThickness.x, typeThickness.y, typeThickness.z));
    }
    // 雲底は形状ノイズで最大 160m（降水する雲は 360m）下がり、降水する雲はさらに最大 250m 垂れ下がる
    // （シェーダーの HikariCloudRaggedBase・HikariCloudResolveColumn）。
    return {(std::max)(bottom - 180.0f - 480.0f * precipitation, 10.0f), top + 50.0f};
}

// 天気の被覆率に設定の加算を足した、雲の実効の被覆率。
float ResolveCloudCoverage(const SceneEnvironment& environment) noexcept {
    return std::clamp(SanitizeFiniteRange(environment.frame.clouds.coverage, 0.0f, 0.0f, 1.0f) +
                          SanitizeFiniteRange(environment.clouds.coverageBias, 0.0f, -0.5f, 0.5f),
                      0.0f, 1.0f);
}

bool BuildConstants(StageState& state, const TEMPORAL::TemporalFrameState& temporalFrame,
                    const SceneEnvironment& environment, const QualityPolicy& policy, bool useHistory) {
    const CloudLayerSettings& layer = environment.clouds;
    const CloudWeatherFrame& weather = environment.frame.clouds;
    const auto& camera = temporalFrame.camera;
    const float planetRadius = SanitizeFiniteRange(environment.atmosphere.planetRadius, 6360000.0f, 1000.0f, 1.0e8f);
    VolumetricCloudConstants constants{};
    // 視線と履歴の再投影はカメラ相対の座標で、TAA のずらしを除いた行列を使う。
    // カメラ相対：行列に数 km の平行移動が入っていると、単精度の逆行列で作った視線を元の行列で投影し直す往復で
    // 画面の端が半画素以上ずれ（桁落ち）、履歴が毎フレームずれた位置から再標本化されて流れた筋になる
    // （2026-10-09、同じ画素を直接読む版と比べて確かめた）。VP × T(カメラ位置) の平行移動の列は射影の定数だけに
    // なり、前フレームの行列も T(今のカメラ位置) を掛けて相対座標から投影する。
    // ずらし無し：履歴は TAA のずらしを除いた画素の格子に持ち、追跡はこのフレームのずらし（gCloudWind.zw）ぶん標本の
    // 位置をずらし、合成はずらした位置で履歴を読む。こうして画面の TAA には幾何と同じくずらした雲が渡る。
    const MATH::Mat4 toCamera = MATH::Mat4::Translate({camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z});
    constants.inverseViewProj = MATH::Inverse(camera.unjitteredViewProj * toCamera);
    constants.previousViewProj = camera.prevUnjitteredViewProj * toCamera;
    constants.camera = {camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z, planetRadius};
    const auto inverse = [](uint32_t value) { return 1.0f / static_cast<float>((std::max)(1u, value)); };
    constants.historySize = {float(state.stats.historyWidth), float(state.stats.historyHeight),
                             inverse(state.stats.historyWidth), inverse(state.stats.historyHeight)};

    // 雲の種類ごとの雲底と厚さ。積雲の雲底（設定値）に比例させる。降水する雲は雲底が 300m 下がる。積雲の厚さは 2km
    // （被覆率 1 の最も強いセル。幅数 km の雲が高さより幅広く読める。深い対流は積乱雲の雲型で）。
    const float scale = SanitizeFiniteRange(layer.baseAltitude, 1500.0f, 100.0f, 10000.0f) / 1500.0f;
    constants.typeBase = {600.0f * scale, 1500.0f * scale, 1000.0f * scale, 300.0f};
    constants.typeThickness = {600.0f * scale, 2000.0f * scale, 8500.0f * scale,
                               RENDER3D::COLOR::GetActiveViewExposure().x};
    const float coverage = ResolveCloudCoverage(environment);
    const float type = SanitizeFiniteRange(weather.type, 0.5f, 0.0f, 1.0f);
    const float precipitation = SanitizeFiniteRange(weather.precipitation, 0.0f, 0.0f, 1.0f);
    const MATH::Vec2 shell = ResolveShell(constants.typeBase, constants.typeThickness, type, precipitation);
    // 最大距離の 6 割から個々の雲を平均の濃さへ移す（薄くするのは shader が最大距離の 1/4 から別に行う。平均の層が
    // セルの間の晴れを透かすようになってからは、移り始めが遅いほうが個々の雲の消え際が目立つ）。地平線近くで
    // 数十 km 分の雲の列が壁のように積み上がらず、遠くの小さな雲も粒の列（ポップコーン）に見えない。
    constants.shell = {shell.x, shell.y, policy.maxDistance, policy.maxDistance * 0.6f};
    constants.weather = {coverage, type, precipitation, 0.0f};
    const MATH::Vec3& albedo = environment.atmosphere.groundAlbedo;
    constants.density = {kReferenceExtinction * SanitizeFiniteRange(layer.densityScale, 1.0f, 0.0f, 8.0f) *
                             SanitizeFiniteRange(weather.density, 1.0f, 0.0f, 8.0f),
                         SanitizeFiniteRange(layer.detailErosion, 0.12f, 0.0f, 1.0f),
                         SanitizeFiniteRange(layer.ambientScale, 1.0f, 0.0f, 4.0f),
                         std::clamp(albedo.x * 0.2126f + albedo.y * 0.7152f + albedo.z * 0.0722f, 0.0f, 1.0f)};

    // 風で流れた量（地上風の積分 × 雲の高さの倍率）を倍精度で周期へ折りたたみ、float の精度を保つ。雲の発達は
    // 形状ノイズを上へ流して表す。
    const double shapePeriod = kShapePeriod * SanitizeFiniteRange(layer.shapeScale, 1.0f, 0.25f, 4.0f);
    const double groundWindX = std::isfinite(weather.windDisplacementX) ? weather.windDisplacementX : 0.0;
    const double groundWindZ = std::isfinite(weather.windDisplacementZ) ? weather.windDisplacementZ : 0.0;
    const double windX = groundWindX * kCloudWindScale;
    const double windZ = groundWindZ * kCloudWindScale;
    const double seconds = std::isfinite(weather.seconds) ? weather.seconds : 0.0;
    constants.shapePhase = {NoisePhase(windX, shapePeriod), NoisePhase(windZ, shapePeriod), float(shapePeriod),
                            NoisePhase(-seconds * kEvolutionRiseSpeed, shapePeriod)};
    constants.detailPhase = {NoisePhase(windX * kDetailWindScale, kDetailPeriod),
                             NoisePhase(windZ * kDetailWindScale, kDetailPeriod), float(kDetailPeriod),
                             policy.detailDistance};
    constants.weatherPhase = {NoisePhase(windX * kWeatherWindScale, kWeatherPeriod),
                              NoisePhase(windZ * kWeatherWindScale, kWeatherPeriod), float(kWeatherPeriod), 0.0f};
    // xy は形状ノイズの風の移動（履歴の再投影に使う）、z は履歴の P/P_prev（前フレームの Pre-Exposure との比）。
    const float windDeltaX = useHistory ? float((groundWindX - state.lastWindX) * kCloudWindScale) : 0.0f;
    const float windDeltaZ = useHistory ? float((groundWindZ - state.lastWindZ) * kCloudWindScale) : 0.0f;
    constants.windDelta = {windDeltaX, windDeltaZ, RENDER3D::COLOR::GetActiveViewExposure().w, 0.0f};

    // 主光源（昼は太陽、夜は月）。雲層の中ほどの高さで大気を通った照度と色を使う。
    const float midAltitude = (shell.x + shell.y) * 0.5f;
    const auto cloudLight = ATMOSPHERE::BuildAtmosphereSolarState(environment, midAltitude);
    const auto groundLight = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    constants.lightDirection = {cloudLight.sunDirection.x, cloudLight.sunDirection.y, cloudLight.sunDirection.z,
                                cloudLight.sunRadiance};
    constants.lightColor = {cloudLight.sunColor.x, cloudLight.sunColor.y, cloudLight.sunColor.z,
                            groundLight.sunRadiance};
    constants.temporal = {float(temporalFrame.frameIndex & 0xffffu), useHistory ? 1.0f : 0.0f, policy.currentWeight,
                          0.25f};
    constants.quality = {float(policy.maxSteps), float(policy.lightSamples), policy.minimumStep,
                         policy.stepPerDistance};
    const SKYRENDERER::AerialPerspectiveView& aerial = SKYRENDERER::GetAerialPerspectiveView();
    const bool skyView = SKYRENDERER::GetSkyViewLutSrv().ptr != 0u;
    // w は追跡の 1 画素が張る角度 [rad]。ノイズの mip を距離 × 角度の画素の幅で選ぶ。
    const float pixelAngle = 2.0f * std::tan(0.5f * SanitizeFiniteRange(camera.fovYRad, 1.0f, 0.01f, 3.0f)) *
                             inverse(state.stats.historyHeight);
    constants.aerial = {aerial.maxDistance, aerial.valid ? 1.0f : 0.0f, skyView ? 1.0f : 0.0f, pixelAngle};
    const MATH::Vec3 ambient = SKYRENDERER::EstimateSkyAmbientRadiance(environment);
    constants.fallbackAmbient = {ambient.x, ambient.y, ambient.z, 0.0f};
    // 遠くの雲と雲の空に掛ける空気遠近（指数大気）。Mie の消散は散乱の 1.11 倍。
    const AtmosphereSettings& air = environment.atmosphere;
    const AtmosphereSettings defaults{};
    const float airStrength =
        SanitizeFiniteRange(air.aerialPerspectiveStrength, defaults.aerialPerspectiveStrength, 0.0f, 16.0f);
    constants.airRayleigh = {
        air.rayleighScattering.x * airStrength, air.rayleighScattering.y * airStrength,
        air.rayleighScattering.z * airStrength,
        SanitizeFiniteRange(air.rayleighScaleHeight, defaults.rayleighScaleHeight, 100.0f, 100000.0f)};
    const float mieScattering = (air.mieScattering.x + air.mieScattering.y + air.mieScattering.z) / 3.0f;
    const float mieScaleHeight = SanitizeFiniteRange(air.mieScaleHeight, defaults.mieScaleHeight, 100.0f, 100000.0f);
    const CloudShadingEstimate shading = EstimateCloudShading(environment);
    constants.airMie = {
        std::isfinite(mieScattering) ? (std::max)(mieScattering, 0.0f) * 1.11f * airStrength * kCloudHazeScale : 0.0f,
        (std::max)(mieScaleHeight, kCloudHazeHeightPerBase * scale * 1500.0f), shading.shadeAltitude, 0.0f};
    constants.airUnderClouds = {shading.ambientRadiance.x, shading.ambientRadiance.y, shading.ambientRadiance.z,
                                shading.sunVisibility};
    // 落雷の光（雲の中の点光源）。色は HIKARI_Lightning.cpp と shader の HikariCloudLightningRadiance で同じ青白。
    const LIGHTNING::LightningLight lightning =
        LIGHTNING::ResolveLightningLight(environment, {camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z});
    constants.lightning = {lightning.position.x, lightning.position.y, lightning.position.z,
                           lightning.active ? lightning.intensity : 0.0f};
    // 見た目の倍率と置かれた形状に落とす影の濃さ、風の向き（雲の群れは風に沿う）。
    constants.look = {SanitizeFiniteRange(layer.sunlightScale, 1.0f, 0.0f, 4.0f),
                      SanitizeFiniteRange(layer.silverLining, 1.0f, 0.0f, 2.0f),
                      SanitizeFiniteRange(layer.clusterStrength, 1.0f, 0.0f, 1.0f),
                      SanitizeFiniteRange(layer.shadowStrength, 0.65f, 0.0f, 1.0f)};
    const float windX2 = std::isfinite(weather.windVelocity.x) ? weather.windVelocity.x : 0.0f;
    const float windZ2 = std::isfinite(weather.windVelocity.y) ? weather.windVelocity.y : 0.0f;
    const float windSpeed = std::sqrt(windX2 * windX2 + windZ2 * windZ2);
    // zw はこのフレームの TAA のずらし [uv]（画素のずらし / 履歴の大きさ）。
    constants.wind = {windSpeed > 1.0e-3f ? windX2 / windSpeed : 1.0f, windSpeed > 1.0e-3f ? windZ2 / windSpeed : 0.0f,
                      camera.jitter.x * inverse(state.stats.historyWidth),
                      camera.jitter.y * inverse(state.stats.historyHeight)};

    const auto allocation = state.uploads[state.context.frameIndex].Allocate(sizeof(constants));
    if (!allocation)
        return false;
    std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
    state.constantsAddress = allocation.gpuAddress;
    return true;
}

void SetCloudViewport(ID3D12GraphicsCommandList* cmd, uint32_t width, uint32_t height) {
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    cmd->RSSetViewports(1u, &viewport);
    cmd->RSSetScissorRects(1u, &scissor);
}

// t3/t4 は今読む雲の色と深度、t5 は前フレームの履歴の色。使わない段は有効な記述子で埋める。
void BindPassInputs(ID3D12GraphicsCommandList* cmd, const StageState& state, const CloudTexture& color,
                    const CloudTexture& depth, const CloudTexture& historyColor,
                    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth) {
    const D3D12_GPU_DESCRIPTOR_HANDLE aerial = SKYRENDERER::GetAerialPerspectiveView().srv;
    const D3D12_GPU_DESCRIPTOR_HANDLE skyView = SKYRENDERER::GetSkyViewLutSrv();
    cmd->SetGraphicsRootDescriptorTable(1u, state.shapeNoise.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(2u, state.detailNoise.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(3u, state.weatherNoise.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(4u, color.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(5u, depth.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(6u, historyColor.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(7u, sceneDepth);
    cmd->SetGraphicsRootDescriptorTable(8u, aerial.ptr != 0u ? aerial : state.shapeNoise.srv.gpu);
    cmd->SetGraphicsRootDescriptorTable(9u, skyView.ptr != 0u ? skyView : state.weatherNoise.srv.gpu);
}

void DrawFullscreen(ID3D12GraphicsCommandList* cmd) {
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0u, 0u, nullptr);
    cmd->IASetIndexBuffer(nullptr);
    cmd->DrawInstanced(3u, 1u, 0u, 0u);
}

void DrawCloudTargets(StageState& state, ID3D12PipelineState* pso, CloudTexture& color, CloudTexture& depth,
                      const CloudTexture& inputColor, const CloudTexture& inputDepth, const CloudTexture& historyColor,
                      D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth) {
    // 書き込み先の大きさで描く（追跡は追跡の解像度、再構成は履歴の解像度）。
    ID3D12GraphicsCommandList* cmd = state.context.cmdList;
    TransitionTexture(cmd, color, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionTexture(cmd, depth, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {color.rtv, depth.rtv};
    cmd->OMSetRenderTargets(2u, rtvs, FALSE, nullptr);
    SetCloudViewport(cmd, color.width, color.height);
    cmd->SetGraphicsRootSignature(state.passRoot.Get());
    cmd->SetPipelineState(pso);
    cmd->SetGraphicsRootConstantBufferView(0u, state.constantsAddress);
    BindPassInputs(cmd, state, inputColor, inputDepth, historyColor, sceneDepth);
    DrawFullscreen(cmd);
    TransitionTexture(cmd, color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionTexture(cmd, depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void ReleaseGpuState(StageState& state) {
    if (state.solarTexture)
        ReleaseTextureResource(state.solarTexture);
    state.solarTexture = {};
    state.solarFrame = {};
    RetireNoiseTexture(state, state.solarAtlas, "VolumetricCloudStage.SolarAtlas");
    RetireNoiseTexture(state, state.skyMap, "VolumetricCloudStage.SkyMap");
    state.skyMapFence = 0u;
    RetireNoiseResources(state, "VolumetricCloudStage.NoiseHistogram");
    RetireCloudTargets(state);
    for (auto& uploads : state.uploads)
        uploads.Finalize(&state.context);
    state.constantsAddress = 0;
    GFX::RetireD3D12ObjectForFrame(state.tracePso, state.context, "VolumetricCloudStage.TracePSO");
    GFX::RetireD3D12ObjectForFrame(state.reprojectPso, state.context, "VolumetricCloudStage.ReprojectPSO");
    GFX::RetireD3D12ObjectForFrame(state.compositePso, state.context, "VolumetricCloudStage.CompositePSO");
    GFX::RetireD3D12ObjectForFrame(state.solarPso, state.context, "VolumetricCloudStage.SolarPSO");
    GFX::RetireD3D12ObjectForFrame(state.skyPso, state.context, "VolumetricCloudStage.SkyPSO");
    for (ComPtr<ID3D12PipelineState>& noisePso : state.noisePso)
        GFX::RetireD3D12ObjectForFrame(noisePso, state.context, "VolumetricCloudStage.NoisePSO");
    GFX::RetireD3D12ObjectForFrame(state.passRoot, state.context, "VolumetricCloudStage.PassRoot");
    GFX::RetireD3D12ObjectForFrame(state.noiseRoot, state.context, "VolumetricCloudStage.NoiseRoot");
    GFX::RetireD3D12ObjectForFrame(state.computeRoot, state.context, "VolumetricCloudStage.ComputeRoot");
    GFX::RetireD3D12ObjectForFrame(state.rtvHeap, state.context, "VolumetricCloudStage.RtvHeap");
    state.rtvDescriptorSize = 0u;
    state.device = nullptr;
    state.pipelineReady = false;
    state.pipelineRetryFence = 0;
    state.resourcesReady = false;
    state.noiseReady = false;
    state.hasExecutedFrame = false;
    state.historyValid = false;
    state.stats = {};
}

// 共通の準備（デバイスの切り替え、パイプライン、ノイズ）。
bool PrepareStage(StageState& state, const GFX::Context& graphics) {
    if (state.device != nullptr &&
        (state.device != graphics.device || state.context.deviceGeneration != graphics.deviceGeneration ||
         state.context.frameFence != graphics.frameFence)) {
        ReleaseGpuState(state);
    }
    state.context = graphics;
    state.device = graphics.device;
    UpdateRenderResourceDescriptorPoolContext(graphics);
    return PrepareFrameUploads(state) && EnsurePipeline(state) && EnsureNoiseResources(state) && GenerateNoise(state);
}

// 雲の空の版。天気の値と kSkyVersionSeconds ごとの時刻で決め、環境光はこの版が変わった時だけ作り直す。
uint64_t ResolveSkyVersion(const CloudWeatherFrame& weather, const CloudLayerSettings& layer) noexcept {
    const auto quantize = [](float value) { return std::isfinite(value) ? uint64_t(std::lround(value * 64.0f)) : 0u; };
    const double seconds = std::isfinite(weather.seconds) ? weather.seconds : 0.0;
    uint64_t version = uint64_t(std::floor((std::max)(seconds, 0.0) / kSkyVersionSeconds));
    for (const float value : {weather.coverage, weather.type, weather.density, weather.precipitation,
                              layer.coverageBias, layer.clusterStrength, layer.sunlightScale, layer.silverLining})
        version = version * 1315423911ull + quantize(value) + 1u;
    return version == 0u ? 1u : version;
}

// 環境光へ渡す雲の空を描く。コマンドの状態は呼び出し側が影の表と共有する。
bool RenderSkyMap(StageState& state, const SceneEnvironment& environment) {
    if (!state.skyMap.IsValid() && !CreateNoiseTexture(state, state.skyMap, kSkyMapWidth, kSkyMapHeight, 1u,
                                                       kSkyMapFormat, L"HIKARI.VolumetricCloud.SkyMap"))
        return false;
    ID3D12GraphicsCommandList* commands = state.context.cmdList;
    const D3D12_GPU_DESCRIPTOR_HANDLE skyView = SKYRENDERER::GetSkyViewLutSrv();
    TransitionTexture(commands, state.skyMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetPipelineState(state.skyPso.Get());
    commands->SetComputeRootDescriptorTable(5, state.skyMap.uav.gpu);
    commands->SetComputeRootDescriptorTable(6, skyView.ptr != 0u ? skyView : state.weatherNoise.srv.gpu);
    commands->Dispatch((kSkyMapWidth + 7u) / 8u, (kSkyMapHeight + 7u) / 8u, 1u);
    TransitionTexture(commands, state.skyMap, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    state.skyMapFence = state.context.currentFrameRetireFenceValue;
    state.skyVersion = ResolveSkyVersion(environment.frame.clouds, environment.clouds);
    return true;
}

bool IsGraphicsReady(const GFX::Context& graphics) noexcept {
    return graphics.device != nullptr && graphics.cmdList != nullptr && graphics.srvHeap != nullptr &&
           graphics.frameFence != nullptr && graphics.frameIndex < GFX::kFrameResourceCount &&
           graphics.currentFrameRetireFenceValue != 0u;
}

} // namespace

bool HasVisibleClouds(const SceneEnvironment& environment) noexcept {
    const CloudWeatherFrame& weather = environment.frame.clouds;
    return environment.clouds.enabled && environment.clouds.densityScale > 0.0f && weather.density > 0.0f &&
           ResolveCloudCoverage(environment) > 0.0f;
}

CloudSolarVisibilityFrame PrepareCloudLightingFrame(const PIPELINE::RenderFrameContext& frame,
                                                    const TEMPORAL::TemporalFrameState& temporalFrame,
                                                    const SceneEnvironment& environment,
                                                    VolumetricCloudQuality quality) {
    StageState& state = State();
    state.stats.solarVisibilityReady = false;
    const GFX::Context& graphics = frame.graphics;
    if (!HasVisibleClouds(environment)) {
        state.skyMapFence = 0u;
        state.stats.skyMapReady = false;
        return {};
    }
    if (!IsPrimaryPurpose(frame.purpose) || frame.viewId != kPrimaryRenderViewId || !temporalFrame.camera.valid ||
        !IsGraphicsReady(graphics) || !MATH::IsFinite(temporalFrame.camera.cameraPos))
        return {};
    if (state.solarFrame && state.solarFrame.fenceValue == graphics.currentFrameRetireFenceValue) {
        state.stats.solarVisibilityReady = true;
        return state.solarFrame;
    }
    if (!PrepareStage(state, graphics))
        return {};
    const QualityPolicy policy = ResolveQualityPolicy(quality);
    const UINT resolution = policy.solarResolution;
    const UINT steps = policy.solarSteps;
    if (!BuildConstants(state, temporalFrame, environment, policy, false))
        return {};
    ID3D12GraphicsCommandList* commands = graphics.cmdList;
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::CloudSolarVisibility);
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(state.computeRoot.Get());
    commands->SetComputeRootConstantBufferView(0, state.constantsAddress);
    commands->SetComputeRootDescriptorTable(2, state.shapeNoise.srv.gpu);
    commands->SetComputeRootDescriptorTable(3, state.detailNoise.srv.gpu);
    commands->SetComputeRootDescriptorTable(4, state.weatherNoise.srv.gpu);
    // 雲の空は夜（月明かり）も描く。版（天気の量と 2 秒ごとの時刻）が変わった時だけ描き直す。環境光の畳み込みも
    // 版が変わった時にしか作り直さないので、毎フレーム描いても読まれない。影の表は太陽が出ている時だけ。
    const uint64_t skyVersion = ResolveSkyVersion(environment.frame.clouds, environment.clouds);
    if (!state.skyMap.IsValid() || state.skyMapFence == 0u || state.skyVersion != skyVersion)
        state.stats.skyMapReady = RenderSkyMap(state, environment);
    else
        state.stats.skyMapReady = true;
    if (state.solarAtlas.IsValid() && state.solarAtlas.width != resolution * 8) {
        if (graphics.frameFence->GetCompletedValue() < state.solarFrame.fenceValue)
            return {};
        if (state.solarTexture)
            ReleaseTextureResource(state.solarTexture);
        state.solarTexture = {};
        state.solarFrame = {};
        RetireNoiseTexture(state, state.solarAtlas, "VolumetricCloudStage.SolarAtlas.Resize");
    }
    if (!state.solarAtlas.IsValid()) {
        if (!CreateNoiseTexture(state, state.solarAtlas, resolution * 8, ((steps + 8) / 8) * resolution, 1,
                                DXGI_FORMAT_R32G32_FLOAT, L"HIKARI.VolumetricCloud.SolarTransmittance"))
            return {};
        state.solarTexture =
            RegisterTextureResourceFromNative(state.solarAtlas.resource.Get(), DXGI_FORMAT_R32G32_FLOAT);
        if (!IsTextureResourceValid(state.solarTexture) ||
            GetTextureResourceSrvDescriptorIndex(state.solarTexture) >= GFX::DESCRIPTOR::kUserSrvCount) {
            if (state.solarTexture)
                ReleaseTextureResource(state.solarTexture);
            state.solarTexture = {};
            RetireNoiseTexture(state, state.solarAtlas, "VolumetricCloudStage.SolarAtlas.RegisterFailed");
            return {};
        }
    }
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    if (solar.sunRadiance <= 0.0f)
        return {};
    // 光源に垂直な面の格子。格子の大きさに原点を吸着させ、カメラの移動で影が揺れないようにする。
    LIGHTING::CloudSolarVisibilityGpuData data;
    const MATH::Vec3 direction = solar.sunDirection;
    const MATH::Vec3 right = MATH::Normalize(
        MATH::Cross(std::abs(direction.y) < 0.99f ? MATH::Vec3{0, 1, 0} : MATH::Vec3{0, 0, 1}, direction));
    const MATH::Vec3 up = MATH::Cross(direction, right);
    const MATH::Vec3 camera{temporalFrame.camera.cameraPos.x, temporalFrame.camera.cameraPos.y,
                            temporalFrame.camera.cameraPos.z};
    const float cell = 2.0f * kSolarExtent / float(resolution);
    const MATH::Vec3 origin = right * (std::floor(MATH::Dot(camera, right) / cell) * cell) +
                              up * (std::floor(MATH::Dot(camera, up) / cell) * cell) +
                              direction * MATH::Dot(camera, direction);
    data.originDescriptor = {origin.x, origin.y, origin.z,
                             float(GetTextureResourceSrvDescriptorIndex(state.solarTexture) + 1)};
    data.rightExtent = {right.x, right.y, right.z, kSolarExtent};
    data.upResolution = {up.x, up.y, up.z, float(resolution)};
    data.directionSteps = {direction.x, direction.y, direction.z, float(steps)};
    const auto allocation = state.uploads[graphics.frameIndex].Allocate(sizeof(data));
    if (!allocation)
        return {};
    std::memcpy(allocation.cpuAddress, &data, sizeof(data));
    TransitionTexture(commands, state.solarAtlas, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commands->SetPipelineState(state.solarPso.Get());
    commands->SetComputeRootConstantBufferView(1, allocation.gpuAddress);
    commands->SetComputeRootDescriptorTable(5, state.solarAtlas.uav.gpu);
    commands->Dispatch((resolution + 7) / 8, (resolution + 7) / 8, 1);
    TransitionTexture(commands, state.solarAtlas, D3D12_RESOURCE_STATE_GENERIC_READ);
    state.stats.solarVisibilityReady = true;
    state.stats.solarResolution = resolution;
    state.stats.solarSteps = steps;
    const auto desc = state.solarAtlas.resource->GetDesc();
    state.stats.solarBytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    state.solarFrame = {data, state.solarAtlas.srv.gpu, graphics.currentFrameRetireFenceValue};
    return state.solarFrame;
}

bool ExecuteVolumetricCloudStage(const PIPELINE::RenderFrameContext& frame,
                                 const TEMPORAL::TemporalFrameState& temporalFrame, const SceneEnvironment& environment,
                                 VolumetricCloudQuality quality) {
    StageState& state = State();
    state.stats.requested = HasVisibleClouds(environment);
    state.stats.active = false;
    state.stats.rendered = false;
    if (!state.stats.requested || !IsPrimaryPurpose(frame.purpose) || !(frame.viewId == kPrimaryRenderViewId) ||
        !IsGraphicsReady(frame.graphics) || frame.renderWidth == 0u || frame.renderHeight == 0u ||
        frame.scene.depth.resource == nullptr || !frame.scene.depth.IsSampleable() ||
        frame.scene.readOnlyDsv.ptr == 0u || frame.targetAccess.beginDepthRead == nullptr ||
        frame.targetAccess.endDepthRead == nullptr || frame.targetAccess.rebind == nullptr ||
        !temporalFrame.camera.valid || !MATH::IsFinite(temporalFrame.camera.invUnjitteredViewProj) ||
        !MATH::IsFinite(temporalFrame.camera.prevViewProj)) {
        InvalidateHistory(state);
        return false;
    }
    const QualityPolicy policy = ResolveQualityPolicy(quality);
    // 履歴はビュー・画質・解像度が変わった時と、カメラの切り替え・フレームの飛びでだけ捨てる。
    // 太陽・天気・風・露出の連続的な変化では捨てない。
    if ((state.hasExecutedFrame &&
         (temporalFrame.frameIndex != state.lastFrameIndex + 1u || !(state.lastViewId == frame.viewId) ||
          state.lastPurpose != frame.purpose || state.lastQualityIndex != policy.qualityIndex)) ||
        frame.cameraCut || temporalFrame.resetHistory || !temporalFrame.historyValid ||
        !temporalFrame.camera.previousValid) {
        InvalidateHistory(state);
    }
    if (!PrepareStage(state, frame.graphics) ||
        !EnsureCloudTargets(state, frame.renderWidth, frame.renderHeight, policy.resolutionDivisor)) {
        InvalidateHistory(state);
        return false;
    }
    state.stats.resourcesReady = true;
    const bool useHistory = state.historyValid;
    if (!BuildConstants(state, temporalFrame, environment, policy, useHistory)) {
        InvalidateHistory(state);
        return false;
    }

    GFX::PIX::ScopedGpuEvent event(state.context.cmdList, GFX::PIX::kColorPost, "VolumetricClouds");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(state.context.cmdList, GFX::GPU_PROFILE::Pass::VolumetricClouds);
    if (!frame.targetAccess.BeginDepthRead())
        return false;
    ID3D12GraphicsCommandList* cmd = state.context.cmdList;
    ID3D12DescriptorHeap* heaps[] = {state.context.srvHeap};
    cmd->SetDescriptorHeaps(1u, heaps);
    const D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth = frame.scene.depth.srv;
    const uint32_t readIndex = state.historyReadIndex;
    const uint32_t writeIndex = state.historyWriteIndex;
    // 追跡（2x2 ごとに 1 画素）→ 履歴へ再構成して書き込み先の履歴へ → それを全解像度へ合成。
    DrawCloudTargets(state, state.tracePso.Get(), state.currentColor, state.currentDepth, state.historyColor[readIndex],
                     state.historyDepth[readIndex], state.historyColor[readIndex], sceneDepth);
    DrawCloudTargets(state, state.reprojectPso.Get(), state.historyColor[writeIndex], state.historyDepth[writeIndex],
                     state.currentColor, state.currentDepth, state.historyColor[readIndex], sceneDepth);
    state.historyReadIndex = writeIndex;
    state.historyWriteIndex = readIndex;

    // 場面の色と読み取り専用の深度を描画先へ戻す。
    if (!frame.targetAccess.BeginDepthRead()) {
        frame.targetAccess.EndDepthRead();
        (void)frame.targetAccess.Rebind();
        InvalidateHistory(state);
        return false;
    }
    SetCloudViewport(cmd, frame.renderWidth, frame.renderHeight);
    cmd->SetGraphicsRootSignature(state.passRoot.Get());
    cmd->SetPipelineState(state.compositePso.Get());
    cmd->SetGraphicsRootConstantBufferView(0u, state.constantsAddress);
    BindPassInputs(cmd, state, state.historyColor[writeIndex], state.historyDepth[writeIndex],
                   state.historyColor[readIndex], sceneDepth);
    DrawFullscreen(cmd);
    frame.targetAccess.EndDepthRead();
    if (!frame.targetAccess.Rebind()) {
        InvalidateHistory(state);
        return false;
    }

    const CloudWeatherFrame& weather = environment.frame.clouds;
    state.lastWindX = std::isfinite(weather.windDisplacementX) ? weather.windDisplacementX : 0.0;
    state.lastWindZ = std::isfinite(weather.windDisplacementZ) ? weather.windDisplacementZ : 0.0;
    state.lastFrameIndex = temporalFrame.frameIndex;
    state.lastViewId = frame.viewId;
    state.lastPurpose = frame.purpose;
    state.lastQualityIndex = policy.qualityIndex;
    state.historyValid = true;
    state.stats.historyValid = useHistory;
    state.stats.maxSteps = policy.maxSteps;
    state.stats.lightSamples = policy.lightSamples;
    state.stats.qualityIndex = policy.qualityIndex;
    state.stats.rendered = true;
    state.stats.active = true;
    ++state.stats.traceDispatchCount;
    if (!state.hasExecutedFrame) {
        HIKARI_LOG_INFO("[VolumetricCloudStage] active trace=" + std::to_string(state.stats.traceWidth) + "x" +
                        std::to_string(state.stats.traceHeight) + " steps=" + std::to_string(policy.maxSteps) +
                        " workingSetBytes=" + std::to_string(state.stats.workingSetBytes));
    }
    state.hasExecutedFrame = true;
    return true;
}

CloudShadingEstimate EstimateCloudShading(const SceneEnvironment& environment) {
    CloudShadingEstimate estimate{};
    if (!HasVisibleClouds(environment))
        return estimate;
    const CloudLayerSettings& layer = environment.clouds;
    const CloudWeatherFrame& weather = environment.frame.clouds;
    const float coverage = ResolveCloudCoverage(environment);
    const float type = SanitizeFiniteRange(weather.type, 0.5f, 0.0f, 1.0f);
    const float precipitation = SanitizeFiniteRange(weather.precipitation, 0.0f, 0.0f, 1.0f);
    const float scale = SanitizeFiniteRange(layer.baseAltitude, 1500.0f, 100.0f, 10000.0f) / 1500.0f;
    const float base = Lerp3(type, 600.0f, 1500.0f, 1000.0f) * scale;
    // 雲の高さは被覆率とともに伸びる（シェーダーの HikariCloudResolveColumn と同じ）。平均の厚さはその半分。
    const float height =
        Lerp3(type, 600.0f, 2500.0f, 8500.0f) * scale * (0.08f + 0.92f * coverage * std::sqrt(coverage));
    const float depth = kReferenceExtinction * SanitizeFiniteRange(layer.densityScale, 1.0f, 0.0f, 8.0f) *
                        SanitizeFiniteRange(weather.density, 1.0f, 0.0f, 8.0f) * (1.0f + 0.6f * precipitation) * 0.5f *
                        height;
    const auto light = ATMOSPHERE::BuildAtmosphereSolarState(environment, base + height * 0.5f);
    const float elevation = (std::max)(light.sunDirection.y, 0.0f);
    estimate.sunVisibility = 1.0f - coverage * (1.0f - std::exp(-depth / (std::max)(elevation, 0.1f)));
    // 雲底の輝度。上から入る直射と空の光が拡散透過 1/(1 + 0.11τ) で抜けてくる。
    const float diffuse = 1.0f / (1.0f + 0.11f * depth);
    const MATH::Vec3 sky = SKYRENDERER::EstimateSkyAmbientRadiance(environment);
    const MATH::Vec3 cloudBase = (light.sunColor * (light.sunRadiance * elevation / 3.14159265f) + sky) * diffuse;
    estimate.ambientRadiance = sky * (1.0f - coverage) + cloudBase * coverage;
    estimate.shadeAltitude = base + height * 0.5f;
    return estimate;
}

CloudSkyView GetCloudSkyView() {
    const StageState& state = State();
    if (state.skyMapFence == 0u || !state.skyMap.IsValid())
        return {};
    return {state.skyMap.srv.gpu, state.skyVersion};
}

const VolumetricCloudStageStats& GetVolumetricCloudStageStats() { return State().stats; }

void ShutdownVolumetricCloudStage() {
    StageState& state = State();
    ReleaseGpuState(state);
    state.context = {};
    state.historyReadIndex = 0u;
    state.historyWriteIndex = 1u;
    state.lastFrameIndex = 0u;
    state.lastViewId = {};
    state.lastPurpose = RenderViewPurpose::Game;
}

} // namespace HIKARI::RENDER3D::CLOUDS
