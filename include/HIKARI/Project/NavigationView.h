#pragma once

#include <cstdint>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/NavigationTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Value-only view over the current World's prepared navigation snapshot.
// It is valid only while that World remains alive and must be used from
// the main gameplay thread; queries are not re-entrant or thread-safe.
// The host prepares navigation once at Play entry and once per frame, so
// query calls never rebuild terrain or retain caller buffers. IsValid()
// describes only the World lease; when the optional Navigation Feature
// is inactive, FindPath returns Unavailable and other queries return
// false without installing or building a navigation service.
class NavigationView {
public:
    NavigationView() = default;

    bool IsValid() const noexcept { return world_ != nullptr; }

    HIKARI_GAME_PROJECT_SDK_API bool ProjectPoint(GameProjectFloat3 point, GameProjectFloat3 halfExtents,
                                                  GameProjectFloat3& outProjectedPoint) const noexcept;

    HIKARI_GAME_PROJECT_SDK_API GameProjectNavigationPathResult FindPath(GameProjectFloat3 start, GameProjectFloat3 end,
                                                                         GameProjectFloat3 halfExtents,
                                                                         GameProjectNavigationPathPoint* outPoints,
                                                                         uint32_t pointCapacity) const noexcept;
    // BufferTooSmall writes the available prefix and returns the required
    // pointCount. partial remains set when the reachable end differs from
    // the requested end, even when BufferTooSmall takes status priority.

    HIKARI_GAME_PROJECT_SDK_API bool Raycast(GameProjectFloat3 start, GameProjectFloat3 end,
                                             GameProjectFloat3 halfExtents, GameProjectFloat3& outHitPoint,
                                             float& outNormalizedDistance) const noexcept;
    // Raycast starts at the navmesh-projected start. The returned point
    // and normalized distance are measured along that projected segment.

    HIKARI_GAME_PROJECT_SDK_API uint64_t GetSourceRevision() const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit NavigationView(void* world) noexcept : world_(world) {}

    void* world_ = nullptr;
};

} // namespace HIKARI
