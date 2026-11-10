#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::WATER {

constexpr std::size_t kMaxWaterWaves = 4u;

enum class WaterSurfacePreset : uint8_t {
    AnimeSummer = 0,
    RealisticClearSummer = 1,
};

enum class WaterSurfaceGeometryMode : uint8_t {
    FiniteAuthored = 0,
    CameraCenteredOcean = 1,
};

// シーンに保存する名前。
inline constexpr TEXT::EnumName<WaterSurfacePreset> kWaterSurfacePresetNames[] = {
    {WaterSurfacePreset::AnimeSummer, "anime_summer"},
    {WaterSurfacePreset::RealisticClearSummer, "realistic_clear_summer"},
};
inline constexpr TEXT::EnumName<WaterSurfaceGeometryMode> kWaterSurfaceGeometryModeNames[] = {
    {WaterSurfaceGeometryMode::FiniteAuthored, "finite_authored"},
    {WaterSurfaceGeometryMode::CameraCenteredOcean, "camera_centered_ocean"},
};

struct WaterGerstnerWave {
    float directionDeg = 0.0f;
    float amplitude = 0.25f;
    float wavelength = 10.0f;
    float steepness = 0.6f;
};

// Water は外観と波形だけを所有する。平面サイズと分割数は
// ProceduralMeshComponent が唯一の所有者になる。
struct WaterSurfaceSettings {
    WaterSurfacePreset preset = WaterSurfacePreset::AnimeSummer;
    WaterSurfaceGeometryMode geometryMode = WaterSurfaceGeometryMode::FiniteAuthored;
    float timeScale = 0.78f;
    float choppiness = 0.58f;
    float microWaveStrength = 0.32f;

    // absorptionDistance 進んだ光に残る RGB 透過率。
    MATH::Vec3 depthTransmittance{0.04f, 0.30f, 0.68f};
    MATH::Vec3 scatteringColor{0.035f, 0.58f, 0.78f};
    float shallowClearDepth = 0.20f;
    float shallowBlendDepth = 2.80f;
    float shallowGraphicWeight = 0.14f;
    MATH::Vec3 foamColor{1.0f, 0.97f, 0.82f};
    float foamAmount = 0.58f;
    float absorptionDistance = 8.0f;
    float surfaceRoughness = 0.18f;
    // Scene-linear contribution controls shared by both formal presets.
    float directSunStrength = 0.82f;
    float transmissionStrength = 0.92f;
    float shadowStrength = 0.72f;
    float reflectionStrength = 1.0f;
    float sunGlintStrength = 0.0f;
    float glintDensity = 0.0f;
    float glintFlowSpeed = 0.0f;
    float ribbonWidth = 0.02f;
    // 解析短波/毛細波の wind-aligned slope field。旧 preset は 0 で無効。
    float windDirectionDeg = 0.0f;
    float windSpeed = 0.0f;
    float shortWaveStrength = 0.0f;
    float capillaryStrength = 0.0f;

    std::array<WaterGerstnerWave, kMaxWaterWaves> waves{{
        {18.0f, 0.25f, 11.5f, 0.72f},
        {74.0f, 0.13f, 6.4f, 0.58f},
        {-39.0f, 0.07f, 3.8f, 0.44f},
        {132.0f, 0.035f, 2.2f, 0.30f},
    }};
};

struct WaterSurfaceEvaluation {
    MATH::Vec3 positionLocal{};
    MATH::Vec3 normalLocal{0.0f, 1.0f, 0.0f};
    MATH::Vec3 velocityLocal{};
    float compression = 0.0f;
    float horizontalJacobian = 1.0f;
};

WaterSurfaceSettings MakeWaterSurfacePresetSettings(WaterSurfacePreset preset) noexcept;
WaterSurfaceSettings SanitizeWaterSurfaceSettings(WaterSurfaceSettings settings) noexcept;

// GPU Gems のサンプリング条件に従い、頂点間隔の 2 倍以下の波を
// 幾何変形から除外し、4 倍までを滑らかにフェードインする。
WaterSurfaceSettings ResolveWaterSurfaceSettingsForMesh(const WaterSurfaceSettings& settings,
                                                        float maximumVertexSpacing) noexcept;

// CameraCenteredOcean is an infinite world-horizontal surface. Tilted or
// sheared authored planes use the finite route; both render and query
// owners must consume this same resolved settings snapshot.
// 海の格子の歪め方（着色器の HIKARI_OCEAN_NEAR_CELL_METERS / HIKARI_OCEAN_MIN_GRADE_SHARE と同じ）。
// 中心の一番細かい格は等間隔の格 × OceanGradeShare。
inline constexpr float kOceanNearCellMeters = 0.5f;
inline constexpr float kOceanMinimumGradeShare = 0.01f;
float OceanGradeShare(float uniformCell) noexcept;

WaterSurfaceSettings ResolveWaterSurfaceRuntimeSettings(const WaterSurfaceSettings& settings,
                                                        float maximumVertexSpacing, const MATH::Mat4& world) noexcept;

WaterSurfaceEvaluation EvaluateWaterSurface(const WaterSurfaceSettings& settings, const MATH::Vec2& sourceLocalXZ,
                                            float timeSeconds) noexcept;

// 入力は変形後の局所 XZ。固定反復で元の波座標を逆算するため、
// 浮力と画面上の水平 Gerstner 変形が一致する。
WaterSurfaceEvaluation EvaluateWaterSurfaceAtHorizontalPosition(const WaterSurfaceSettings& settings,
                                                                const MATH::Vec2& deformedLocalXZ, float timeSeconds,
                                                                MATH::Vec2* outSourceLocalXZ = nullptr) noexcept;

float CalculateWaterMaximumHorizontalDisplacement(const WaterSurfaceSettings& settings) noexcept;

bool WaterSurfaceSettingsEqual(const WaterSurfaceSettings& lhs, const WaterSurfaceSettings& rhs) noexcept;

} // namespace HIKARI::WATER
