#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::WATER {

    constexpr uint32_t kWaterSurfaceSchemaVersion = 5u;
    constexpr std::size_t kMaxWaterWaves = 4u;

    enum class WaterSurfacePreset : uint8_t {
        AnimeSummer = 0,
        RealisticClearSummer = 1,
        AnimeSparklingSummer = 2,
        RealisticSpectralSummer = 3,
    };

    enum class WaterSurfaceGeometryMode : uint8_t {
        FiniteAuthored = 0,
        CameraCenteredOcean = 1,
    };

    const char* ToString(WaterSurfacePreset preset) noexcept;
    const char* ToString(WaterSurfaceGeometryMode mode) noexcept;
    WaterSurfacePreset ParseWaterSurfacePreset(
        std::string_view value,
        WaterSurfacePreset fallback =
            WaterSurfacePreset::AnimeSummer) noexcept;
    WaterSurfaceGeometryMode ParseWaterSurfaceGeometryMode(
        std::string_view value,
        WaterSurfaceGeometryMode fallback =
            WaterSurfaceGeometryMode::FiniteAuthored) noexcept;

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
        WaterSurfaceGeometryMode geometryMode =
            WaterSurfaceGeometryMode::FiniteAuthored;
        float timeScale = 0.78f;
        float choppiness = 0.58f;
        float microWaveStrength = 0.32f;

        // absorptionDistance 進んだ光に残る RGB 透過率。
        MATH::Vec3 depthTransmittance{ 0.04f, 0.30f, 0.68f };
        MATH::Vec3 scatteringColor{ 0.035f, 0.58f, 0.78f };
        float shallowClearDepth = 0.20f;
        float shallowBlendDepth = 2.80f;
        float shallowGraphicWeight = 0.14f;
        MATH::Vec3 foamColor{ 1.0f, 0.97f, 0.82f };
        float foamAmount = 0.58f;
        float absorptionDistance = 8.0f;
        float surfaceRoughness = 0.18f;
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
            { 18.0f, 0.25f, 11.5f, 0.72f },
            { 74.0f, 0.13f, 6.4f, 0.58f },
            { -39.0f, 0.07f, 3.8f, 0.44f },
            { 132.0f, 0.035f, 2.2f, 0.30f },
        }};
    };

    struct WaterSurfaceEvaluation {
        MATH::Vec3 positionLocal{};
        MATH::Vec3 normalLocal{ 0.0f, 1.0f, 0.0f };
        MATH::Vec3 velocityLocal{};
        float compression = 0.0f;
        float horizontalJacobian = 1.0f;
    };

    WaterSurfaceSettings MakeWaterSurfacePresetSettings(
        WaterSurfacePreset preset) noexcept;
    WaterSurfaceSettings SanitizeWaterSurfaceSettings(
        WaterSurfaceSettings settings) noexcept;

    // GPU Gems のサンプリング条件に従い、頂点間隔の 2 倍以下の波を
    // 幾何変形から除外し、4 倍までを滑らかにフェードインする。
    WaterSurfaceSettings ResolveWaterSurfaceSettingsForMesh(
        const WaterSurfaceSettings& settings,
        float maximumVertexSpacing) noexcept;

    WaterSurfaceEvaluation EvaluateWaterSurface(
        const WaterSurfaceSettings& settings,
        const MATH::Vec2& sourceLocalXZ,
        float timeSeconds) noexcept;

    // 入力は変形後の局所 XZ。固定反復で元の波座標を逆算するため、
    // 浮力と画面上の水平 Gerstner 変形が一致する。
    WaterSurfaceEvaluation EvaluateWaterSurfaceAtHorizontalPosition(
        const WaterSurfaceSettings& settings,
        const MATH::Vec2& deformedLocalXZ,
        float timeSeconds,
        MATH::Vec2* outSourceLocalXZ = nullptr) noexcept;

    float CalculateWaterMaximumHorizontalDisplacement(
        const WaterSurfaceSettings& settings) noexcept;

    bool WaterSurfaceSettingsEqual(
        const WaterSurfaceSettings& lhs,
        const WaterSurfaceSettings& rhs) noexcept;

} // namespace HIKARI::WATER
