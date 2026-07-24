#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Physics/HIKARI_PhysicsTypes.h"

namespace HIKARI::PHYSICS {

    // Read-only bridge from the physics runtime to editor/debug tools.
    // PhysicsSystem remains the sole writer of body lifecycle state.
    class PhysicsRuntimeStatusService {
    public:
        void Clear() noexcept;
        void Remove(RuntimeObjectHandle object) noexcept;
        void SetBodyStatus(PhysicsBodyRuntimeStatus status);
        void SetBodyDebugShapes(
            RuntimeObjectHandle object,
            std::vector<PhysicsShapeDesc> shapes);
        void SetStepResult(
            PhysicsStepResult result,
            PhysicsBackendStatistics statistics);

        const PhysicsBodyRuntimeStatus* FindBodyStatus(
            RuntimeObjectHandle object) const noexcept;
        const std::vector<PhysicsShapeDesc>* FindBodyDebugShapes(
            RuntimeObjectHandle object) const noexcept;
        std::vector<PhysicsBodyRuntimeStatus> GetBodyStatuses() const;
        const PhysicsStepResult& GetLastStepResult() const noexcept;
        const PhysicsBackendStatistics& GetBackendStatistics()
            const noexcept;
        uint64_t GetRevision() const noexcept;

    private:
        void AdvanceRevision() noexcept;

        std::unordered_map<uint64_t, PhysicsBodyRuntimeStatus>
            bodyStatuses_{};
        std::unordered_map<uint64_t, std::vector<PhysicsShapeDesc>>
            bodyDebugShapes_{};
        PhysicsStepResult lastStepResult_{};
        PhysicsBackendStatistics backendStatistics_{};
        uint64_t revision_ = 1u;
    };

} // namespace HIKARI::PHYSICS
