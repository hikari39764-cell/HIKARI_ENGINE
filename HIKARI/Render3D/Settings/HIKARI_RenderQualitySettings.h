#pragma once

#include <cstdint>
#include <string_view>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::RENDER3D {

enum class RenderResolutionPreset : uint8_t {
    Viewport = 0,
    P720,
    P1080,
    P1440,
    P2160,
};

inline constexpr TEXT::EnumName<RenderResolutionPreset> kRenderResolutionPresetNames[] = {
    {RenderResolutionPreset::Viewport, "Viewport"}, {RenderResolutionPreset::P720, "720p"},
    {RenderResolutionPreset::P1080, "1080p"},       {RenderResolutionPreset::P1440, "1440p"},
    {RenderResolutionPreset::P2160, "2160p"},
};

inline constexpr TEXT::EnumName<RenderResolutionPreset> kRenderResolutionPresetLabels[] = {
    {RenderResolutionPreset::Viewport, "Viewport"}, {RenderResolutionPreset::P720, "1280 x 720"},
    {RenderResolutionPreset::P1080, "1920 x 1080"}, {RenderResolutionPreset::P1440, "2560 x 1440"},
    {RenderResolutionPreset::P2160, "3840 x 2160"},
};

enum class WindowPresentationMode : uint8_t {
    Windowed = 0,
    BorderlessWindow,
    Fullscreen,
};

inline constexpr TEXT::EnumName<WindowPresentationMode> kWindowPresentationModeNames[] = {
    {WindowPresentationMode::Windowed, "Windowed"},
    {WindowPresentationMode::BorderlessWindow, "Borderless"},
    {WindowPresentationMode::Fullscreen, "Fullscreen"},
};

enum class GeometryPipelineMode : uint8_t {
    MeshShader = 0,
    TraditionalVsPs,
    AutoFallback,
};

inline constexpr TEXT::EnumName<GeometryPipelineMode> kGeometryPipelineModeNames[] = {
    {GeometryPipelineMode::MeshShader, "MeshShader"},
    {GeometryPipelineMode::TraditionalVsPs, "TraditionalVsPs"},
    {GeometryPipelineMode::AutoFallback, "AutoFallback"},
};

inline constexpr TEXT::EnumName<GeometryPipelineMode> kGeometryPipelineModeLabels[] = {
    {GeometryPipelineMode::MeshShader, "Mesh Shader"},
    {GeometryPipelineMode::TraditionalVsPs, "VS + PS"},
    {GeometryPipelineMode::AutoFallback, "Auto Fallback"},
};

enum class ForwardShadingCostMode : uint8_t {
    Full = 0,
    AlbedoOnly,
    NoNormalMap,
    NoShadow,
    NoSsao,
    NoMaterialExtras,
};

inline constexpr TEXT::EnumName<ForwardShadingCostMode> kForwardShadingCostModeNames[] = {
    {ForwardShadingCostMode::Full, "Full"},
    {ForwardShadingCostMode::AlbedoOnly, "AlbedoOnly"},
    {ForwardShadingCostMode::NoNormalMap, "NoNormalMap"},
    {ForwardShadingCostMode::NoShadow, "NoShadow"},
    {ForwardShadingCostMode::NoSsao, "NoSsao"},
    {ForwardShadingCostMode::NoMaterialExtras, "NoMaterialExtras"},
};

inline constexpr TEXT::EnumName<ForwardShadingCostMode> kForwardShadingCostModeLabels[] = {
    {ForwardShadingCostMode::Full, "Full"},
    {ForwardShadingCostMode::AlbedoOnly, "Albedo Only"},
    {ForwardShadingCostMode::NoNormalMap, "No Normal Map"},
    {ForwardShadingCostMode::NoShadow, "No Shadow"},
    {ForwardShadingCostMode::NoSsao, "No SSAO"},
    {ForwardShadingCostMode::NoMaterialExtras, "No Material Extras"},
};

enum class LightProbeVolumeSamplingMode : uint8_t {
    FastSmooth = 0,
    FullTrilinear,
    Off,
};

inline constexpr TEXT::EnumName<LightProbeVolumeSamplingMode> kLightProbeVolumeSamplingModeNames[] = {
    {LightProbeVolumeSamplingMode::FastSmooth, "FastSmooth"},
    {LightProbeVolumeSamplingMode::FullTrilinear, "FullTrilinear"},
    {LightProbeVolumeSamplingMode::Off, "Off"},
};

inline constexpr TEXT::EnumName<LightProbeVolumeSamplingMode> kLightProbeVolumeSamplingModeLabels[] = {
    {LightProbeVolumeSamplingMode::FastSmooth, "Fast Smooth"},
    {LightProbeVolumeSamplingMode::FullTrilinear, "Full Trilinear"},
    {LightProbeVolumeSamplingMode::Off, "Off"},
};

enum class RenderAntiAliasingMode : uint8_t {
    Off = 0,
    FXAA,
    TAA,
    DLAA,
    DLSS,
};

inline constexpr TEXT::EnumName<RenderAntiAliasingMode> kRenderAntiAliasingModeNames[] = {
    {RenderAntiAliasingMode::Off, "Off"},   {RenderAntiAliasingMode::FXAA, "FXAA"},
    {RenderAntiAliasingMode::TAA, "TAA"},   {RenderAntiAliasingMode::DLAA, "DLAA"},
    {RenderAntiAliasingMode::DLSS, "DLSS"},
};

enum class DlssQualityMode : uint8_t {
    Quality = 0,
    Balanced,
    Performance,
    UltraPerformance,
};

inline constexpr TEXT::EnumName<DlssQualityMode> kDlssQualityModeNames[] = {
    {DlssQualityMode::Quality, "Quality"},
    {DlssQualityMode::Balanced, "Balanced"},
    {DlssQualityMode::Performance, "Performance"},
    {DlssQualityMode::UltraPerformance, "UltraPerformance"},
};

inline constexpr TEXT::EnumName<DlssQualityMode> kDlssQualityModeLabels[] = {
    {DlssQualityMode::Quality, "Quality"},
    {DlssQualityMode::Balanced, "Balanced"},
    {DlssQualityMode::Performance, "Performance"},
    {DlssQualityMode::UltraPerformance, "Ultra Performance"},
};

enum class RenderFrameGenerationMode : uint8_t {
    Off = 0,
    Dlss,
};

inline constexpr TEXT::EnumName<RenderFrameGenerationMode> kRenderFrameGenerationModeNames[] = {
    {RenderFrameGenerationMode::Off, "Off"},
    {RenderFrameGenerationMode::Dlss, "DLSS"},
};

inline constexpr TEXT::EnumName<RenderFrameGenerationMode> kRenderFrameGenerationModeLabels[] = {
    {RenderFrameGenerationMode::Off, "Off"},
    {RenderFrameGenerationMode::Dlss, "DLSS Frame Generation"},
};

enum class VolumetricLightingQuality : uint8_t {
    Low = 0,
    Balanced,
    High,
};

inline constexpr TEXT::EnumName<VolumetricLightingQuality> kVolumetricLightingQualityNames[] = {
    {VolumetricLightingQuality::Low, "Low"},
    {VolumetricLightingQuality::Balanced, "Balanced"},
    {VolumetricLightingQuality::High, "High"},
};

enum class DynamicGiQuality : uint8_t { Off = 0, Low, Balanced, High };

inline constexpr TEXT::EnumName<DynamicGiQuality> kDynamicGiQualityNames[] = {
    {DynamicGiQuality::Off, "Off"},
    {DynamicGiQuality::Low, "Low"},
    {DynamicGiQuality::Balanced, "Balanced"},
    {DynamicGiQuality::High, "High"},
};

enum class LocalShadowQuality : uint8_t {
    Off = 0,
    Low,
    Balanced,
    High,
};

inline constexpr TEXT::EnumName<LocalShadowQuality> kLocalShadowQualityNames[] = {
    {LocalShadowQuality::Off, "Off"},
    {LocalShadowQuality::Low, "Low"},
    {LocalShadowQuality::Balanced, "Balanced"},
    {LocalShadowQuality::High, "High"},
};

// 体積雲の追跡の画質（解像度・歩数・光源方向のサンプル数・届く距離）。
enum class VolumetricCloudQuality : uint8_t {
    Low = 0,
    Balanced,
    High,
};

inline constexpr TEXT::EnumName<VolumetricCloudQuality> kVolumetricCloudQualityNames[] = {
    {VolumetricCloudQuality::Low, "Low"},
    {VolumetricCloudQuality::Balanced, "Balanced"},
    {VolumetricCloudQuality::High, "High"},
};

enum class DepthOfFieldQuality : uint8_t {
    Off = 0,
    Low,
    Medium,
    High,
};

inline constexpr TEXT::EnumName<DepthOfFieldQuality> kDepthOfFieldQualityNames[] = {
    {DepthOfFieldQuality::Off, "Off"},
    {DepthOfFieldQuality::Low, "Low"},
    {DepthOfFieldQuality::Medium, "Medium"},
    {DepthOfFieldQuality::High, "High"},
};

// 運動ブラーの標本数（Low 8、Medium 12、High 16）。Off はカメラの設定に関わらず掛けない。
enum class MotionBlurQuality : uint8_t {
    Off = 0,
    Low,
    Medium,
    High,
};

inline constexpr TEXT::EnumName<MotionBlurQuality> kMotionBlurQualityNames[] = {
    {MotionBlurQuality::Off, "Off"},
    {MotionBlurQuality::Low, "Low"},
    {MotionBlurQuality::Medium, "Medium"},
    {MotionBlurQuality::High, "High"},
};

struct RenderResolution {
    int width = 0;
    int height = 0;
};

// 太陽方向光の影の技術設定。影の距離と強さは環境プロファイルが持つ。
struct DirectionalShadowQualitySettings {
    uint32_t resolution = 2048;
    uint32_t cascadeCount = 3;
    float cascadeSplitLambda = 0.65f;
    float cascadeBlend = 0.10f;
    float nearPlane = 0.1f;
    float farPlane = 80.0f;
    float depthBias = 0.001f;
    float normalBias = 0.02f;
    bool pcfEnabled = true;
    float pcfRadius = 1.0f;
    // 半影：太陽の角半径 × この倍率で、遮蔽者との距離に応じて核を広げる（PCSS）。0 で固定半径の PCF。
    // 実際の太陽（角半径 0.27°）では 1m 先の半影が 1cm 弱でほぼ硬い。2 倍で 3m の壁の影の縁が少し和らぐ程度。
    // 8 倍では壁の影が 1m 近い幅の不自然な濃淡になり、遠くから見ると全体がぼやけた。
    float penumbraScale = 2.0f;
    // 光線追跡の影：場面の深度 prepass の後に画素ごとの太陽の可視率を求め、不透明面のカスケード影と接触影を置き換える
    // （半透明・地形はカスケードのまま）。対応 GPU と実行時の加速構造が要り、無ければ静かにカスケードへ戻る。
    // 有効時は場面の深度 prepass も走る。標本数は画素あたりの光線数（TAA が均す）。
    bool rayTraced = false;
    uint32_t rayTracedSamples = 2;
    bool stabilize = true;

    bool operator==(const DirectionalShadowQualitySettings&) const = default;
};

struct RenderQualitySettings {
    RenderResolutionPreset sceneResolution = RenderResolutionPreset::P1080;
    RenderResolutionPreset windowSize = RenderResolutionPreset::P720;
    WindowPresentationMode windowMode = WindowPresentationMode::Windowed;
    GeometryPipelineMode geometryPipeline = GeometryPipelineMode::MeshShader;
    ForwardShadingCostMode forwardCostMode = ForwardShadingCostMode::Full;
    LightProbeVolumeSamplingMode lightProbeVolumeSampling = LightProbeVolumeSamplingMode::FastSmooth;
    float viewportScale = 1.0f;
    bool vSync = false;
    // DLAA by default when Streamline DLSS is available; falls back to the
    // declared temporal policy when DLSS is unavailable.
    RenderAntiAliasingMode antiAliasingMode = RenderAntiAliasingMode::DLAA;
    DlssQualityMode dlssQualityMode = DlssQualityMode::Quality;
    RenderFrameGenerationMode frameGenerationMode = RenderFrameGenerationMode::Off;
    uint8_t frameGenerationMultiplier = 2;
    VolumetricLightingQuality volumetricLightingQuality = VolumetricLightingQuality::Balanced;
    DepthOfFieldQuality depthOfFieldQuality = DepthOfFieldQuality::Medium;
    MotionBlurQuality motionBlurQuality = MotionBlurQuality::Medium;
    VolumetricCloudQuality volumetricCloudQuality = VolumetricCloudQuality::Balanced;
    // 光追（inline ray query）が無い GPU では動的 GI が Unsupported になり、空の光へ戻る。
    DynamicGiQuality dynamicGiQuality = DynamicGiQuality::Balanced;
    LocalShadowQuality localShadowQuality = LocalShadowQuality::Balanced;
    DirectionalShadowQualitySettings directionalShadow{};
    float taaHistoryWeight = 0.92f;
    float taaVarianceClipGamma = 1.25f;
    float taaDepthRejection = 0.0025f;
    float taaLuminanceRejection = 0.55f;
    float taaSharpness = 0.25f;
    // Optional scene-depth prepass for overdraw-heavy scenes.
    // Geometry-bound scenes should leave this off.
    bool sceneDepthPrepass = false;
    // 光線追跡の反射：画面空間の反射が外した所（画面外・裏面）を光線で埋める。対応 GPU と加速構造が要る。
    // 機械の性能に依る選択なので画面の設定（環境配置）ではなく画質に置く。
    bool rayTracedReflections = false;

    bool operator==(const RenderQualitySettings&) const = default;
};

// Runtime render-quality edits are applied between frames.  The
// classification keeps the presentation owner from treating every scalar
// tweak as a GPU-resource rebuild while still making resolution, temporal
// backend, and Streamline changes explicit.
struct RenderQualityRuntimeChangeSet {
    bool sceneResolutionChanged = false;
    bool temporalBackendChanged = false;
    bool streamlineStateChanged = false;
    bool frameGenerationChanged = false;
    bool presentationChanged = false;

    bool RequiresGpuDrain() const noexcept {
        return sceneResolutionChanged || temporalBackendChanged || streamlineStateChanged || frameGenerationChanged;
    }
    bool RequiresTemporalReset() const noexcept { return sceneResolutionChanged || temporalBackendChanged; }
    bool RequiresStreamlineReset() const noexcept { return streamlineStateChanged || frameGenerationChanged; }
};

RenderQualitySettings& GetRenderQualitySettings();
RenderQualitySettings NormalizeRenderQualitySettings(RenderQualitySettings settings);
RenderQualityRuntimeChangeSet ClassifyRenderQualityRuntimeChanges(const RenderQualitySettings& before,
                                                                  const RenderQualitySettings& after);
void SetRenderQualitySettings(const RenderQualitySettings& settings);

// 表示名（保存名と違うものは表示名の表、同じものは保存名の表から引く）。
const char* RenderResolutionPresetLabel(RenderResolutionPreset preset);
const char* WindowPresentationModeLabel(WindowPresentationMode mode);
const char* GeometryPipelineModeLabel(GeometryPipelineMode mode);
const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode);
const char* LightProbeVolumeSamplingModeLabel(LightProbeVolumeSamplingMode mode);
const char* RenderAntiAliasingModeLabel(RenderAntiAliasingMode mode);
const char* DlssQualityModeLabel(DlssQualityMode mode);
const char* RenderFrameGenerationModeLabel(RenderFrameGenerationMode mode);
const char* VolumetricLightingQualityLabel(VolumetricLightingQuality quality);
const char* VolumetricCloudQualityLabel(VolumetricCloudQuality quality);
const char* DynamicGiQualityLabel(DynamicGiQuality quality);
const char* LocalShadowQualityLabel(LocalShadowQuality quality);

bool IsTemporalAntiAliasingMode(RenderAntiAliasingMode mode);
bool UsesTemporalJitter(RenderAntiAliasingMode mode);
bool IsDlaaAntiAliasingMode(RenderAntiAliasingMode mode);
bool IsDlssAntiAliasingMode(RenderAntiAliasingMode mode);
bool IsFxaaAntiAliasingMode(RenderAntiAliasingMode mode);
bool IsAntiAliasingModeAvailable(RenderAntiAliasingMode mode);
bool IsFrameGenerationModeAvailable(RenderFrameGenerationMode mode);

bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset);
RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset);
RenderResolution ResolveSceneOutputResolution(const RenderQualitySettings& settings, int viewportWidth,
                                              int viewportHeight);

} // namespace HIKARI::RENDER3D
