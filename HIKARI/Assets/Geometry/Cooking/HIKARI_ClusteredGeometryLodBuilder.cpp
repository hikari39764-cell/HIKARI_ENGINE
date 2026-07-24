#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Tools/Geometry/HIKARI_MeshLodGenerator.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    bool ShouldBuildReducedLods(uint32_t flags) {
        return !RENDER3D::CLUSTER::HasFlag(
            flags,
            ClusterSurfaceFlags::Transparent);
    }

    float ResolveLodTargetRatio(uint32_t flags, uint32_t lodIndex, const ClusteredGeometryCookSettings& settings) {
        float ratio = settings.lod4TriangleRatio;
        switch (lodIndex) {
        case 1u:
            ratio = settings.lod1TriangleRatio;
            break;
        case 2u:
            ratio = settings.lod2TriangleRatio;
            break;
        case 3u:
            ratio = settings.lod3TriangleRatio;
            break;
        default:
            break;
        }
        ratio = (std::max)(0.05f, (std::min)(ratio, 0.95f));
        if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask)) {
            // 両面/マスク材質は輪郭破綻が目立つので、通常 opaque より控えめに落とす。
            ratio = std::sqrt(ratio);
        } else if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided)) {
            ratio = ratio * 0.85f + std::sqrt(ratio) * 0.15f;
        }
        return ratio;
    }

    float ResolveLodTargetError(uint32_t flags, uint32_t lodIndex, const ClusteredGeometryCookSettings& settings) {
        float error = settings.lod4TargetError;
        switch (lodIndex) {
        case 1u:
            error = settings.lod1TargetError;
            break;
        case 2u:
            error = settings.lod2TargetError;
            break;
        case 3u:
            error = settings.lod3TargetError;
            break;
        default:
            break;
        }
        error = (std::max)(0.0001f, (std::min)(error, 0.08f));
        if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask)) {
            error *= 0.65f;
        } else if (RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided)) {
            error *= 0.85f;
        }
        return error;
    }

    float ResolveLodMinScreenRadius(uint32_t lodIndex, const ClusteredGeometryCookSettings& settings) {
        if (lodIndex == 0u) {
            return (std::max)(settings.lod0MinScreenRadius, settings.lod1MinScreenRadius);
        }
        if (lodIndex == 1u) {
            return (std::max)(settings.lod1MinScreenRadius, settings.lod2MinScreenRadius);
        }
        if (lodIndex == 2u) {
            return (std::max)(settings.lod2MinScreenRadius, settings.lod3MinScreenRadius);
        }
        if (lodIndex == 3u) {
            return (std::max)(settings.lod3MinScreenRadius, settings.lod4MinScreenRadius);
        }
        return (std::max)(0.0f, settings.lod4MinScreenRadius);
    }

    bool BuildReducedSurfaceLod(
        const SurfaceCookInput& source,
        uint32_t lodIndex,
        uint32_t previousTriangleCount,
        const ClusteredGeometryCookSettings& settings,
        bool lockSectionBorders,
        SurfaceLodResult& outResult) {

        outResult = {};
        if (!settings.buildSurfaceLods ||
            settings.maxSurfaceLodCount <= lodIndex ||
            !ShouldBuildReducedLods(source.flags)) {
            return false;
        }

        const uint32_t sourceTriangleCount = CountSurfaceTriangles(source);
        if (sourceTriangleCount < 96u || previousTriangleCount < 48u) {
            return false;
        }

        const float targetRatio = ResolveLodTargetRatio(source.flags, lodIndex, settings);
        const uint32_t minimumUsefulReduction =
            (std::max)(1u, static_cast<uint32_t>(
                std::floor(static_cast<float>(previousTriangleCount) * 0.92f)));

        TOOLS::GEOMETRY::MeshLodGeneratorSettings lodSettings{};
        lodSettings.lodIndex = lodIndex;
        lodSettings.targetTriangleRatio = targetRatio;
        lodSettings.targetError = ResolveLodTargetError(source.flags, lodIndex, settings);
        const bool permissiveOpaqueLod = ShouldUsePermissiveOpaqueLods(source.flags);
        // 通常 opaque は seam 越しの簡略化を許可し、section 境界だけは裂け防止で固定する。
        lodSettings.preserveAttributes = true;
        lodSettings.lockOpenBorders = lockSectionBorders || !permissiveOpaqueLod;
        lodSettings.optimizeVertexCache = true;
        lodSettings.allowAttributeSeamCollapse = permissiveOpaqueLod && !lockSectionBorders;
        lodSettings.protectGeometricBorders = permissiveOpaqueLod || lockSectionBorders;
        lodSettings.pruneIsolatedComponents = false;

        TOOLS::GEOMETRY::MeshLodGeneratorResult lodResult{};
        if (!TOOLS::GEOMETRY::GenerateClusterSurfaceLod(
                source.vertices,
                source.indices,
                lodSettings,
                lodResult)) {
            return false;
        }

        SurfaceCookInput lodWork = source;
        lodWork.vertices = std::move(lodResult.vertices);
        lodWork.indices = std::move(lodResult.indices);

        const uint32_t lodTriangleCount = CountSurfaceTriangles(lodWork);
        if (lodTriangleCount == 0u || lodTriangleCount >= minimumUsefulReduction) {
            return false;
        }

        outResult.work = std::move(lodWork);
        outResult.geometricError = lodResult.geometricError;
        return outResult.geometricError >= 0.0f;
    }


} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
