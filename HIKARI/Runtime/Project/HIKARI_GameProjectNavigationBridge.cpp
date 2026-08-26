#include "HIKARI/Project/NavigationView.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Navigation/HIKARI_NavigationWorldService.h"

namespace HIKARI {
    namespace {

        NAVIGATION::NavigationWorldService* FindNavigationService(
            World* world) noexcept {
            return world != nullptr
                ? world->Services().Find<
                    NAVIGATION::NavigationWorldService>()
                : nullptr;
        }

        MATH::Vec3 ToRuntime(GameProjectFloat3 value) noexcept {
            return { value.x, value.y, value.z };
        }

        GameProjectFloat3 ToProject(const MATH::Vec3& value) noexcept {
            return { value.x, value.y, value.z };
        }

        GameProjectNavigationStatus ToProject(
            NAVIGATION::QueryStatus status) noexcept {
            switch (status) {
            case NAVIGATION::QueryStatus::Success:
                return GameProjectNavigationStatus::Success;
            case NAVIGATION::QueryStatus::Partial:
                return GameProjectNavigationStatus::Partial;
            case NAVIGATION::QueryStatus::Failed:
                return GameProjectNavigationStatus::Failed;
            case NAVIGATION::QueryStatus::BufferTooSmall:
                return GameProjectNavigationStatus::BufferTooSmall;
            case NAVIGATION::QueryStatus::Unavailable:
            default:
                return GameProjectNavigationStatus::Unavailable;
            }
        }

    } // namespace

    NavigationView GameProjectSdkAccess::MakeNavigationView(
        World& world) noexcept {
        return NavigationView(&world);
    }

    bool NavigationView::ProjectPoint(
        GameProjectFloat3 point,
        GameProjectFloat3 halfExtents,
        GameProjectFloat3& outProjectedPoint) const noexcept {
        outProjectedPoint = {};
        try {
            World* world = static_cast<World*>(world_);
            NAVIGATION::NavigationWorldService* service =
                FindNavigationService(world);
            MATH::Vec3 projected{};
            if (service == nullptr ||
                !service->ProjectPoint(
                    *world,
                    ToRuntime(point),
                    ToRuntime(halfExtents),
                    projected)) {
                return false;
            }
            outProjectedPoint = ToProject(projected);
            return true;
        } catch (...) {
            outProjectedPoint = {};
            return false;
        }
    }

    GameProjectNavigationPathResult NavigationView::FindPath(
        GameProjectFloat3 start,
        GameProjectFloat3 end,
        GameProjectFloat3 halfExtents,
        GameProjectNavigationPathPoint* outPoints,
        uint32_t pointCapacity) const noexcept {
        GameProjectNavigationPathResult result{};
        try {
            World* world = static_cast<World*>(world_);
            NAVIGATION::NavigationWorldService* service =
                FindNavigationService(world);
            if (service == nullptr) {
                return result;
            }

            constexpr uint32_t kBridgeCapacity = 256u;
            std::array<MATH::Vec3, kBridgeCapacity> points{};
            std::array<uint8_t, kBridgeCapacity> flags{};
            const NAVIGATION::PathResult runtimeResult = service->FindPath(
                *world,
                ToRuntime(start),
                ToRuntime(end),
                ToRuntime(halfExtents),
                points.data(),
                flags.data(),
                kBridgeCapacity);
            result.status = ToProject(runtimeResult.status);
            result.partial = runtimeResult.partial ? 1u : 0u;
            result.pointCount = runtimeResult.pointCount;
            result.pathLength = runtimeResult.pathLength;
            if (runtimeResult.status == NAVIGATION::QueryStatus::Unavailable ||
                runtimeResult.status == NAVIGATION::QueryStatus::Failed) {
                return result;
            }
            if (pointCapacity > 0u && outPoints == nullptr) {
                result.status = GameProjectNavigationStatus::Failed;
                return result;
            }
            const uint32_t writeCount = (std::min)(
                pointCapacity, runtimeResult.pointCount);
            for (uint32_t point = 0u; point < writeCount; ++point) {
                outPoints[point] = {};
                outPoints[point].position = ToProject(points[point]);
                outPoints[point].flags =
                    static_cast<GameProjectNavigationPathPointFlags>(
                        flags[point]);
            }
            if (pointCapacity < runtimeResult.pointCount) {
                result.status =
                    GameProjectNavigationStatus::BufferTooSmall;
            }
            return result;
        } catch (...) {
            result.status = GameProjectNavigationStatus::Failed;
            return result;
        }
    }

    bool NavigationView::Raycast(
        GameProjectFloat3 start,
        GameProjectFloat3 end,
        GameProjectFloat3 halfExtents,
        GameProjectFloat3& outHitPoint,
        float& outNormalizedDistance) const noexcept {
        outHitPoint = end;
        outNormalizedDistance = 1.0f;
        try {
            World* world = static_cast<World*>(world_);
            NAVIGATION::NavigationWorldService* service =
                FindNavigationService(world);
            MATH::Vec3 hit{};
            if (service == nullptr ||
                !service->Raycast(
                    *world,
                    ToRuntime(start),
                    ToRuntime(end),
                    ToRuntime(halfExtents),
                    hit,
                    outNormalizedDistance)) {
                return false;
            }
            outHitPoint = ToProject(hit);
            return true;
        } catch (...) {
            outHitPoint = end;
            outNormalizedDistance = 1.0f;
            return false;
        }
    }

    uint64_t NavigationView::GetSourceRevision() const noexcept {
        try {
            NAVIGATION::NavigationWorldService* service =
                FindNavigationService(static_cast<World*>(world_));
            return service != nullptr
                ? service->GetSourceRevision()
                : 0u;
        } catch (...) {
            return 0u;
        }
    }

} // namespace HIKARI
