#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

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

    const char* ToString(SandSurfaceBrushMode mode) noexcept {
        switch (mode) {
        case SandSurfaceBrushMode::Raise:
            return "raise";
        case SandSurfaceBrushMode::Lower:
            return "lower";
        case SandSurfaceBrushMode::Smooth:
            return "smooth";
        case SandSurfaceBrushMode::Flatten:
        default:
            return "flatten";
        }
    }

    SandSurfaceBrushMode ParseSandSurfaceBrushMode(
        std::string_view value,
        SandSurfaceBrushMode fallback) noexcept {

        if (value == "raise" || value == "Raise") {
            return SandSurfaceBrushMode::Raise;
        }
        if (value == "lower" || value == "Lower") {
            return SandSurfaceBrushMode::Lower;
        }
        if (value == "smooth" || value == "Smooth") {
            return SandSurfaceBrushMode::Smooth;
        }
        if (value == "flatten" || value == "Flatten") {
            return SandSurfaceBrushMode::Flatten;
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
        settings.toonBandCount = std::clamp(
            settings.toonBandCount,
            2u,
            4u);
        settings.toonBandHardness = std::clamp(
            FiniteOr(settings.toonBandHardness, 0.14f),
            0.0f,
            0.5f);
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
            lhs.pitDarkening == rhs.pitDarkening &&
            lhs.toonBandCount == rhs.toonBandCount &&
            lhs.toonBandHardness == rhs.toonBandHardness;
    }

    SandSurfaceBrush SanitizeSandSurfaceBrush(
        SandSurfaceBrush brush) noexcept {

        const uint8_t modeIndex =
            static_cast<uint8_t>(brush.mode);
        brush.mode = modeIndex <=
                static_cast<uint8_t>(SandSurfaceBrushMode::Flatten)
            ? brush.mode
            : SandSurfaceBrushMode::Raise;
        brush.radius = ClampPositive(brush.radius, 0.01f, 100.0f);
        brush.strength = ClampPositive(brush.strength, 0.0f, 10.0f);
        brush.hardness = std::clamp(
            FiniteOr(brush.hardness, 0.5f), 0.0f, 1.0f);
        brush.flattenHeight = std::clamp(
            FiniteOr(brush.flattenHeight, 0.0f), -10.0f, 10.0f);
        return brush;
    }

    SandSurfaceTerrain SanitizeSandSurfaceTerrain(
        SandSurfaceTerrain terrain) noexcept {

        terrain.authoringResolution = std::clamp(
            terrain.authoringResolution,
            kSandSurfaceMinTerrainResolution,
            kSandSurfaceMaxTerrainResolution);
        const uint32_t stride = terrain.authoringResolution + 1u;
        const size_t count =
            static_cast<size_t>(stride) * stride;
        if (terrain.heights.size() != count) {
            terrain.heights.assign(count, 0.0f);
        }
        terrain.brush = SanitizeSandSurfaceBrush(terrain.brush);
        return terrain;
    }

    bool SandSurfaceTerrainEqual(
        const SandSurfaceTerrain& lhs,
        const SandSurfaceTerrain& rhs) noexcept {

        return lhs.authoringResolution == rhs.authoringResolution &&
            lhs.heights == rhs.heights &&
            lhs.brush.mode == rhs.brush.mode &&
            lhs.brush.radius == rhs.brush.radius &&
            lhs.brush.strength == rhs.brush.strength &&
            lhs.brush.hardness == rhs.brush.hardness &&
            lhs.brush.flattenHeight == rhs.brush.flattenHeight;
    }

    float SampleSandSurfaceTerrain(
        const SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) noexcept {

        const uint32_t resolution =
            (std::max)(terrain.authoringResolution, 1u);
        const uint32_t stride = resolution + 1u;
        if (terrain.heights.size() !=
            static_cast<size_t>(stride) * stride) {
            return 0.0f;
        }
        const float halfX = (std::max)(halfExtents.x, 1.0e-3f);
        const float halfZ = (std::max)(halfExtents.y, 1.0e-3f);
        const float u = std::clamp(
            localXZ.x / (2.0f * halfX) + 0.5f, 0.0f, 1.0f);
        const float v = std::clamp(
            localXZ.y / (2.0f * halfZ) + 0.5f, 0.0f, 1.0f);
        const float fu = u * static_cast<float>(resolution);
        const float fv = v * static_cast<float>(resolution);
        const uint32_t x0 = static_cast<uint32_t>(fu);
        const uint32_t y0 = static_cast<uint32_t>(fv);
        const uint32_t x1 = (std::min)(x0 + 1u, resolution);
        const uint32_t y1 = (std::min)(y0 + 1u, resolution);
        const float tx = fu - static_cast<float>(x0);
        const float ty = fv - static_cast<float>(y0);
        const float h00 = terrain.heights[
            static_cast<size_t>(y0) * stride + x0];
        const float h10 = terrain.heights[
            static_cast<size_t>(y0) * stride + x1];
        const float h01 = terrain.heights[
            static_cast<size_t>(y1) * stride + x0];
        const float h11 = terrain.heights[
            static_cast<size_t>(y1) * stride + x1];
        return std::lerp(
            std::lerp(h00, h10, tx),
            std::lerp(h01, h11, tx),
            ty);
    }

    void SculptSandSurfaceTerrain(
        SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) noexcept {

        terrain.authoringResolution = std::clamp(
            terrain.authoringResolution,
            kSandSurfaceMinTerrainResolution,
            kSandSurfaceMaxTerrainResolution);
        const uint32_t resolution = terrain.authoringResolution;
        const uint32_t stride = resolution + 1u;
        const size_t count =
            static_cast<size_t>(stride) * stride;
        if (terrain.heights.size() != count) {
            terrain.heights.assign(count, 0.0f);
        }

        const SandSurfaceBrush brush =
            SanitizeSandSurfaceBrush(terrain.brush);
        const float halfX = (std::max)(halfExtents.x, 1.0e-3f);
        const float halfZ = (std::max)(halfExtents.y, 1.0e-3f);
        const float cellX = (2.0f * halfX) /
            static_cast<float>(resolution);
        const float cellZ = (2.0f * halfZ) /
            static_cast<float>(resolution);
        const float cell = (std::max)(
            (std::min)(cellX, cellZ), 1.0e-4f);
        const float radius = (std::max)(brush.radius, cell);
        const int extent =
            static_cast<int>(radius / cell) + 2;

        const float u = std::clamp(
            localXZ.x / (2.0f * halfX) + 0.5f, 0.0f, 1.0f);
        const float v = std::clamp(
            localXZ.y / (2.0f * halfZ) + 0.5f, 0.0f, 1.0f);
        const int centerX = static_cast<int>(std::lround(
            u * static_cast<float>(resolution)));
        const int centerZ = static_cast<int>(std::lround(
            v * static_cast<float>(resolution)));
        const float hardnessExp = 1.0f + brush.hardness * 7.0f;

        for (int z = centerZ - extent;
            z <= centerZ + extent;
            ++z) {
            for (int x = centerX - extent;
                x <= centerX + extent;
                ++x) {
                if (x < 0 || z < 0 ||
                    x > static_cast<int>(resolution) ||
                    z > static_cast<int>(resolution)) {
                    continue;
                }
                const float dx =
                    (static_cast<float>(x) /
                        static_cast<float>(resolution) - 0.5f) *
                    2.0f * halfX - localXZ.x;
                const float dz =
                    (static_cast<float>(z) /
                        static_cast<float>(resolution) - 0.5f) *
                    2.0f * halfZ - localXZ.y;
                const float distance = std::sqrt(dx * dx + dz * dz);
                const float normalized =
                    std::clamp(distance / radius, 0.0f, 1.0f);
                const float falloff = std::pow(
                    (std::max)(1.0f - normalized, 0.0f),
                    hardnessExp);

                const size_t index =
                    static_cast<size_t>(z) * stride + x;
                float& height = terrain.heights[index];
                switch (brush.mode) {
                case SandSurfaceBrushMode::Raise:
                    height += brush.strength * falloff;
                    break;
                case SandSurfaceBrushMode::Lower:
                    height -= brush.strength * falloff;
                    break;
                case SandSurfaceBrushMode::Smooth: {
                    float neighborSum = 0.0f;
                    uint32_t neighborCount = 0u;
                    const auto accumulate = [&](int nx, int nz) {
                        if (nx >= 0 && nz >= 0 &&
                            nx <= static_cast<int>(resolution) &&
                            nz <= static_cast<int>(resolution)) {
                            neighborSum += terrain.heights[
                                static_cast<size_t>(nz) * stride + nx];
                            ++neighborCount;
                        }
                    };
                    accumulate(x - 1, z);
                    accumulate(x + 1, z);
                    accumulate(x, z - 1);
                    accumulate(x, z + 1);
                    if (neighborCount != 0u) {
                        const float average = neighborSum /
                            static_cast<float>(neighborCount);
                        const float blend = std::clamp(
                            brush.strength, 0.0f, 1.0f) * falloff;
                        height += (average - height) * blend;
                    }
                    break;
                }
                case SandSurfaceBrushMode::Flatten: {
                    const float blend = std::clamp(
                        brush.strength, 0.0f, 1.0f) * falloff;
                    height += (brush.flattenHeight - height) * blend;
                    break;
                }
                }
            }
        }
    }

    void ResetSandSurfaceTerrain(
        SandSurfaceTerrain& terrain) noexcept {

        terrain.authoringResolution = std::clamp(
            terrain.authoringResolution,
            kSandSurfaceMinTerrainResolution,
            kSandSurfaceMaxTerrainResolution);
        const uint32_t stride = terrain.authoringResolution + 1u;
        terrain.heights.assign(
            static_cast<size_t>(stride) * stride,
            0.0f);
    }

    void BakeSandSurfaceTerrain(
        SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        float rippleAmount,
        float heightScale) noexcept {

        (void)rippleAmount;
        terrain.authoringResolution = std::clamp(
            terrain.authoringResolution,
            kSandSurfaceMinTerrainResolution,
            kSandSurfaceMaxTerrainResolution);
        const uint32_t resolution = terrain.authoringResolution;
        const uint32_t stride = resolution + 1u;
        terrain.heights.assign(
            static_cast<size_t>(stride) * stride,
            0.0f);
        const float halfX = (std::max)(halfExtents.x, 1.0e-3f);
        const float halfZ = (std::max)(halfExtents.y, 1.0e-3f);
        for (uint32_t z = 0u; z <= resolution; ++z) {
            for (uint32_t x = 0u; x <= resolution; ++x) {
                const MATH::Vec2 localXZ{
                    (static_cast<float>(x) /
                        static_cast<float>(resolution) - 0.5f) *
                        2.0f * halfX,
                    (static_cast<float>(z) /
                        static_cast<float>(resolution) - 0.5f) *
                        2.0f * halfZ
                };
                terrain.heights[
                    static_cast<size_t>(z) * stride + x] =
                    EvaluateSandSurfaceDuneHeight(
                        localXZ,
                        heightScale);
            }
        }
    }

    namespace {
        constexpr char kSandTerrainBase64Alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string Base64EncodeBytes(
            const uint8_t* data,
            size_t size) {

            std::string out{};
            out.reserve((size + 2u) / 3u * 4u);
            for (size_t i = 0u; i < size; i += 3u) {
                const uint32_t a = data[i];
                const uint32_t b =
                    i + 1u < size ? data[i + 1u] : 0u;
                const uint32_t c =
                    i + 2u < size ? data[i + 2u] : 0u;
                const uint32_t triple =
                    (a << 16u) | (b << 8u) | c;
                out.push_back(
                    kSandTerrainBase64Alphabet[
                        (triple >> 18u) & 0x3Fu]);
                out.push_back(
                    kSandTerrainBase64Alphabet[
                        (triple >> 12u) & 0x3Fu]);
                out.push_back(
                    i + 1u < size
                        ? kSandTerrainBase64Alphabet[
                            (triple >> 6u) & 0x3Fu]
                        : '=');
                out.push_back(
                    i + 2u < size
                        ? kSandTerrainBase64Alphabet[
                            triple & 0x3Fu]
                        : '=');
            }
            return out;
        }

        int Base64DecodeValue(char c) {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            if (c == '+') {
                return 62;
            }
            if (c == '/') {
                return 63;
            }
            return -1;
        }

        bool Base64DecodeBytes(
            std::string_view input,
            std::vector<uint8_t>& out) {

            out.clear();
            if (input.empty() || input.size() % 4u != 0u) {
                return false;
            }
            out.reserve(input.size() / 4u * 3u);
            uint32_t buffer = 0u;
            int bits = 0;
            for (char c : input) {
                if (c == '=') {
                    break;
                }
                const int value = Base64DecodeValue(c);
                if (value < 0) {
                    return false;
                }
                buffer = (buffer << 6u) |
                    static_cast<uint32_t>(value);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>(
                        (buffer >> bits) & 0xFFu));
                }
            }
            return true;
        }
    }

    std::string EncodeSandSurfaceTerrainHeights(
        const SandSurfaceTerrain& terrain) {

        std::vector<uint8_t> bytes{};
        bytes.reserve(
            terrain.heights.size() * sizeof(uint16_t));
        for (float height : terrain.heights) {
            const int32_t quantized = static_cast<int32_t>(
                std::lround(std::clamp(
                    height * 1000.0f,
                    -32768.0f,
                    32767.0f)));
            const uint16_t value = static_cast<uint16_t>(
                static_cast<int16_t>(quantized));
            bytes.push_back(
                static_cast<uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<uint8_t>(
                (value >> 8u) & 0xFFu));
        }
        return Base64EncodeBytes(
            bytes.data(),
            bytes.size());
    }

    bool DecodeSandSurfaceTerrainHeights(
        SandSurfaceTerrain& terrain,
        std::string_view encoded) {

        std::vector<uint8_t> bytes{};
        if (!Base64DecodeBytes(encoded, bytes)) {
            return false;
        }
        const size_t count =
            bytes.size() / sizeof(uint16_t);
        if (count * sizeof(uint16_t) != bytes.size()) {
            return false;
        }
        const uint32_t stride =
            terrain.authoringResolution + 1u;
        if (count !=
            static_cast<size_t>(stride) * stride) {
            terrain.heights.clear();
            return false;
        }
        terrain.heights.resize(count);
        for (size_t i = 0u; i < count; ++i) {
            const uint16_t value = static_cast<uint16_t>(
                bytes[i * 2u] |
                (static_cast<uint16_t>(
                    bytes[i * 2u + 1u]) << 8u));
            terrain.heights[i] =
                static_cast<float>(
                    static_cast<int16_t>(value)) /
                1000.0f;
        }
        return true;
    }

    float EvaluateSandSurfaceRippleHeight(
        const MATH::Vec2& localXZ,
        float rippleAmount,
        float heightScale) noexcept {

        const float safeRipple =
            std::clamp(FiniteOr(rippleAmount, 0.45f), 0.0f, 3.0f);
        const float safeScale =
            std::clamp(FiniteOr(heightScale, 0.32f), 0.0f, 2.0f);
        const float ripple =
            std::sin(localXZ.x * 0.85f +
                1.7f * std::sin(localXZ.y * 0.21f)) *
                0.012f * safeRipple +
            std::sin(localXZ.y * 0.63f -
                0.9f * std::sin(localXZ.x * 0.17f)) *
                0.009f * safeRipple;
        return ripple * safeScale;
    }

    float EvaluateSandSurfaceDuneHeight(
        const MATH::Vec2& localXZ,
        float heightScale) noexcept {

        const float safeScale =
            std::clamp(FiniteOr(heightScale, 0.32f), 0.0f, 2.0f);
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
        return dune * 0.16f * safeScale;
    }

} // namespace HIKARI::SAND
