#pragma once

#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <string>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {

struct AmbientLight {
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 0.25f;
    bool useSkyColor = false;
    float skyBlend = 1.0f;
};

struct DirectionalLight {
    bool enabled = true;
    MATH::Vec3 direction{0.4f, -1.0f, -0.6f};
    float intensity = 1.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
};

struct DirectionalShadowSettings {
    bool enabled = true;
    uint32_t resolution = 2048;
    uint32_t cascadeCount = 3;
    float cascadeSplitLambda = 0.65f;
    float cascadeBlend = 0.10f;
    float nearPlane = 0.1f;
    float farPlane = 80.0f;
    float depthBias = 0.001f;
    float normalBias = 0.02f;
    // 様式化した陰影の演出用。Standard PBR は実際の可視性を直接使う。
    float strength = 0.75f;
    bool pcfEnabled = true;
    float pcfRadius = 1.0f;
    bool stabilize = true;
    bool showDebugTexture = false;
    float shadowDistance = 30.0f;
    bool showDebugFrustum = false;
};

struct PointLight {
    bool enabled = true;
    MATH::Vec3 position{0.0f, 2.0f, 0.0f};
    float range = 5.0f;
    float sourceRadius = 0.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    int priority = 0;
    RENDER3D::LIGHTING::LocalLightFalloff distanceFalloff = RENDER3D::LIGHTING::LocalLightFalloff::Range;
};

struct SpotLight {
    bool enabled = true;
    MATH::Vec3 position{0.0f, 3.0f, 0.0f};
    float range = 8.0f;
    float sourceRadius = 0.0f;
    MATH::Vec3 direction{0.0f, -1.0f, 0.0f};
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    int priority = 0;
    RENDER3D::LIGHTING::LocalLightFalloff distanceFalloff = RENDER3D::LIGHTING::LocalLightFalloff::Range;
};

enum class SkyMode {
    None = 0,
    Gradient,
    Cubemap,
    Texture2D,
    Atmosphere,
};

// Relative solar illumination keeps the legacy directional intensity as
// the authoritative scene-light value while allowing an atmosphere to
// calibrate sky radiance without introducing a second sun owner.
struct AtmosphereSettings {
    bool enabled = true;
    float planetRadius = 6360000.0f;
    float atmosphereHeight = 80000.0f;
    float rayleighScaleHeight = 8000.0f;
    float mieScaleHeight = 1200.0f;
    MATH::Vec3 rayleighScattering{5.8e-6f, 13.5e-6f, 33.1e-6f};
    MATH::Vec3 mieScattering{3.996e-6f, 3.996e-6f, 3.996e-6f};
    MATH::Vec3 ozoneAbsorption{6.5e-7f, 1.881e-6f, 8.5e-8f};
    float ozoneCenterHeight = 25000.0f;
    float ozoneWidth = 15000.0f;
    float mieAnisotropy = 0.76f;
    float sunIlluminance = 1.0f;
    float sunExposure = 0.0f;
    float groundAlbedo = 0.1f;
    float aerialPerspectiveStrength = 1.0f;
    float sunAngularRadius = 0.004675f;
    uint32_t transmittanceLutWidth = 256u;
    uint32_t transmittanceLutHeight = 64u;
    uint32_t multiScatteringLutSize = 32u;
    uint32_t skyViewLutWidth = 192u;
    uint32_t skyViewLutHeight = 108u;
    bool preferLut = true;
};

// Post owns the glare pass; Environment owns only the persistent policy
// and the renderer-facing values that identify the same solar source.
struct SunGlareSettings {
    bool enabled = false;
    float intensity = 0.15f;
    float haloRadius = 0.08f;
    float haloFalloff = 3.0f;
    float streakIntensity = 0.0f;
    float streakLength = 0.15f;
    float occlusionRadius = 0.012f;
    float edgeFade = 0.15f;
    float maxRadiance = 4.0f;
    // Depth-backed radial shafts are an artistic readability supplement
    // to the froxel volume, not a second fog/light owner.  A zero default
    // preserves the appearance of scenes authored before this contract.
    float shaftIntensity = 0.0f;
    float shaftLength = 0.55f;
    float shaftDecay = 0.94f;
};

struct SkySettings {
    bool enabled = true;
    SkyMode mode = SkyMode::Cubemap;
    std::string skyAsset = "DefaultSky";
    // Optional equirectangular foreground layer for Texture2D skies.  The
    // base panorama remains owned by skyAsset; this layer is composed by
    // its own render stage so the volumetric cloud owner stays separate.
    bool panoramaCloudEnabled = false;
    std::string panoramaCloudAsset{};
    MATH::Vec2 panoramaCloudScrollSpeed{0.0015f, 0.0f};
    float panoramaCloudOpacity = 1.0f;
    float scale = 1.0f;
    float yaw = 0.0f;
    // Scene-linear sky radiance multiplier. Kept as "exposure" in serialized
    // data for compatibility; this is not the camera/display exposure.
    float exposure = 1.0f;
    MATH::Vec3 tint{1.0f, 1.0f, 1.0f};
    bool followCamera = true;
    MATH::Vec3 zenithColor{0.08f, 0.22f, 0.55f};
    MATH::Vec3 horizonColor{0.65f, 0.78f, 0.95f};
    MATH::Vec3 groundColor{0.08f, 0.08f, 0.10f};
    float horizonPower = 1.5f;
    bool showSunDisk = true;
    float sunDiskIntensity = 0.0f;
    float sunDiskSize = 0.04f;
    float ambientFromSky = 1.0f;
    float reflectionIntensity = 1.0f;
    bool showDebugTexture = false;
};

struct StylizedCloudSettings {
    bool enabled = false;
    bool physicallyBasedLighting = true;
    float anisotropy = 0.7f;
    float coverage = 0.46f;
    float density = 1.10f;
    float altitude = 180.0f;
    float thickness = 90.0f;
    float scale = 140.0f;
    MATH::Vec2 windDirection{1.0f, 0.22f};
    float windSpeed = 6.0f;
    float shapeDetail = 0.55f;
    float softness = 0.28f;
    float lightAbsorption = 0.72f;
    float silverLining = 0.42f;
    float horizonFade = 0.62f;
    MATH::Vec3 brightColor{1.0f, 0.985f, 0.94f};
    MATH::Vec3 shadowColor{0.48f, 0.67f, 0.86f};
};

enum class ReflectionProbeInfluenceShape {
    Sphere,
    Box,
};

enum class ReflectionProbeProjectionShape {
    Infinite,
    Box,
};

struct ReflectionProbeSettings {
    bool enabled = false;
    std::string sourceCubemapAsset{};
    MATH::Vec3 position{0.0f, 2.0f, 0.0f};
    float radius = 8.0f;
    float intensity = 1.0f;
    // Influence は probe の有効範囲、Projection は cubemap の視差補正に使う。
    ReflectionProbeInfluenceShape influenceShape = ReflectionProbeInfluenceShape::Sphere;
    MATH::Vec3 influenceBoxCenter{0.0f, 2.0f, 0.0f};
    MATH::Vec3 influenceBoxSize{8.0f, 4.0f, 8.0f};
    ReflectionProbeProjectionShape projectionShape = ReflectionProbeProjectionShape::Infinite;
    MATH::Vec3 projectionBoxCenter{0.0f, 2.0f, 0.0f};
    MATH::Vec3 projectionBoxSize{8.0f, 4.0f, 8.0f};
    float blendDistance = 1.0f;
    int priority = 0;
};

enum class SsaoMode {
    Off = 0,
    Reference,
    OptimizedHigh,
    Balanced,
};

struct AmbientOcclusionSettings {
    bool enabled = false;
    SsaoMode mode = SsaoMode::Balanced;
    float radius = 0.6f;
    float bias = 0.025f;
    float strength = 1.0f;
    float power = 1.5f;
    float diffuseStrength = 1.0f;
    float specularStrength = 0.5f;
    uint32_t sampleCount = 16;
    uint32_t blurIterations = 1;
    bool editorViewportSuppressed = false;
};

enum class ScreenSpaceReflectionQuality {
    Off = 0,
    Low,
    Medium,
    High,
};

inline uint32_t ResolveScreenSpaceReflectionMaxSteps(ScreenSpaceReflectionQuality quality) {
    switch (quality) {
    case ScreenSpaceReflectionQuality::Low: return 24u;
    case ScreenSpaceReflectionQuality::High: return 64u;
    case ScreenSpaceReflectionQuality::Medium: return 40u;
    case ScreenSpaceReflectionQuality::Off:
    default: return 0u;
    }
}

struct ScreenSpaceReflectionSettings {
    bool enabled = true;
    ScreenSpaceReflectionQuality quality = ScreenSpaceReflectionQuality::Medium;
    float intensity = 0.75f;
    float maxDistance = 24.0f;
    float thickness = 0.0025f;
    float maxRoughness = 0.72f;
    float edgeFade = 0.08f;
    float stride = 0.18f;
    bool rayTracingFallback = false;
};

struct SurfaceLightmapSettings {
    bool enabled = true;
};

struct BloomSettings {
    bool enabled = true;
    float threshold = 1.0f;
    float intensity = 0.2f;
    float radius = 1.0f;
    uint32_t downsampleCount = 3;
};

struct FogSettings {
    bool enabled = false;
    bool volumetric = true;
    MATH::Vec3 color{0.55f, 0.65f, 0.75f};
    float density = 0.02f;
    float startDistance = 10.0f;
    float endDistance = 80.0f;
    float heightFalloff = 0.0f;
    float baseHeight = 0.0f;
    float anisotropy = 0.2f;
    float temporalWeight = 0.9f;
    float scatteringIntensity = 1.0f;
    float ambientScatteringStrength = 1.0f;
    float sunShaftStrength = 1.0f;
    bool useSkyHorizonColor = false;
};

struct ToneMappingSettings {
    // enabled は Tone Curve のみを制御する。
    // Exposure と Display Transfer は SDR 出力時に常に適用する。
    bool enabled = true;
    RENDER3D::COLOR::ExposureMode exposureMode = RENDER3D::COLOR::ExposureMode::Manual;
    float exposure = 1.0f; // Manual exposure and automatic-mode seed.
    RENDER3D::COLOR::AutomaticExposureSettings automaticExposure{};
    float whiteBalanceTemperature = 6500.0f;
    float whiteBalanceTint = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float gamma = 2.2f; // SDR display transfer approximation.
    int mode = 2;       // 0=None, 1=Reinhard, 2=ACES fitted
};

// 全画面後処理は後処理プロファイル資産を参照する（エンジン内蔵 ID かプロジェクト資産の GUID）。
struct ScenePostSettings {
    bool enabled = false;
    std::string profileId{};
};

struct SceneEnvironment {
    AmbientLight ambient{};
    DirectionalLight directional{};
    DirectionalShadowSettings directionalShadow{};
    std::vector<PointLight> pointLights{};
    std::vector<SpotLight> spotLights{};
    SkySettings sky{};
    AtmosphereSettings atmosphere{};
    SunGlareSettings sunGlare{};
    StylizedCloudSettings clouds{};
    ReflectionProbeSettings reflectionProbe{};
    AmbientOcclusionSettings ambientOcclusion{};
    SurfaceLightmapSettings surfaceLightmaps{};
    ScreenSpaceReflectionSettings screenSpaceReflections{};
    BloomSettings bloom{};
    FogSettings fog{};
    ToneMappingSettings toneMapping{};
    bool showLightDebug = true;
    bool showSkyDebugInfo = false;
    ScenePostSettings post{};
};

} // namespace HIKARI
