#pragma once

#include <cstdint>
#include <limits>
#include <unordered_map>

#include "Physics/HIKARI_PhysicsTypes.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {
    struct RuntimePlayStateService;
    class PresentationTransformService;
}

namespace HIKARI::PHYSICS {

    class PhysicsWorldService;
    class PhysicsCollisionGeometryStore;
    class PhysicsRuntimeStatusService;
    class KinematicMotionService;
    struct KinematicControllerSettings;

    class PhysicsSystem final : public ISystem {
    public:
        explicit PhysicsSystem(
            PhysicsWorldSettings settings = {});

        std::string_view GetName() const override {
            return "PhysicsSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void FixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PostFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void LateUpdate(
            World& world,
            const FrameContext& frame) override;

    private:
        struct BodyBinding {
            RuntimeObjectHandle object{};
            PhysicsBodyHandle body{};
            PhysicsBodyDesc bodyDesc{};
            std::vector<PhysicsShapeDesc> shapes{};
            PhysicsMotionType requestedMotionType =
                PhysicsMotionType::Static;
            PhysicsMotionType effectiveMotionType =
                PhysicsMotionType::Static;
            uint64_t definitionSignature = 0;
            uint64_t sourceRevision = 0;
            MATH::Vec3 definitionWorldScale{ 1.0f, 1.0f, 1.0f };
            PhysicsPose lastPushedPose{};
            bool hasLastPushedPose = false;
            PhysicsBodyState previousFixedState{};
            PhysicsBodyState currentFixedState{};
            bool hasFixedState = false;
            bool kinematicPresentationActive = false;
            bool presentationDiscontinuity = true;
            PhysicsCharacterHandle kinematicSolver{};
            uint64_t kinematicSolverSignature = 0u;
            // A jump is a multi-tick controller motion. Keep its lifecycle
            // in the physics integration so transient contact changes cannot
            // re-enable ground traversal before a real landing.
            bool kinematicJumpActive = false;
        };

        struct ReconcileFailure {
            PhysicsErrorCode error = PhysicsErrorCode::None;
            uint64_t definitionSignature = 0u;
            uint64_t sourceRevision = 0u;
            uint64_t nextRetryFrame = 0u;
            uint32_t attempts = 0u;
            bool recoverable = false;
            std::string message{};
        };

        bool ShouldSimulate() const noexcept;
        void ReconcileBodies(World& world, uint64_t frameIndex);
        void DestroyBindings() noexcept;
        void PushSceneDrivenPoses(World& world);
        void ProcessKinematicMotions(
            World& world,
            const FrameContext& frame);
        void DestroyKinematicSolver(BodyBinding& binding) noexcept;
        bool EnsureKinematicSolver(
            BodyBinding& binding,
            const KinematicControllerSettings& settings,
            const PhysicsBodyState& bodyState,
            bool& outRetainedPrevious,
            std::string& outError);
        void PullDynamicPoses(World& world);
        void CommitFixedState(
            BodyBinding& binding,
            const PhysicsBodyState& state,
            bool discontinuity) noexcept;
        void UpdatePresentationPoses(
            World& world,
            float interpolationAlpha);
        void ReportFailure(
            RuntimeObjectHandle object,
            const BodyBinding* retainedBinding,
            PhysicsMotionType requestedMotionType,
            uint64_t signature,
            uint64_t sourceRevision,
            PhysicsErrorCode error,
            std::string message,
            bool recoverable);
        bool ShouldRetryFailure(
            uint64_t objectKey,
            uint64_t definitionSignature,
            uint64_t sourceRevision,
            uint64_t frameIndex) const noexcept;

        PhysicsWorldSettings settings_{};
        PhysicsWorldService* service_ = nullptr;
        PhysicsCollisionGeometryStore* collisionGeometryStore_ = nullptr;
        PhysicsRuntimeStatusService* runtimeStatus_ = nullptr;
        KinematicMotionService* kinematicMotion_ = nullptr;
        ::HIKARI::PresentationTransformService*
            presentationTransforms_ = nullptr;
        const RuntimePlayStateService* runtimePlayState_ = nullptr;
        std::unordered_map<uint64_t, BodyBinding> bindings_{};
        std::unordered_map<uint64_t, ReconcileFailure> failures_{};
        PhysicsStepResult lastStepResult_{};
        uint64_t lastReconcileFrame_ =
            (std::numeric_limits<uint64_t>::max)();
    };

} // namespace HIKARI::PHYSICS
