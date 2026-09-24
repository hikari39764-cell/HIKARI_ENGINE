#include "Render3D/Resources/Internal/HIKARI_ClusterGeometrySourceIdentity.h"

#include <cstring>
#include <type_traits>
#include <vector>

#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

namespace HIKARI::RENDER3D::INTERNAL {

namespace {
template <typename Value>
bool PackedValueVectorEqual(const std::vector<Value>& left, const std::vector<Value>& right) noexcept {
    static_assert(std::is_trivially_copyable_v<Value>);
    return left.size() == right.size() &&
           (left.empty() || std::memcmp(left.data(), right.data(), left.size() * sizeof(Value)) == 0);
}
} // namespace

std::string NormalizeClusterGeometryPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::string normalized = PATHS::MakeAbsoluteNormalized(path).generic_string();
#if defined(_WIN32)
    TEXT::ToLowerAsciiInPlace(normalized);
#endif
    return normalized;
}

std::string BuildClusterGeometrySourceKeyFromNormalizedPath(const std::string& normalizedPath) {
    return normalizedPath.empty() ? std::string{} : "cluster-geometry:" + normalizedPath;
}

ClusterGeometryFileStamp ReadClusterGeometryFileStamp(const std::filesystem::path& path) {
    std::error_code ec{};
    ClusterGeometryFileStamp stamp{};
    stamp.writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return {};
    }
    stamp.fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return {};
    }
    stamp.available = true;
    return stamp;
}

bool ClusterGeometryPackedContentEqual(const CLUSTER::ClusterGeometryPackedBytes& left,
                                       const CLUSTER::ClusterGeometryPackedBytes& right) noexcept {
    static_assert(std::is_trivially_copyable_v<CLUSTER::ClusterGeometryPackedLayout>);
    return left.metadataByteSize == right.metadataByteSize && left.geometryBytes == right.geometryBytes &&
           left.metadataBytes == right.metadataBytes &&
           std::memcmp(&left.layout, &right.layout, sizeof(left.layout)) == 0 &&
           PackedValueVectorEqual(left.surfaceRanges, right.surfaceRanges) &&
           PackedValueVectorEqual(left.surfaceLodRanges, right.surfaceLodRanges) &&
           PackedValueVectorEqual(left.surfaceSections, right.surfaceSections);
}

} // namespace HIKARI::RENDER3D::INTERNAL

namespace HIKARI::RENDER3D {

std::string BuildClusterGeometryResourceSourceKey(const std::filesystem::path& hcmeshPath) {
    const std::string normalizedPath = INTERNAL::NormalizeClusterGeometryPath(hcmeshPath);
    return INTERNAL::BuildClusterGeometrySourceKeyFromNormalizedPath(normalizedPath);
}

} // namespace HIKARI::RENDER3D
