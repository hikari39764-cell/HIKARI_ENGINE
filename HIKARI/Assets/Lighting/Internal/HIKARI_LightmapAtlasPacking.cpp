#include "Assets/Lighting/Internal/HIKARI_LightmapAtlasPacking.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::LIGHTING::INTERNAL {

namespace {

constexpr float kMinimumArea = 1.0e-10f;
constexpr float kMinimumUvExtent = 1.0e-6f;

struct PendingAllocation {
    LightmapAtlasSurfaceMetrics metrics{};
    uint32_t interiorWidth = 0u;
    uint32_t interiorHeight = 0u;
    uint32_t outerWidth = 0u;
    uint32_t outerHeight = 0u;
};

struct SkylineNode {
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
};

bool IdentityLess(const LightmapSurfaceIdentity& lhs, const LightmapSurfaceIdentity& rhs) {
    if (lhs.nodeIndex != rhs.nodeIndex) {
        return lhs.nodeIndex < rhs.nodeIndex;
    }
    if (lhs.meshIndex != rhs.meshIndex) {
        return lhs.meshIndex < rhs.meshIndex;
    }
    return lhs.primitiveIndex < rhs.primitiveIndex;
}

uint32_t RoundUp(uint32_t value, uint32_t alignment) {
    if (alignment <= 1u) {
        return value;
    }
    const uint64_t rounded = (static_cast<uint64_t>(value) + alignment - 1u) / alignment * alignment;
    return static_cast<uint32_t>((std::min)(rounded, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t NextPowerOfTwo(uint32_t value) {
    uint32_t result = 1u;
    while (result < value && result <= std::numeric_limits<uint32_t>::max() / 2u) {
        result <<= 1u;
    }
    return result;
}

bool BuildPendingAllocations(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                             const LightmapAtlasSettings& settings, float texelsPerWorldUnit,
                             std::vector<PendingAllocation>& outPending, std::string& outMessage) {
    outPending.clear();
    outPending.reserve(surfaces.size());
    const uint32_t maximumInterior = settings.maximumResolution > settings.guardTexels * 2u
                                         ? settings.maximumResolution - settings.guardTexels * 2u
                                         : 0u;
    if (maximumInterior == 0u) {
        outMessage = "Lightmap atlas guard consumes the entire "
                     "maximum extent.";
        return false;
    }

    for (const LightmapAtlasSurfaceMetrics& metrics : surfaces) {
        const float uvExtentX = metrics.uvMax.x - metrics.uvMin.x;
        const float uvExtentY = metrics.uvMax.y - metrics.uvMin.y;
        if (!metrics.surface.IsValid() || !MATH::IsFinite(metrics.uvMin) || !MATH::IsFinite(metrics.uvMax) ||
            !MATH::IsFinite(metrics.worldArea) || !MATH::IsFinite(metrics.uvArea) || uvExtentX <= kMinimumUvExtent ||
            uvExtentY <= kMinimumUvExtent || metrics.worldArea <= kMinimumArea || metrics.uvArea <= kMinimumArea) {
            outMessage = "Lightmap atlas received invalid surface "
                         "metrics.";
            return false;
        }

        const float worldToUvScale = texelsPerWorldUnit * std::sqrt(metrics.worldArea / metrics.uvArea);
        const auto resolveExtent = [maximumInterior](float uvExtent, float scale, uint32_t minimum) {
            const double requested = std::ceil(static_cast<double>(uvExtent) * static_cast<double>(scale));
            const uint64_t finiteRequested = std::isfinite(requested)
                                                 ? static_cast<uint64_t>((std::max)(1.0, requested))
                                                 : static_cast<uint64_t>(maximumInterior) + 1u;
            return static_cast<uint32_t>(
                (std::min)((std::max)(finiteRequested, static_cast<uint64_t>((std::max)(1u, minimum))),
                           static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
        };

        PendingAllocation pending{};
        pending.metrics = metrics;
        pending.interiorWidth = resolveExtent(uvExtentX, worldToUvScale, metrics.minimumInteriorWidth);
        pending.interiorHeight = resolveExtent(uvExtentY, worldToUvScale, metrics.minimumInteriorHeight);
        if (pending.interiorWidth > maximumInterior || pending.interiorHeight > maximumInterior) {
            outMessage = "A Lightmap surface exceeds the maximum "
                         "atlas extent at the requested density.";
            return false;
        }
        pending.outerWidth = RoundUp(pending.interiorWidth + settings.guardTexels * 2u, settings.alignmentTexels);
        pending.outerHeight = RoundUp(pending.interiorHeight + settings.guardTexels * 2u, settings.alignmentTexels);
        outPending.push_back(std::move(pending));
    }
    return true;
}

bool FindSkylinePosition(const std::vector<SkylineNode>& skyline, size_t nodeIndex, uint32_t rectangleWidth,
                         uint32_t rectangleHeight, uint32_t atlasResolution, uint32_t& outY) {
    const uint32_t x = skyline[nodeIndex].x;
    if (x + rectangleWidth > atlasResolution) {
        return false;
    }
    uint32_t widthLeft = rectangleWidth;
    uint32_t y = skyline[nodeIndex].y;
    for (size_t scan = nodeIndex; widthLeft > 0u; ++scan) {
        if (scan >= skyline.size()) {
            return false;
        }
        y = (std::max)(y, skyline[scan].y);
        if (y + rectangleHeight > atlasResolution) {
            return false;
        }
        if (skyline[scan].width >= widthLeft) {
            break;
        }
        widthLeft -= skyline[scan].width;
    }
    outY = y;
    return true;
}

void AddSkylineLevel(std::vector<SkylineNode>& skyline, size_t nodeIndex, uint32_t x, uint32_t y, uint32_t width,
                     uint32_t height) {
    skyline.insert(skyline.begin() + static_cast<std::ptrdiff_t>(nodeIndex), SkylineNode{x, y + height, width});
    for (size_t index = nodeIndex + 1u; index < skyline.size();) {
        SkylineNode& previous = skyline[index - 1u];
        SkylineNode& current = skyline[index];
        const uint32_t previousEnd = previous.x + previous.width;
        if (current.x >= previousEnd) {
            break;
        }
        const uint32_t overlap = previousEnd - current.x;
        if (overlap >= current.width) {
            skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        current.x += overlap;
        current.width -= overlap;
        break;
    }
    for (size_t index = 1u; index < skyline.size();) {
        if (skyline[index - 1u].y == skyline[index].y) {
            skyline[index - 1u].width += skyline[index].width;
            skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
}

bool TryPack(std::vector<PendingAllocation> pending, const LightmapAtlasSettings& settings, uint32_t atlasResolution,
             std::vector<LightmapAtlasAllocation>& outAllocations) {
    std::sort(pending.begin(), pending.end(), [](const PendingAllocation& lhs, const PendingAllocation& rhs) {
        if (lhs.outerHeight != rhs.outerHeight) {
            return lhs.outerHeight > rhs.outerHeight;
        }
        if (lhs.outerWidth != rhs.outerWidth) {
            return lhs.outerWidth > rhs.outerWidth;
        }
        return IdentityLess(lhs.metrics.surface, rhs.metrics.surface);
    });

    std::vector<SkylineNode> skyline{{0u, 0u, atlasResolution}};
    outAllocations.clear();
    outAllocations.reserve(pending.size());
    for (const PendingAllocation& item : pending) {
        size_t bestNode = std::numeric_limits<size_t>::max();
        uint32_t bestX = std::numeric_limits<uint32_t>::max();
        uint32_t bestY = std::numeric_limits<uint32_t>::max();
        for (size_t nodeIndex = 0u; nodeIndex < skyline.size(); ++nodeIndex) {
            uint32_t y = 0u;
            if (!FindSkylinePosition(skyline, nodeIndex, item.outerWidth, item.outerHeight, atlasResolution, y)) {
                continue;
            }
            const uint32_t x = skyline[nodeIndex].x;
            if (y < bestY || (y == bestY && x < bestX)) {
                bestNode = nodeIndex;
                bestX = x;
                bestY = y;
            }
        }
        if (bestNode == std::numeric_limits<size_t>::max()) {
            outAllocations.clear();
            return false;
        }

        AddSkylineLevel(skyline, bestNode, bestX, bestY, item.outerWidth, item.outerHeight);
        LightmapAtlasAllocation allocation{};
        allocation.surface = item.metrics.surface;
        allocation.outerRect = {bestX, bestY, item.outerWidth, item.outerHeight};
        allocation.interiorRect = {bestX + settings.guardTexels, bestY + settings.guardTexels, item.interiorWidth,
                                   item.interiorHeight};
        const float inverseResolution = 1.0f / static_cast<float>(atlasResolution);
        const float uvExtentX = item.metrics.uvMax.x - item.metrics.uvMin.x;
        const float uvExtentY = item.metrics.uvMax.y - item.metrics.uvMin.y;
        allocation.uvScaleBias = {
            static_cast<float>(item.interiorWidth) * inverseResolution / uvExtentX,
            static_cast<float>(item.interiorHeight) * inverseResolution / uvExtentY,
            static_cast<float>(allocation.interiorRect.x) * inverseResolution -
                item.metrics.uvMin.x * static_cast<float>(item.interiorWidth) * inverseResolution / uvExtentX,
            static_cast<float>(allocation.interiorRect.y) * inverseResolution -
                item.metrics.uvMin.y * static_cast<float>(item.interiorHeight) * inverseResolution / uvExtentY};
        outAllocations.push_back(std::move(allocation));
    }

    std::sort(outAllocations.begin(), outAllocations.end(),
              [](const LightmapAtlasAllocation& lhs, const LightmapAtlasAllocation& rhs) {
                  return IdentityLess(lhs.surface, rhs.surface);
              });
    return true;
}

bool FindSmallestAtlas(const std::vector<PendingAllocation>& pending, const LightmapAtlasSettings& settings,
                       uint32_t& outResolution, std::vector<LightmapAtlasAllocation>& outAllocations) {
    const uint32_t minimumResolution = NextPowerOfTwo((std::max)(1u, settings.minimumResolution));
    const uint32_t maximumResolution = NextPowerOfTwo((std::max)(minimumResolution, settings.maximumResolution));
    for (uint32_t resolution = minimumResolution; resolution <= maximumResolution; resolution <<= 1u) {
        if (TryPack(pending, settings, resolution, outAllocations)) {
            outResolution = resolution;
            return true;
        }
        if (resolution > maximumResolution / 2u) {
            break;
        }
    }
    return false;
}

} // namespace

bool FindSmallestLightmapAtlasAtDensity(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                                        const LightmapAtlasSettings& settings, float texelsPerWorldUnit,
                                        uint32_t& outResolution, std::vector<LightmapAtlasAllocation>& outAllocations,
                                        std::string& outMessage) {
    std::vector<PendingAllocation> pending{};
    return BuildPendingAllocations(surfaces, settings, texelsPerWorldUnit, pending, outMessage) &&
           FindSmallestAtlas(pending, settings, outResolution, outAllocations);
}

bool TryBuildLightmapAtlasAtResolution(const std::vector<LightmapAtlasSurfaceMetrics>& surfaces,
                                       const LightmapAtlasSettings& settings, float texelsPerWorldUnit,
                                       uint32_t resolution, std::vector<LightmapAtlasAllocation>& outAllocations,
                                       std::string& outMessage) {
    std::vector<PendingAllocation> pending{};
    return BuildPendingAllocations(surfaces, settings, texelsPerWorldUnit, pending, outMessage) &&
           TryPack(std::move(pending), settings, resolution, outAllocations);
}

} // namespace HIKARI::ASSETS::LIGHTING::INTERNAL
