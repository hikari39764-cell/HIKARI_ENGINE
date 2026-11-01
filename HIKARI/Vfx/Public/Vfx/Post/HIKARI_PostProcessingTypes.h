#pragma once

#include <cstdint>

namespace HIKARI {
struct ToneMappingSettings;
}

namespace HIKARI::POST {

struct CommonParams;

struct BloomDebugStats {
    bool enabled = false;
    bool initialized = false;
    bool failed = false;
    uint32_t passCount = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    float threshold = 0.0f;
    float intensity = 0.0f;
    float radius = 0.0f;
    uint32_t downsampleCount = 0;
};

struct FxaaSettings {
    float edgeThreshold = 0.125f;
    float edgeThresholdMin = 0.0312f;
    float subpixelQuality = 0.75f;
};

// プレビューと本編が同じ shader パラメータ契約を使う。
// residualExposure は Pre-Exposure 済みの HDR に残りを掛ける E/P。gradingLutSize は t1 に束ねた色調整の
// LUT の一辺（0 なら引かない）。
void WriteToneMappingParameters(CommonParams& parameters, const ToneMappingSettings& settings, float residualExposure,
                                float radialCurvature = 0.0f, float radialStrength = 0.0f,
                                uint32_t gradingLutSize = 0u);
void WriteFxaaParameters(CommonParams& parameters, int width, int height, const FxaaSettings& settings = {});

// Per-view, scene-linear solar glare input.  The render pipeline derives
// this from the authoritative directional sun and the active atmosphere;
// Post only owns occlusion and HDR composition.
struct SunGlareFrame {
    bool enabled = false;
    float sunUvX = 0.5f;
    float sunUvY = 0.5f;
    float coreRadius = 0.003f;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float intensity = 0.0f;
    float haloRadius = 0.08f;
    float haloFalloff = 3.0f;
    float streakIntensity = 0.0f;
    float streakLength = 0.15f;
    float occlusionRadius = 0.05f;
    float edgeFade = 0.15f;
    float maxRadiance = 4.0f;
    float shaftIntensity = 0.0f;
    float shaftLength = 0.55f;
    float shaftDecay = 0.94f;
    float ghostIntensity = 0.0f;
};

struct SunGlareDebugStats {
    bool requested = false;
    bool active = false;
    bool initialized = false;
    bool failed = false;
    int textureWidth = 0;
    int textureHeight = 0;
    float sunUvX = 0.5f;
    float sunUvY = 0.5f;
    float intensity = 0.0f;
};

} // namespace HIKARI::POST
