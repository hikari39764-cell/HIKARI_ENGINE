#include "Render3D/Cluster/HIKARI_ClusteredGeometryManager.h"

#include <algorithm>

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        ClusteredGeometryManager gManager{};
    }

    const ASSETS::GEOMETRY::HcmeshFileInfo* ClusteredGeometryManager::LoadOrGet(const std::filesystem::path& path) {
        ++stats_.requestCount;
        const std::string key = MakeKey(path);
        auto it = assets_.find(key);
        if (it != assets_.end()) {
            ++stats_.hitCount;
            return it->second && it->second->valid ? it->second.get() : nullptr;
        }

        ++stats_.missCount;
        auto info = std::make_unique<ASSETS::GEOMETRY::HcmeshFileInfo>();
        std::string message{};
        if (!ASSETS::GEOMETRY::InspectHcmeshFile(path, *info, message)) {
            lastMessage_ = message;
            assets_[key] = std::move(info);
            RebuildStats();
            return nullptr;
        }

        lastMessage_ = message;
        const ASSETS::GEOMETRY::HcmeshFileInfo* raw = info.get();
        assets_[key] = std::move(info);
        RebuildStats();
        return raw;
    }

    void ClusteredGeometryManager::Invalidate(const std::filesystem::path& path) {
        assets_.erase(MakeKey(path));
        RebuildStats();
    }

    void ClusteredGeometryManager::Clear() {
        assets_.clear();
        lastMessage_.clear();
        stats_ = {};
    }

    const ClusteredGeometryManagerStats& ClusteredGeometryManager::GetStats() const {
        return stats_;
    }

    const std::string& ClusteredGeometryManager::GetLastMessage() const {
        return lastMessage_;
    }

    std::string ClusteredGeometryManager::MakeKey(const std::filesystem::path& path) const {
        return path.lexically_normal().generic_string();
    }

    void ClusteredGeometryManager::RebuildStats() {
        const uint32_t requests = stats_.requestCount;
        const uint32_t hits = stats_.hitCount;
        const uint32_t misses = stats_.missCount;
        stats_ = {};
        stats_.requestCount = requests;
        stats_.hitCount = hits;
        stats_.missCount = misses;

        for (const auto& pair : assets_) {
            const ASSETS::GEOMETRY::HcmeshFileInfo* info = pair.second.get();
            if (info == nullptr || !info->valid) {
                ++stats_.invalidAssetCount;
                continue;
            }

            ++stats_.validAssetCount;
            stats_.surfaceCount += info->surfaceCount;
            stats_.surfaceLodRangeCount += info->surfaceLodRangeCount;
            stats_.surfaceSectionCount += info->surfaceSectionCount;
            stats_.clusterCount += info->clusterCount;
            stats_.pageCount += info->pageCount;
            stats_.totalTriangleCount += info->totalTriangleCount;
            stats_.totalVertexCount += info->totalVertexCount;
            stats_.maxVerticesPerCluster = (std::max)(
                stats_.maxVerticesPerCluster,
                info->maxVerticesPerCluster);
        }
    }

    ClusteredGeometryManager& GetClusteredGeometryManager() {
        return gManager;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
