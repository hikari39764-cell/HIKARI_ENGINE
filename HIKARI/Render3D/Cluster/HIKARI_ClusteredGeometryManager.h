#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Assets/Geometry/HIKARI_HcmeshFormat.h"

namespace HIKARI::RENDER3D::CLUSTER {

    struct ClusteredGeometryManagerStats {
        uint32_t requestCount = 0;
        uint32_t hitCount = 0;
        uint32_t missCount = 0;
        uint32_t validAssetCount = 0;
        uint32_t invalidAssetCount = 0;
        uint32_t surfaceCount = 0;
        uint32_t surfaceLodRangeCount = 0;
        uint32_t surfaceSectionCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t maxVerticesPerCluster = 0;
        uint32_t skippedSkinnedPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t unsupportedFeatureCount = 0;
    };

    class ClusteredGeometryManager {
    public:
        const ASSETS::GEOMETRY::HcmeshFileInfo* LoadOrGet(const std::filesystem::path& path);
        void Invalidate(const std::filesystem::path& path);
        void Clear();

        const ClusteredGeometryManagerStats& GetStats() const;
        const std::string& GetLastMessage() const;

    private:
        std::string MakeKey(const std::filesystem::path& path) const;
        void RebuildStats();

        std::unordered_map<std::string, std::unique_ptr<ASSETS::GEOMETRY::HcmeshFileInfo>> assets_{};
        ClusteredGeometryManagerStats stats_{};
        std::string lastMessage_{};
    };

    ClusteredGeometryManager& GetClusteredGeometryManager();

} // namespace HIKARI::RENDER3D::CLUSTER
