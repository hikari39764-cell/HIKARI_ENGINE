#include "Physics/HIKARI_KinematicMotionService.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace HIKARI::PHYSICS {

    KinematicMotionService::ActorState&
        KinematicMotionService::FindOrCreate(
            RuntimeObjectHandle object) {
        return actors_[object.ToValue()];
    }

    void KinematicMotionService::SubmitMove(
        RuntimeObjectHandle object,
        KinematicMotionSourceId source,
        int priority,
        const KinematicMotionRequest& request,
        uint64_t fixedTickIndex) {
        if (!object.IsValid() || source == 0u) {
            return;
        }

        std::vector<MoveSlot>& slots = FindOrCreate(object).moves;
        auto found = std::find_if(
            slots.begin(),
            slots.end(),
            [source](const MoveSlot& slot) {
                return slot.source == source;
            });
        if (found == slots.end()) {
            slots.push_back(MoveSlot{});
            found = std::prev(slots.end());
            found->source = source;
        }
        found->priority = priority;
        found->submittedTick = fixedTickIndex;
        found->request = request;

        slots.erase(
            std::remove_if(
                slots.begin(),
                slots.end(),
                [fixedTickIndex](const MoveSlot& slot) {
                    return fixedTickIndex > slot.submittedTick + 2u;
                }),
            slots.end());
    }

    bool KinematicMotionService::ConsumeMove(
        RuntimeObjectHandle object,
        uint64_t fixedTickIndex,
        KinematicMotionRequest& outRequest,
        KinematicMotionSourceId* outSource) {
        outRequest = {};
        const auto actor = actors_.find(object.ToValue());
        if (!object.IsValid() || actor == actors_.end()) {
            return false;
        }

        MoveSlot* selected = nullptr;
        for (MoveSlot& slot : actor->second.moves) {
            if (slot.submittedTick != fixedTickIndex) {
                continue;
            }
            if (selected == nullptr || slot.priority > selected->priority ||
                (slot.priority == selected->priority &&
                    slot.source < selected->source)) {
                selected = &slot;
            }
        }
        if (selected == nullptr) {
            return false;
        }

        outRequest = selected->request;
        if (outSource != nullptr) {
            *outSource = selected->source;
        }
        return true;
    }

    void KinematicMotionService::RequestTeleport(
        RuntimeObjectHandle object,
        KinematicMotionSourceId source,
        int priority,
        const KinematicTeleportRequest& request) {
        if (!object.IsValid() || source == 0u) {
            return;
        }
        ActorState& actor = FindOrCreate(object);
        if (!actor.teleport.has_value() ||
            priority > actor.teleport->priority ||
            (priority == actor.teleport->priority &&
                source < actor.teleport->source)) {
            actor.teleport = TeleportSlot{ source, priority, request };
        }
    }

    bool KinematicMotionService::ConsumeTeleport(
        RuntimeObjectHandle object,
        KinematicTeleportRequest& outRequest) {
        const auto actor = actors_.find(object.ToValue());
        if (!object.IsValid() || actor == actors_.end() ||
            !actor->second.teleport.has_value()) {
            return false;
        }
        outRequest = actor->second.teleport->request;
        actor->second.teleport.reset();
        return true;
    }

    void KinematicMotionService::AddExternalVelocity(
        RuntimeObjectHandle object,
        const MATH::Vec3& velocity) {
        if (object.IsValid()) {
            ActorState& actor = FindOrCreate(object);
            actor.externalVelocity = actor.externalVelocity + velocity;
        }
    }

    MATH::Vec3 KinematicMotionService::ConsumeExternalVelocity(
        RuntimeObjectHandle object) {
        const auto actor = actors_.find(object.ToValue());
        if (!object.IsValid() || actor == actors_.end()) {
            return {};
        }
        const MATH::Vec3 velocity = actor->second.externalVelocity;
        actor->second.externalVelocity = {};
        return velocity;
    }

    void KinematicMotionService::SetState(
        RuntimeObjectHandle object,
        const KinematicMotionState& state) {
        if (!object.IsValid()) {
            return;
        }
        ActorState& actor = FindOrCreate(object);
        actor.state = state;
        actor.hasState = true;
    }

    const KinematicMotionState* KinematicMotionService::FindState(
        RuntimeObjectHandle object) const noexcept {
        const auto actor = actors_.find(object.ToValue());
        return object.IsValid() && actor != actors_.end() &&
            actor->second.hasState
            ? &actor->second.state
            : nullptr;
    }

    void KinematicMotionService::SetStatus(
        RuntimeObjectHandle object,
        KinematicMotionRuntimeStatus status) {
        if (!object.IsValid()) {
            return;
        }
        ActorState& actor = FindOrCreate(object);
        if (actor.hasStatus && actor.status.health == status.health &&
            actor.status.message == status.message) {
            return;
        }
        actor.status = std::move(status);
        actor.hasStatus = true;
    }

    const KinematicMotionRuntimeStatus*
        KinematicMotionService::FindStatus(
            RuntimeObjectHandle object) const noexcept {
        const auto actor = actors_.find(object.ToValue());
        return object.IsValid() && actor != actors_.end() &&
            actor->second.hasStatus
            ? &actor->second.status
            : nullptr;
    }

    void KinematicMotionService::Remove(
        RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) {
            actors_.erase(object.ToValue());
        }
    }

    void KinematicMotionService::Clear() noexcept {
        actors_.clear();
    }

} // namespace HIKARI::PHYSICS
