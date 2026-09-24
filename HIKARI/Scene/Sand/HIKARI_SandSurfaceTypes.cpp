#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Core/Text/HIKARI_EnumNames.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

namespace HIKARI::SAND {

namespace {
float FiniteOr(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }

float ClampPositive(float value, float minimum, float maximum) noexcept {
    return std::clamp(FiniteOr(value, minimum), minimum, maximum);
}

float Hash21(MATH::Vec2 value) noexcept {
    const MATH::Vec3 p3{value.x * 0.1031f - std::floor(value.x * 0.1031f),
                        value.y * 0.1031f - std::floor(value.y * 0.1031f),
                        value.x * 0.1031f - std::floor(value.x * 0.1031f)};
    const float dot3 = p3.x * (p3.y + 33.33f) + p3.y * (p3.z + 33.33f) + p3.z * (p3.x + 33.33f);
    const float mixed = dot3 * 1.61803398875f;
    return mixed - std::floor(mixed);
}

float ValueNoise(MATH::Vec2 point) noexcept {
    const MATH::Vec2 cell{std::floor(point.x), std::floor(point.y)};
    const MATH::Vec2 local = point - cell;
    const MATH::Vec2 fade{local.x * local.x * (3.0f - 2.0f * local.x), local.y * local.y * (3.0f - 2.0f * local.y)};
    const float a = Hash21(cell);
    const float b = Hash21(cell + MATH::Vec2{1.0f, 0.0f});
    const float c = Hash21(cell + MATH::Vec2{0.0f, 1.0f});
    const float d = Hash21(cell + MATH::Vec2{1.0f, 1.0f});
    return std::lerp(std::lerp(a, b, fade.x), std::lerp(c, d, fade.x), fade.y);
}
} // namespace

const char* ToString(SandSurfaceStyle style) noexcept {
    switch (style) {
    case SandSurfaceStyle::AnimeDry:
    default: return "anime_dry";
    }
}

constexpr TEXT::EnumName<SandSurfaceBrushMode> kSandSurfaceBrushModeNames[] = {
    {SandSurfaceBrushMode::Raise, "raise"},   {SandSurfaceBrushMode::Lower, "lower"},
    {SandSurfaceBrushMode::Smooth, "smooth"}, {SandSurfaceBrushMode::Flatten, "flatten"},
    {SandSurfaceBrushMode::Raise, "Raise"},   {SandSurfaceBrushMode::Lower, "Lower"},
    {SandSurfaceBrushMode::Smooth, "Smooth"}, {SandSurfaceBrushMode::Flatten, "Flatten"},
};
const char* ToString(SandSurfaceBrushMode mode) noexcept {
    return TEXT::EnumToName(kSandSurfaceBrushModeNames, mode, "flatten");
}

SandSurfaceBrushMode ParseSandSurfaceBrushMode(std::string_view value, SandSurfaceBrushMode fallback) noexcept {
    return TEXT::ParseEnum(kSandSurfaceBrushModeNames, value, fallback);
}

SandSurfaceSettings SanitizeSandSurfaceSettings(SandSurfaceSettings settings) noexcept {
    settings.gridResolution =
        std::clamp(settings.gridResolution, kSandSurfaceMinGridResolution, kSandSurfaceMaxGridResolution);
    settings.heightScale = ClampPositive(settings.heightScale, 0.0f, 2.0f);
    settings.maxDepth = ClampPositive(settings.maxDepth, 0.01f, 2.0f);
    settings.stampRadius = ClampPositive(settings.stampRadius, 0.02f, 10.0f);
    settings.rimStrength = std::clamp(FiniteOr(settings.rimStrength, 0.32f), 0.0f, 2.0f);
    settings.responseResolutionScale = std::clamp(settings.responseResolutionScale, 1u, 4u);
    settings.restDepthScale = std::clamp(FiniteOr(settings.restDepthScale, 0.36f), 0.0f, 2.0f);
    settings.minimumDepth = ClampPositive(settings.minimumDepth, 0.0f, 2.0f);
    settings.settleStartRatio = std::clamp(FiniteOr(settings.settleStartRatio, 0.72f), 0.0f, 1.0f);
    settings.settleSeconds = ClampPositive(settings.settleSeconds, 0.0f, 10.0f);
    settings.impactDepthScale = ClampPositive(settings.impactDepthScale, 0.0f, 2.0f);
    settings.rimWidthScale = std::clamp(FiniteOr(settings.rimWidthScale, 0.42f), 0.05f, 2.0f);
    settings.refillPerSecond = ClampPositive(settings.refillPerSecond, 0.0f, 10.0f);
    settings.diffusionPerSecond = ClampPositive(settings.diffusionPerSecond, 0.0f, 100.0f);
    settings.plasticity = std::clamp(FiniteOr(settings.plasticity, 0.93f), 0.0f, 1.0f);
    settings.plasticRefillPerSecond = ClampPositive(settings.plasticRefillPerSecond, 0.0f, 10.0f);
    settings.recoveryDelaySeconds = ClampPositive(settings.recoveryDelaySeconds, 0.0f, 60.0f);
    settings.edgeFalloff = ClampPositive(settings.edgeFalloff, 0.0f, 1000.0f);
    settings.edgeDepth = std::clamp(FiniteOr(settings.edgeDepth, 1.5f), 0.0f, 100.0f);

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
    settings.sparkleStrength = ClampPositive(settings.sparkleStrength, 0.0f, 2.0f);
    settings.rippleAmount = ClampPositive(settings.rippleAmount, 0.0f, 3.0f);
    settings.pitDarkening = std::clamp(FiniteOr(settings.pitDarkening, 0.32f), 0.0f, 1.0f);
    settings.toonBandCount = std::clamp(settings.toonBandCount, 2u, 4u);
    settings.toonBandHardness = std::clamp(FiniteOr(settings.toonBandHardness, 0.14f), 0.0f, 0.5f);
    return settings;
}

bool SandSurfaceSettingsEqual(const SandSurfaceSettings& lhs, const SandSurfaceSettings& rhs) noexcept {
    return lhs.style == rhs.style && lhs.presetId == rhs.presetId && lhs.deformationEnabled == rhs.deformationEnabled &&
           lhs.gridResolution == rhs.gridResolution && lhs.heightScale == rhs.heightScale &&
           lhs.maxDepth == rhs.maxDepth && lhs.stampRadius == rhs.stampRadius && lhs.rimStrength == rhs.rimStrength &&
           lhs.responseResolutionScale == rhs.responseResolutionScale && lhs.restDepthScale == rhs.restDepthScale &&
           lhs.minimumDepth == rhs.minimumDepth && lhs.settleStartRatio == rhs.settleStartRatio &&
           lhs.settleSeconds == rhs.settleSeconds && lhs.impactDepthScale == rhs.impactDepthScale &&
           lhs.rimWidthScale == rhs.rimWidthScale && lhs.refillPerSecond == rhs.refillPerSecond &&
           lhs.diffusionPerSecond == rhs.diffusionPerSecond && lhs.plasticity == rhs.plasticity &&
           lhs.plasticRefillPerSecond == rhs.plasticRefillPerSecond &&
           lhs.recoveryDelaySeconds == rhs.recoveryDelaySeconds && lhs.edgeFalloff == rhs.edgeFalloff &&
           lhs.edgeDepth == rhs.edgeDepth && lhs.lightTint.x == rhs.lightTint.x && lhs.lightTint.y == rhs.lightTint.y &&
           lhs.lightTint.z == rhs.lightTint.z && lhs.darkTint.x == rhs.darkTint.x && lhs.darkTint.y == rhs.darkTint.y &&
           lhs.darkTint.z == rhs.darkTint.z && lhs.sparkleStrength == rhs.sparkleStrength &&
           lhs.rippleAmount == rhs.rippleAmount && lhs.pitDarkening == rhs.pitDarkening &&
           lhs.toonBandCount == rhs.toonBandCount && lhs.toonBandHardness == rhs.toonBandHardness;
}

float EvaluateSandSurfaceEffectiveHeight(float baseHeight, float coverage, float edgeDepth,
                                         float fallbackDepth) noexcept {
    const float edge = std::clamp(FiniteOr(coverage, 0.0f), 0.0f, 1.0f);
    const float depth = edgeDepth > 1.0e-4f ? edgeDepth : (std::max)(fallbackDepth, 0.0f);
    const float smooth = edge * edge * (3.0f - 2.0f * edge);
    return baseHeight - depth * (1.0f - smooth);
}

namespace {
constexpr char kSandTerrainBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                              "abcdefghijklmnopqrstuvwxyz"
                                              "0123456789+/";

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

bool Base64DecodeBytes(std::string_view input, std::vector<uint8_t>& out) {
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
        buffer = (buffer << 6u) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFFu));
        }
    }
    return true;
}
} // namespace

bool DecodeSandSurfaceTerrainHeights(SandSurfaceTerrain& terrain, std::string_view encoded) {
    std::vector<uint8_t> bytes{};
    if (!Base64DecodeBytes(encoded, bytes)) {
        return false;
    }
    const size_t count = bytes.size() / sizeof(uint16_t);
    if (count * sizeof(uint16_t) != bytes.size()) {
        return false;
    }
    const uint32_t stride = terrain.authoringResolution + 1u;
    if (count != static_cast<size_t>(stride) * stride) {
        terrain.heights.clear();
        return false;
    }
    terrain.heights.resize(count);
    for (size_t i = 0u; i < count; ++i) {
        const uint16_t value = static_cast<uint16_t>(bytes[i * 2u] | (static_cast<uint16_t>(bytes[i * 2u + 1u]) << 8u));
        terrain.heights[i] = static_cast<float>(static_cast<int16_t>(value)) / 1000.0f;
    }
    return true;
}

} // namespace HIKARI::SAND
