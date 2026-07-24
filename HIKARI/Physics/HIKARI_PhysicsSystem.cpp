#include "Physics/HIKARI_PhysicsSystem.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Core/HIKARI_Logger.h"
#include "Physics/HIKARI_PhysicsCollisionGeometryStore.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {

    PhysicsSystem::PhysicsSystem(PhysicsWorldSettings settings)
        : settings_(settings) {
    }

    void PhysicsSystem::OnWorldAttached(World& world) {
        service_ = world.Services().Find<PhysicsWorldService>();
        runtimePlayState_ =
            world.Services().Find<RuntimePlayStateService>();
        collisionGeometryStore_ =
            world.Services().Find<PhysicsCollisionGeometryStore>();
        runtimeStatus_ =
            world.Services().Find<PhysicsRuntimeStatusService>();
        kinematicMotion_ =
            world.Services().Find<KinematicMotionService>();
        presentationTransforms_ =
            world.Services().Find<PresentationTransformService>();
        if (runtimeStatus_ != nullptr) {
            runtimeStatus_->Clear();
        }
        if (kinematicMotion_ != nullptr) {
            kinematicMotion_->Clear();
        }
        if (service_ == nullptr) {
            HIKARI_LOG_ERROR(
                "[Physics] PhysicsWorldService is not registered");
            return;
        }
        if (!service_->Configure(settings_) ||
            !service_->AttachWorld(world)) {
            HIKARI_LOG_ERROR(
                "[Physics] failed to attach physics world service");
            service_ = nullptr;
            return;
        }
        ReconcileBodies(world, 0u);
        HIKARI_LOG_INFO(
            "[Physics] world attached backend=" +
            std::string(service_->GetBackendName()));
    }

    void PhysicsSystem::OnWorldDetached(World&) {
        DestroyBindings();
        if (service_ != nullptr) {
            service_->DetachWorld();
        }
        if (runtimeStatus_ != nullptr) {
            runtimeStatus_->Clear();
        }
        if (kinematicMotion_ != nullptr) {
            kinematicMotion_->Clear();
        }
        failures_.clear();
        lastStepResult_ = {};
        lastReconcileFrame_ =
            (std::numeric_limits<uint64_t>::max)();
        service_ = nullptr;
        collisionGeometryStore_ = nullptr;
        runtimeStatus_ = nullptr;
        kinematicMotion_ = nullptr;
        presentationTransforms_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void PhysicsSystem::PreFixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (!ShouldSimulate()) {
            return;
        }
        if (lastReconcileFrame_ != frame.frameIndex) {
            ReconcileBodies(world, frame.frameIndex);
        }
        PushSceneDrivenPoses(world);
    }

    void PhysicsSystem::FixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (!ShouldSimulate() || service_ == nullptr) {
            return;
        }
        ProcessKinematicMotions(world, frame);
        PhysicsStepResult stepResult = service_->Step(frame.fixedDt);
        if (runtimeStatus_ != nullptr) {
            runtimeStatus_->SetStepResult(
                stepResult,
                service_->GetStatistics());
        }
        if (!stepResult.Succeeded()) {
            if (lastStepResult_.error != stepResult.error ||
                lastStepResult_.message != stepResult.message) {
                HIKARI_LOG_ERROR(
                    "[Physics] fixed step failed: " +
                    stepResult.message);
            }
            lastStepResult_ = std::move(stepResult);
            return;
        }
        if (!lastStepResult_.Succeeded()) {
            HIKARI_LOG_INFO(
                "[Physics] fixed-step simulation recovered");
        }
        lastStepResult_ = std::move(stepResult);
        for (PhysicsContactEvent& event :
                service_->ConsumeContactEvents()) {
            world.FixedEvents().Publish(std::move(event));
        }
    }

    void PhysicsSystem::PostFixedUpdate(
        World& world,
        const FrameContext&) {
        if (ShouldSimulate()) {
            PullDynamicPoses(world);
        }
    }

    void PhysicsSystem::LateUpdate(
        World& world,
        const FrameContext& frame) {
        if (!ShouldSimulate()) {
            return;
        }
        UpdatePresentationPoses(
            world,
            (std::clamp)(
                frame.fixedInterpolationAlpha,
                0.0f,
                1.0f));
    }

    bool PhysicsSystem::ShouldSimulate() const noexcept {
        if (service_ == nullptr || !service_->IsWorldAttached()) {
            return false;
        }
#if defined(HIKARI_WITH_EDITOR)
        return runtimePlayState_ != nullptr &&
            runtimePlayState_->IsActive();
#else
        return true;
#endif
    }

} // namespace HIKARI::PHYSICS
