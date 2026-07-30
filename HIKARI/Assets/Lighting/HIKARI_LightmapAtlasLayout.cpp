#include "Assets/Lighting/HIKARI_LightmapAtlasLayout.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "Assets/Lighting/Internal/HIKARI_LightmapAtlasPacking.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::LIGHTING {

    namespace {

        bool IsPowerOfTwo(uint32_t value) {
            return value > 0u &&
                (value & (value - 1u)) == 0u;
        }

    } // namespace

    bool BuildLightmapAtlasLayout(
        const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
        const LightmapAtlasSettings& settings,
        LightmapAtlasLayout& outLayout) {

        outLayout = {};
        outLayout.requestedTexelsPerWorldUnit =
            settings.targetTexelsPerWorldUnit;
        if (surfaces.empty()) {
            outLayout.message =
                "Lightmap atlas has no eligible surfaces.";
            return false;
        }
        if (!MATH::IsFinite(
                settings.targetTexelsPerWorldUnit) ||
            settings.targetTexelsPerWorldUnit <= 0.0f ||
            !IsPowerOfTwo(settings.minimumResolution) ||
            !IsPowerOfTwo(settings.maximumResolution) ||
            settings.maximumResolution <
                settings.minimumResolution ||
            settings.guardTexels == 0u ||
            settings.alignmentTexels == 0u) {
            outLayout.message =
                "Lightmap atlas settings are invalid.";
            return false;
        }

        std::string error{};
        if (INTERNAL::FindSmallestLightmapAtlasAtDensity(
                surfaces,
                settings,
                settings.targetTexelsPerWorldUnit,
                outLayout.resolution,
                outLayout.allocations,
                error)) {
            outLayout.resolvedTexelsPerWorldUnit =
                settings.targetTexelsPerWorldUnit;
        } else {
            std::vector<LightmapAtlasAllocation>
                minimumAllocations{};
            if (!INTERNAL::TryBuildLightmapAtlasAtResolution(
                    surfaces,
                    settings,
                    0.0f,
                    settings.maximumResolution,
                    minimumAllocations,
                    error)) {
                outLayout.message =
                    "Lightmap atlas minimum surface extents do "
                    "not fit the configured maximum resolution.";
                return false;
            }

            float lower = 0.0f;
            float upper =
                settings.targetTexelsPerWorldUnit;
            for (uint32_t iteration = 0u;
                 iteration < 24u;
                 ++iteration) {
                const float candidate =
                    (lower + upper) * 0.5f;
                std::vector<LightmapAtlasAllocation>
                    candidateAllocations{};
                if (INTERNAL::
                        TryBuildLightmapAtlasAtResolution(
                            surfaces,
                            settings,
                            candidate,
                            settings.maximumResolution,
                            candidateAllocations,
                            error)) {
                    lower = candidate;
                } else {
                    upper = candidate;
                }
            }

            if (!INTERNAL::
                    FindSmallestLightmapAtlasAtDensity(
                        surfaces,
                        settings,
                        lower,
                        outLayout.resolution,
                        outLayout.allocations,
                        error)) {
                outLayout.message =
                    "Lightmap atlas failed after resolving its "
                    "texel density.";
                return false;
            }
            outLayout.resolvedTexelsPerWorldUnit = lower;
            outLayout.densityReduced = true;
        }

        std::ostringstream message{};
        message
            << outLayout.allocations.size()
            << " surface(s), "
            << outLayout.resolution
            << " atlas, density "
            << outLayout.resolvedTexelsPerWorldUnit
            << " texels/world-unit";
        if (outLayout.densityReduced) {
            message
                << " (requested "
                << outLayout.requestedTexelsPerWorldUnit
                << ", uniformly reduced)";
        }
        message << '.';
        outLayout.message = message.str();
        return true;
    }

} // namespace HIKARI::ASSETS::LIGHTING
