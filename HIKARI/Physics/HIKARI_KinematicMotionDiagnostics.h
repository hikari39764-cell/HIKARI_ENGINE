#pragma once

#include <string_view>

#include "Core/HIKARI_FrameContext.h"
#include "Physics/HIKARI_KinematicMotionTypes.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::PHYSICS {

    struct KinematicMotionTraceSample {
        std::string_view objectName{};
        RuntimeObjectHandle object{};
        PhysicsBodyHandle body{};
        PhysicsCharacterHandle solverBeforeEnsure{};
        PhysicsCharacterHandle solver{};
        uint64_t solverSignatureBeforeEnsure = 0u;
        uint64_t solverSignature = 0u;
        FrameContext frame{};
        PhysicsBodyState bodyState{};
        PhysicsCharacterState solverStateBeforeSync{};
        PhysicsCharacterState beforeStep{};
        PhysicsCharacterState afterStep{};
        KinematicMotionRequest request{};
        MATH::Vec3 gravity{};
        MATH::Vec3 commandedVelocity{};
        bool hasSolverStateBeforeSync = false;
        bool retainedPreviousSolver = false;
        bool jumpActiveAtEntry = false;
        bool jumpActiveBeforeStep = false;
        bool jumpApplied = false;
        bool jumpActiveAfterStep = false;
        bool allowGroundTraversal = false;
    };

    // Emits a compact warning only when a jump observes a broken body/solver
    // hand-off or an unexpected loss of upward velocity.
    void TraceKinematicMotionStep(
        const KinematicMotionTraceSample& sample);

} // namespace HIKARI::PHYSICS
