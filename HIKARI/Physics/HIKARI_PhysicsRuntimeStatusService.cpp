#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <utility>

namespace HIKARI::PHYSICS {

void PhysicsRuntimeStatusService::Clear() noexcept {
    bodyStatuses_.clear();
    bodyDebugShapes_.clear();
    lastStepResult_ = {};
    backendStatistics_ = {};
    NUMERIC::AdvanceNonZero(revision_);
}

void PhysicsRuntimeStatusService::Remove(RuntimeObjectHandle object) noexcept {
    if (!object.IsValid()) {
        return;
    }
    const uint64_t key = object.ToValue();
    const bool removedStatus = bodyStatuses_.erase(key) > 0u;
    const bool removedShapes = bodyDebugShapes_.erase(key) > 0u;
    if (removedStatus || removedShapes) {
        NUMERIC::AdvanceNonZero(revision_);
    }
}

void PhysicsRuntimeStatusService::SetBodyStatus(PhysicsBodyRuntimeStatus status) {
    if (!status.object.IsValid()) {
        return;
    }
    bodyStatuses_[status.object.ToValue()] = std::move(status);
    NUMERIC::AdvanceNonZero(revision_);
}

void PhysicsRuntimeStatusService::SetBodyDebugShapes(RuntimeObjectHandle object, std::vector<PhysicsShapeDesc> shapes) {
    if (!object.IsValid()) {
        return;
    }
    bodyDebugShapes_.insert_or_assign(object.ToValue(), std::move(shapes));
    NUMERIC::AdvanceNonZero(revision_);
}

void PhysicsRuntimeStatusService::SetStepResult(PhysicsStepResult result, PhysicsBackendStatistics statistics) {
    lastStepResult_ = std::move(result);
    backendStatistics_ = statistics;
    NUMERIC::AdvanceNonZero(revision_);
}

const PhysicsBodyRuntimeStatus* PhysicsRuntimeStatusService::FindBodyStatus(RuntimeObjectHandle object) const noexcept {
    if (!object.IsValid()) {
        return nullptr;
    }
    const auto found = bodyStatuses_.find(object.ToValue());
    return found != bodyStatuses_.end() ? &found->second : nullptr;
}

const std::vector<PhysicsShapeDesc>* PhysicsRuntimeStatusService::FindBodyDebugShapes(
    RuntimeObjectHandle object) const noexcept {
    if (!object.IsValid()) {
        return nullptr;
    }
    const auto found = bodyDebugShapes_.find(object.ToValue());
    return found != bodyDebugShapes_.end() ? &found->second : nullptr;
}

uint64_t PhysicsRuntimeStatusService::GetRevision() const noexcept { return revision_; }

} // namespace HIKARI::PHYSICS
