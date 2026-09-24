#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
class World;
}

namespace HIKARI::NAVIGATION {

enum class SynchronizationMode : uint8_t { Immediate, Background };

enum class QueryStatus : uint8_t {
    Unavailable = 0,
    Success = 1,
    Partial = 2,
    Failed = 3,
    BufferTooSmall = 4,
};

struct PathResult {
    QueryStatus status = QueryStatus::Unavailable;
    uint32_t pointCount = 0u;
    float pathLength = 0.0f;
    bool partial = false;
};

// Main-thread diagnostics for runtime validation and performance audits.
// Query calls never mutate these counters or trigger synchronization.
struct NavigationDiagnostics {
    uint64_t synchronizeCalls = 0u;
    uint64_t unchangedStampFastPaths = 0u;
    uint64_t sourceChunkGatherCount = 0u;
    uint64_t fullBuildCount = 0u;
    uint64_t incrementalBuildCount = 0u;
    uint64_t lastKnownGoodRetentionCount = 0u;
    uint32_t lastChangedChunkCount = 0u;
    uint32_t lastDirtyTileCount = 0u;
    uint64_t backgroundBuildCount = 0u;
    uint64_t discardedBuildCount = 0u;
    bool buildPending = false;
};

class NavigationWorldService final {
public:
    NavigationWorldService();
    ~NavigationWorldService();

    NavigationWorldService(const NavigationWorldService&) = delete;
    NavigationWorldService& operator=(const NavigationWorldService&) = delete;

    bool Synchronize(World& world, SynchronizationMode mode = SynchronizationMode::Immediate) noexcept;
    void Clear() noexcept;

    bool ProjectPoint(World& world, const MATH::Vec3& point, const MATH::Vec3& halfExtents,
                      MATH::Vec3& outProjectedPoint) noexcept;

    PathResult FindPath(World& world, const MATH::Vec3& start, const MATH::Vec3& end, const MATH::Vec3& halfExtents,
                        MATH::Vec3* outPoints, uint8_t* outPointFlags, uint32_t pointCapacity) noexcept;

    bool Raycast(World& world, const MATH::Vec3& start, const MATH::Vec3& end, const MATH::Vec3& halfExtents,
                 MATH::Vec3& outHitPoint, float& outNormalizedDistance) noexcept;

    uint64_t GetSourceRevision() const noexcept;
    const std::string& GetDiagnostic() const noexcept;
    const NavigationDiagnostics& GetDiagnostics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::NAVIGATION
