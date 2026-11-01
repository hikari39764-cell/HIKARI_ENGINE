#include "RenderContracts/Lighting/HIKARI_LightmapAtlasLayout.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

#include "RenderContracts/Lighting/Internal/HIKARI_LightmapAtlasPacking.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::LIGHTING {

namespace {

bool IsPowerOfTwo(uint32_t value) { return value > 0u && (value & (value - 1u)) == 0u; }

bool ValidAtlasSettings(const LightmapAtlasSettings& settings) {
    return MATH::IsFinite(settings.targetTexelsPerWorldUnit) && settings.targetTexelsPerWorldUnit > 0 &&
           IsPowerOfTwo(settings.minimumResolution) && IsPowerOfTwo(settings.maximumResolution) &&
           settings.maximumResolution >= settings.minimumResolution &&
           settings.maximumResolution <= kSurfaceLightmapMaximumAtlasResolution && settings.guardTexels > 0 &&
           settings.guardTexels < settings.maximumResolution / 2 && settings.alignmentTexels > 0 &&
           settings.alignmentTexels <= settings.maximumResolution;
}

} // namespace

bool BuildLightmapAtlasLayout(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                              const LightmapAtlasSettings& settings, LightmapAtlasLayout& outLayout) {
    outLayout = {};
    outLayout.requestedTexelsPerWorldUnit = settings.targetTexelsPerWorldUnit;
    if (surfaces.empty()) {
        outLayout.message = "Lightmap atlas has no eligible surfaces.";
        return false;
    }
    if (!ValidAtlasSettings(settings)) {
        outLayout.message = "Lightmap atlas settings are invalid.";
        return false;
    }

    std::string error{};
    if (INTERNAL::FindSmallestLightmapAtlasAtDensity(surfaces, settings, settings.targetTexelsPerWorldUnit,
                                                     outLayout.resolution, outLayout.allocations, error)) {
        outLayout.resolvedTexelsPerWorldUnit = settings.targetTexelsPerWorldUnit;
    } else {
        std::vector<LightmapAtlasAllocation> minimumAllocations{};
        if (!INTERNAL::TryBuildLightmapAtlasAtResolution(surfaces, settings, 0.0f, settings.maximumResolution,
                                                         minimumAllocations, error)) {
            outLayout.message = "Lightmap atlas minimum surface extents do "
                                "not fit the configured maximum resolution.";
            return false;
        }

        float lower = 0.0f;
        float upper = settings.targetTexelsPerWorldUnit;
        for (uint32_t iteration = 0u; iteration < 24u; ++iteration) {
            const float candidate = (lower + upper) * 0.5f;
            std::vector<LightmapAtlasAllocation> candidateAllocations{};
            if (INTERNAL::TryBuildLightmapAtlasAtResolution(surfaces, settings, candidate, settings.maximumResolution,
                                                            candidateAllocations, error)) {
                lower = candidate;
            } else {
                upper = candidate;
            }
        }

        if (!INTERNAL::FindSmallestLightmapAtlasAtDensity(surfaces, settings, lower, outLayout.resolution,
                                                          outLayout.allocations, error)) {
            outLayout.message = "Lightmap atlas failed after resolving its "
                                "texel density.";
            return false;
        }
        outLayout.resolvedTexelsPerWorldUnit = lower;
        outLayout.densityReduced = true;
    }

    std::ostringstream message{};
    message << outLayout.allocations.size() << " surface(s), " << outLayout.resolution << " atlas, density "
            << outLayout.resolvedTexelsPerWorldUnit << " texels/world-unit";
    if (outLayout.densityReduced) {
        message << " (requested " << outLayout.requestedTexelsPerWorldUnit << ", uniformly reduced)";
    }
    message << '.';
    outLayout.message = message.str();
    return true;
}

bool BuildLightmapAtlasPages(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                             const LightmapAtlasSettings& settings, std::vector<LightmapAtlasLayout>& outPages,
                             std::string& outMessage) {
    outPages.clear();
    LightmapAtlasLayout single{};
    if (BuildLightmapAtlasLayout(surfaces, settings, single)) {
        outMessage = single.message;
        outPages.push_back(std::move(single));
        return true;
    }
    outMessage = single.message;
    if (surfaces.empty() || !ValidAtlasSettings(settings))
        return false;

    // 大きい chart から安定順で分割する。各 page の packing は既存 owner を使う。
    auto remaining = surfaces;
    std::stable_sort(remaining.begin(), remaining.end(), [](const auto& a, const auto& b) {
        return uint64_t(a.minimumInteriorWidth) * a.minimumInteriorHeight >
               uint64_t(b.minimumInteriorWidth) * b.minimumInteriorHeight;
    });
    std::vector<LightmapAtlasLayout> pages;
    size_t first = 0;
    while (first < remaining.size()) {
        if (pages.size() >= kSurfaceLightmapMaximumAtlasPages) {
            outMessage =
                "Lightmap exceeds the per-object atlas page limit; split the model into smaller static objects.";
            return false;
        }
        size_t lower = 0, upper = remaining.size() - first;
        while (lower < upper) {
            const size_t count = lower + (upper - lower + 1) / 2;
            const std::vector<LightmapAtlasSurfaceMetrics> candidate(remaining.begin() + first,
                                                                     remaining.begin() + first + count);
            std::vector<LightmapAtlasAllocation> allocations;
            std::string error;
            if (INTERNAL::TryBuildLightmapAtlasAtResolution(candidate, settings, 0.0f, settings.maximumResolution,
                                                            allocations, error))
                lower = count;
            else
                upper = count - 1;
        }
        if (lower == 0) {
            outMessage = "A lightmap surface cannot fit an atlas page while preserving its UV chart padding.";
            return false;
        }
        const std::vector<LightmapAtlasSurfaceMetrics> pageSurfaces(remaining.begin() + first,
                                                                    remaining.begin() + first + lower);
        LightmapAtlasLayout page;
        if (!BuildLightmapAtlasLayout(pageSurfaces, settings, page)) {
            outMessage = page.message;
            return false;
        }
        pages.push_back(std::move(page));
        first += lower;
    }
    outMessage = std::to_string(surfaces.size()) + " surface(s), " + std::to_string(pages.size()) +
                 " atlas pages; UV chart padding preserved.";
    outPages = std::move(pages);
    return true;
}

} // namespace HIKARI::ASSETS::LIGHTING
