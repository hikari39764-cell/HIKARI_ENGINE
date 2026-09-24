#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"

namespace HIKARI::RENDER3D::INTERNAL {

struct ClusterGeometryFileStamp {
    std::filesystem::file_time_type writeTime{};
    uint64_t fileSize = 0;
    bool available = false;
};

std::string NormalizeClusterGeometryPath(const std::filesystem::path& path);
std::string BuildClusterGeometrySourceKeyFromNormalizedPath(const std::string& normalizedPath);
ClusterGeometryFileStamp ReadClusterGeometryFileStamp(const std::filesystem::path& path);
bool ClusterGeometryPackedContentEqual(const CLUSTER::ClusterGeometryPackedBytes& left,
                                       const CLUSTER::ClusterGeometryPackedBytes& right) noexcept;

} // namespace HIKARI::RENDER3D::INTERNAL
