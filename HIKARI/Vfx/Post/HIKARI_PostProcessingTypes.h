#pragma once

#include <cstdint>

namespace HIKARI::POST {

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

} // namespace HIKARI::POST
