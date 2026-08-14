#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::SAND {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float ClampPositive(float value, float minimum, float maximum) noexcept {
            return std::clamp(
                FiniteOr(value, minimum),
                minimum,
                maximum);
        }

        float Hash21(MATH::Vec2 value) noexcept {
            const MATH::Vec3 p3{
                value.x * 0.1031f - std::floor(value.x * 0.1031f),
                value.y * 0.1031f - std::floor(value.y * 0.1031f),
                value.x * 0.1031f - std::floor(value.x * 0.1031f)
            };
            const float dot3 =
                p3.x * (p3.y + 33.33f) +
                p3.y * (p3.z + 33.33f) +
                p3.z * (p3.x + 33.33f);
            const float mixed = dot3 * 1.61803398875f;
            return mixed - std::floor(mixed);
        }

        float ValueNoise(MATH::Vec2 point) noexcept {
            const MATH::Vec2 cell{
                std::floor(point.x),
                std::floor(point.y)
            };
            const MATH::Vec2 local = point - cell;
            const MATH::Vec2 fade{
                local.x * local.x * (3.0f - 2.0f * local.x),
                local.y * local.y * (3.0f - 2.0f * local.y)
            };
            const float a = Hash21(cell);
            const float b = Hash21(cell + MATH::Vec2{ 1.0f, 0.0f });
            const float c = Hash21(cell + MATH::Vec2{ 0.0f, 1.0f });
            const float d = Hash21(cell + MATH::Vec2{ 1.0f, 1.0f });
            return std::lerp(
                std::lerp(a, b, fade.x),
                std::lerp(c, d, fade.x),
                fade.y);
        }
    }

    const char* ToString(SandSurfaceStyle style) noexcept {
        switch (style) {
        case SandSurfaceStyle::AnimeDry:
        default:
            return "anime_dry";
        }
    }

    SandSurfaceStyle ParseSandSurfaceStyle(
        std::string_view value,
        SandSurfaceStyle fallback) noexcept {

        if (value == "anime_dry" || value == "AnimeDry") {
            return SandSurfaceStyle::AnimeDry;
        }
        return fallback;
    }

    SandSurfaceSettings SanitizeSandSurfaceSettings(
        SandSurfaceSettings settings) noexcept {

        settings.gridResolution = std::clamp(
            settings.gridResolution,
            kSandSurfaceMinGridResolution,
            kSandSurfaceMaxGridResolution);
        settings.heightScale = ClampPositive(
            settings.heightScale, 0.0f, 2.0f);
        settings.maxDepth = ClampPositive(
            settings.maxDepth, 0.01f, 2.0f);
        settings.stampRadius = ClampPositive(
            settings.stampRadius, 0.02f, 10.0f);
        settings.rimStrength = std::clamp(
            FiniteOr(settings.rimStrength, 0.28f),
            0.0f,
            2.0f);
        settings.refillPerSecond = ClampPositive(
            settings.refillPerSecond, 0.0f, 10.0f);
        settings.diffusionPerSecond = ClampPositive(
            settings.diffusionPerSecond, 0.0f, 100.0f);

        settings.lightTint = {
            std::clamp(FiniteOr(settings.lightTint.x, 1.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(settings.lightTint.y, 1.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(settings.lightTint.z, 1.0f), 0.0f, 1.0f),
        };
        settings.darkTint = {
            std::clamp(FiniteOr(settings.darkTint.x, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(settings.darkTint.y, 0.0f), 0.0f, 1.0f),
            std::clamp(FiniteOr(settings.darkTint.z, 0.0f), 0.0f, 1.0f),
        };
        settings.sparkleStrength = ClampPositive(
            settings.sparkleStrength, 0.0f, 2.0f);
        settings.rippleAmount = ClampPositive(
            settings.rippleAmount, 0.0f, 3.0f);
        settings.pitDarkening = std::clamp(
            FiniteOr(settings.pitDarkening, 0.32f),
            0.0f,
            1.0f);
        return settings;
    }

    bool SandSurfaceSettingsEqual(
        const SandSurfaceSettings& lhs,
        const SandSurfaceSettings& rhs) noexcept {

        return lhs.style == rhs.style &&
            lhs.gridResolution == rhs.gridResolution &&
            lhs.heightScale == rhs.heightScale &&
            lhs.maxDepth == rhs.maxDepth &&
            lhs.stampRadius == rhs.stampRadius &&
            lhs.rimStrength == rhs.rimStrength &&
            lhs.refillPerSecond == rhs.refillPerSecond &&
            lhs.diffusionPerSecond == rhs.diffusionPerSecond &&
            lhs.lightTint.x == rhs.lightTint.x &&
            lhs.lightTint.y == rhs.lightTint.y &&
            lhs.lightTint.z == rhs.lightTint.z &&
            lhs.darkTint.x == rhs.darkTint.x &&
            lhs.darkTint.y == rhs.darkTint.y &&
            lhs.darkTint.z == rhs.darkTint.z &&
            lhs.sparkleStrength == rhs.sparkleStrength &&
            lhs.rippleAmount == rhs.rippleAmount &&
            lhs.pitDarkening == rhs.pitDarkening;
    }

    float EvaluateSandSurfaceBaseHeight(
        const MATH::Vec2& localXZ,
        float rippleAmount,
        float heightScale) noexcept {

        const float safeRipple =
            std::clamp(FiniteOr(rippleAmount, 0.45f), 0.0f, 3.0f);
        const float safeScale =
            std::clamp(FiniteOr(heightScale, 0.32f), 0.0f, 2.0f);

        // 3 オクターブの value noise で低周波の砂丘を作る。
        // 値と周期は HLSL 側の HikariEvaluateSandBaseHeight と一致させる。
        const float dune =
            ValueNoise({
                localXZ.x * 0.045f,
                localXZ.y * 0.045f }) * 0.72f +
            ValueNoise({
                localXZ.x * 0.11f + 7.3f,
                localXZ.y * 0.11f + 2.9f }) * 0.20f +
            ValueNoise({
                localXZ.x * 0.27f + 2.1f,
                localXZ.y * 0.27f + 9.4f }) * 0.08f;
        const float ripple =
            std::sin(localXZ.x * 0.85f +
                1.7f * std::sin(localXZ.y * 0.21f)) *
                0.012f * safeRipple +
            std::sin(localXZ.y * 0.63f -
                0.9f * std::sin(localXZ.x * 0.17f)) *
                0.009f * safeRipple;
        return (dune * 0.16f + ripple) * safeScale;
    }

} // namespace HIKARI::SAND
