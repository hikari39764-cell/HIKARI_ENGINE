#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::SKYRENDERER {

struct SkyRendererDebugState {
    bool initialized = false;
    bool lastRenderSubmitted = false;
    bool skyAssetFound = false;
    bool cubemapLoaded = false;
    bool usingFallback = false;
    bool textureValid = false;
    SkyMode mode = SkyMode::None;
    RENDER3D::TextureResourceHandle cubemapResource{};
    RENDER3D::TextureResourceHandle textureResource{};
    int cubemapHandle = -1;
    int textureHandle = -1;
    uint32_t constantFrameSlot = 0;
    uint32_t constantDrawSlot = 0;
    uint32_t constantDrawCountThisFrame = 0;
    uint32_t constantCapacityPerFrame = 0;
    size_t psoCreateCount = 0;
    size_t drawCount = 0;
    size_t constantOverflowCount = 0;
    std::string activeSkyAsset{};
    std::string activeTexturePath{};
};

struct SkyEnvironmentData {
    bool valid = false;
    bool hasCubemap = false;
    bool usingFallback = false;
    SkyMode mode = SkyMode::None;
    RENDER3D::TextureResourceHandle cubemapResource{};
    RENDER3D::TextureResourceHandle textureResource{};
    int cubemapHandle = -1;
    int textureHandle = -1;
    D3D12_GPU_DESCRIPTOR_HANDLE cubemapSrv{};
    MATH::Vec3 tint{1.0f, 1.0f, 1.0f};
    // 空のテクスチャの輝度倍率（大気の空は 1）。
    float exposure = 1.0f;
    // 天空光（拡散）と反射の作画用倍率。
    float ambientFromSky = 0.0f;
    float reflectionIntensity = 0.0f;
    float yaw = 0.0f;
    std::string activeSkyAsset{};
    std::string activeTexturePath{};
    bool atmosphereEnabled = false;
    bool atmosphereLutsReady = false;
    bool atmosphereAnalyticFallback = false;
    MATH::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    MATH::Vec3 sunSourceColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunTransmittance{1.0f, 1.0f, 1.0f};
    // Source radiance drives atmosphere integration.  Observed radiance
    // drives the disk/glare only; directional light intensity remains the
    // sole direct surface-light authority.
    float sunSourceRadiance = 0.0f;
    float sunRadiance = 0.0f;
    ATMOSPHERE::AtmosphereLutConstants atmosphereConstants{};
    float sunAngularRadius = 0.004675f;
    SunGlareSettings sunGlare{};
};

// Narrow renderer-to-post contract. Direction and illuminance are derived
// from SceneEnvironment::frame (the primary celestial light); screen position
// is a probe input, not a second light owner.
struct AtmosphereFrameData {
    bool valid = false;
    bool atmosphereEnabled = false;
    bool lutsReady = false;
    bool usingAnalyticFallback = false;
    MATH::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    MATH::Vec3 sunSourceColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunTransmittance{1.0f, 1.0f, 1.0f};
    float sunSourceRadiance = 0.0f;
    float sunRadiance = 0.0f;
    float sunAngularRadius = 0.004675f;
    MATH::Vec2 screenSunPosition{0.5f, 0.5f};
    bool screenSunVisible = false;
    SunGlareSettings sunGlare{};
};

void Reset();
// Runtime/device teardown hook for the atmosphere owner. Reset() is a
// per-frame debug-state reset and intentionally preserves atmosphere LUTs
// and PSOs. The host must call this before the shared descriptor pool is
// destroyed; it does not tear down the legacy sky renderer resources.
void ShutdownAtmosphereRuntime();
void InvalidateSkyTextureCache();
// drawSky が false なら空を描かず、環境光・大気・IBL の状態だけを更新する（一様な背景で塗る確認用ビュー）。
void Render(const GFX::Context& graphics, const Camera3D& camera, const SceneEnvironment& environment,
            SkyManager& skyManager, bool drawSky = true);
const SkyRendererDebugState& GetDebugState();
const SkyEnvironmentData& GetEnvironmentData();
// 直近の Render のビューの空気遠近の体積 LUT（Pre-Exposure 済みの内散乱光と平均透過率）。
// 奥行きは d = maxDistance * w^2（w は体積の奥行き座標）。大気の空でない時は無効。
struct AerialPerspectiveView {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    float maxDistance = 0.0f;
    bool valid = false;
};
const AerialPerspectiveView& GetAerialPerspectiveView();
// 直近の Render の天空 LUT（絶対輝度、HikariAtmosphereSkyViewUv の座標）。大気の空でない時は 0。
D3D12_GPU_DESCRIPTOR_HANDLE GetSkyViewLutSrv();
// 空が与える平均の環境光の輝度 [nits] の CPU 近似（雲・霧・光線追跡の環境光項）。
MATH::Vec3 EstimateSkyAmbientRadiance(const SceneEnvironment& environment);
// 場面を照らす光の量（自動露出の入射式の手がかり）。視点の高さ cameraAltitude [m] での水平面の照度 = 雲を抜けた
// 直射 + 雲の下の空。雲の陰の境より上（雲海の上）では晴れた空の日照。
RENDER3D::COLOR::ExposureIllumination EstimateExposureIllumination(const SceneEnvironment& environment,
                                                                   float cameraAltitude);
AtmosphereFrameData BuildAtmosphereFrameData(const SceneEnvironment& environment);
AtmosphereFrameData BuildAtmosphereFrameData(const SceneEnvironment& environment, const Camera3D& camera);

} // namespace HIKARI::SKYRENDERER
