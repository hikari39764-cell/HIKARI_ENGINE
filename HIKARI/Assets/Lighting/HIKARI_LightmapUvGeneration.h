#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::LIGHTING {

enum class LightmapUvGenerationMode : uint8_t {
    Preserve,
    GenerateIfMissingOrInvalid,
    ForceRegenerate,
};

struct LightmapUvGenerationSettings {
    LightmapUvGenerationMode mode = LightmapUvGenerationMode::GenerateIfMissingOrInvalid;
    uint32_t resolution = 64u;
    uint32_t padding = 8u;
};

struct LightmapUvGenerationReport {
    uint32_t primitiveCount = 0u;
    uint32_t authoredValidCount = 0u;
    uint32_t missingCount = 0u;
    uint32_t invalidCount = 0u;
    uint32_t generatedCount = 0u;
    uint32_t generationFailedCount = 0u;
    uint32_t skippedUnsupportedCount = 0u;
    bool canceled = false;
    std::vector<std::string> messages{};
};

struct LightmapUvGenerationCallbacks {
    std::function<bool()> isCancellationRequested{};
    std::function<void(uint32_t currentPrimitive, uint32_t primitiveCount, std::string_view primitiveName)>
        reportProgress{};
};

bool PrepareModelLightmapUvs(ModelAsset& model, const LightmapUvGenerationSettings& settings,
                             LightmapUvGenerationReport& outReport,
                             const LightmapUvGenerationCallbacks* callbacks = nullptr);

const char* ToString(LightmapUvGenerationMode mode) noexcept;

} // namespace HIKARI::ASSETS::LIGHTING
