#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "Physics/HIKARI_KinematicMotionTypes.h"

namespace HIKARI::PHYSICS {

class KinematicMotionService {
public:
    void SubmitMove(RuntimeObjectHandle object, KinematicMotionSourceId source, int priority,
                    const KinematicMotionRequest& request, uint64_t fixedTickIndex);
    bool ConsumeMove(RuntimeObjectHandle object, uint64_t fixedTickIndex, KinematicMotionRequest& outRequest,
                     KinematicMotionSourceId* outSource = nullptr);

    bool SubmitMotionDelta(RuntimeObjectHandle object, KinematicMotionSourceId source, int priority,
                           uint64_t fixedTickIndex, const MATH::Vec3& translation, const MATH::Quat& rotation);
    bool ConsumeMotionDelta(RuntimeObjectHandle object, uint64_t fixedTickIndex, KinematicMotionDelta& outDelta,
                            KinematicMotionSourceId* outSource = nullptr);

    void RequestTeleport(RuntimeObjectHandle object, KinematicMotionSourceId source, int priority,
                         const KinematicTeleportRequest& request);
    bool ConsumeTeleport(RuntimeObjectHandle object, KinematicTeleportRequest& outRequest);
    MATH::Vec3 ConsumeExternalVelocity(RuntimeObjectHandle object);

    void SetState(RuntimeObjectHandle object, const KinematicMotionState& state);
    const KinematicMotionState* FindState(RuntimeObjectHandle object) const noexcept;
    void SetStatus(RuntimeObjectHandle object, KinematicMotionRuntimeStatus status);
    const KinematicMotionRuntimeStatus* FindStatus(RuntimeObjectHandle object) const noexcept;

    void Remove(RuntimeObjectHandle object) noexcept;
    void Clear() noexcept;

private:
    struct MoveSlot {
        KinematicMotionSourceId source = 0u;
        int priority = 0;
        uint64_t submittedTick = 0u;
        KinematicMotionRequest request{};
    };

    struct MotionDeltaSlot {
        KinematicMotionSourceId source = 0u;
        int priority = 0;
        uint64_t submittedTick = 0u;
        KinematicMotionDelta delta{};
    };

    struct TeleportSlot {
        KinematicMotionSourceId source = 0u;
        int priority = 0;
        KinematicTeleportRequest request{};
    };

    struct ActorState {
        std::vector<MoveSlot> moves{};
        std::vector<MotionDeltaSlot> motionDeltas{};
        std::optional<TeleportSlot> teleport{};
        MATH::Vec3 externalVelocity{};
        KinematicMotionState state{};
        KinematicMotionRuntimeStatus status{};
        uint64_t lastConsumedMotionDeltaTick = 0u;
        bool hasConsumedMotionDeltaTick = false;
        bool hasState = false;
        bool hasStatus = false;
    };

    ActorState& FindOrCreate(RuntimeObjectHandle object);

    std::unordered_map<uint64_t, ActorState> actors_{};
};

} // namespace HIKARI::PHYSICS
