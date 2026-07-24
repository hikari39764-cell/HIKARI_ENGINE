#include "Physics/HIKARI_KinematicMotionDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "Core/HIKARI_Logger.h"

namespace HIKARI::PHYSICS {
    namespace {

        const char* GroundStateName(
            PhysicsCharacterGroundState state) noexcept {
            switch (state) {
            case PhysicsCharacterGroundState::OnGround:
                return "OnGround";
            case PhysicsCharacterGroundState::OnSteepGround:
                return "OnSteepGround";
            case PhysicsCharacterGroundState::NotSupported:
                return "NotSupported";
            case PhysicsCharacterGroundState::InAir:
            default:
                return "InAir";
            }
        }

        void AppendCharacterState(
            std::ostringstream& stream,
            std::string_view label,
            const PhysicsCharacterState& state) {
            stream << ' ' << label << "Y=" << state.pose.position.y
                << ' ' << label << "Vy=" << state.linearVelocity.y
                << ' ' << label << "Ground="
                << GroundStateName(state.groundState)
                << ' ' << label << "GroundY=" << state.groundPosition.y
                << ' ' << label << "GroundVy=" << state.groundVelocity.y
                << ' ' << label << "NormalY=" << state.groundNormal.y
                << ' ' << label << "GroundObject="
                << state.groundObject.ToValue();
        }

    } // namespace

    void TraceKinematicMotionStep(
        const KinematicMotionTraceSample& sample) {
        if (!sample.request.jumpRequested &&
            !sample.jumpActiveAtEntry &&
            !sample.jumpActiveBeforeStep &&
            !sample.jumpActiveAfterStep) {
            return;
        }

        const bool solverChanged =
            sample.solverBeforeEnsure != sample.solver ||
            sample.solverSignatureBeforeEnsure != sample.solverSignature;
        const float preSyncDeltaY = sample.hasSolverStateBeforeSync
            ? sample.solverStateBeforeSync.pose.position.y -
                sample.bodyState.pose.position.y
            : 0.0f;
        const float expectedGravityDelta =
            std::abs(sample.gravity.y) * sample.frame.fixedDt;
        const float solverVelocityLoss =
            sample.commandedVelocity.y - sample.afterStep.linearVelocity.y;
        const bool unexpectedUpwardLoss =
            sample.commandedVelocity.y > 0.25f &&
            solverVelocityLoss > (std::max)(
                0.25f,
                expectedGravityDelta * 2.5f) &&
            !sample.afterStep.hitCeiling;
        const bool staleBodyResync =
            sample.jumpActiveAtEntry &&
            sample.hasSolverStateBeforeSync &&
            std::abs(preSyncDeltaY) > 0.01f;
        const bool suspicious = solverChanged ||
            unexpectedUpwardLoss || staleBodyResync;
        if (!suspicious) {
            return;
        }

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(5)
            << "[CharacterMotionTrace]"
            << " object=\"" << sample.objectName << '"'
            << " objectHandle=" << sample.object.ToValue()
            << " frame=" << sample.frame.frameIndex
            << " tick=" << sample.frame.fixedTickIndex
            << " fixedStep=" << sample.frame.fixedStepIndex
            << '/' << sample.frame.fixedStepsThisFrame
            << " dt=" << sample.frame.fixedDt
            << " bodyHandle=" << sample.body.ToValue()
            << " solver=" << sample.solver.ToValue()
            << " solverBefore=" << sample.solverBeforeEnsure.ToValue()
            << " solverChanged=" << solverChanged
            << " retainedSolver=" << sample.retainedPreviousSolver
            << " bodyY=" << sample.bodyState.pose.position.y
            << " bodyVy=" << sample.bodyState.linearVelocity.y;

        if (sample.hasSolverStateBeforeSync) {
            AppendCharacterState(
                stream,
                "preSync",
                sample.solverStateBeforeSync);
            stream << " preSyncDeltaY=" << preSyncDeltaY;
        } else {
            stream << " preSync=unavailable";
        }

        AppendCharacterState(stream, "before", sample.beforeStep);
        stream << " inputXZ=(" << sample.request.horizontalVelocity.x
            << ',' << sample.request.horizontalVelocity.z << ')'
            << " jumpRequested=" << sample.request.jumpRequested
            << " allowUngroundedJump="
            << sample.request.allowJumpWithoutGroundContact
            << " jumpSpeed=" << sample.request.jumpSpeed
            << " jumpApplied=" << sample.jumpApplied
            << " jumpActive=" << sample.jumpActiveAtEntry
            << '>' << sample.jumpActiveBeforeStep
            << '>' << sample.jumpActiveAfterStep
            << " commandVy=" << sample.commandedVelocity.y
            << " groundTraversal=" << sample.allowGroundTraversal;
        AppendCharacterState(stream, "after", sample.afterStep);
        stream << " poseDeltaY="
            << sample.afterStep.pose.position.y -
                sample.bodyState.pose.position.y
            << " velocityLoss=" << solverVelocityLoss
            << " wall=" << sample.afterStep.hitWall
            << " ceiling=" << sample.afterStep.hitCeiling
            << " maxHits=" << sample.afterStep.maxHitsExceeded
            << " suspectSolverReset=" << solverChanged
            << " suspectBodyResync=" << staleBodyResync
            << " suspectVelocityLoss=" << unexpectedUpwardLoss;

        HIKARI_LOG_WARN(stream.str());
    }

} // namespace HIKARI::PHYSICS
