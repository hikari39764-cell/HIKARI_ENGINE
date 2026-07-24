#include "Physics/HIKARI_PhysicsSystem.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {
    namespace {
        PhysicsBodyState InitialStateFrom(
            const PhysicsBodyCreateInfo& createInfo) noexcept {
            PhysicsBodyState state{};
            state.pose = createInfo.initialPose;
            state.linearVelocity = createInfo.initialLinearVelocity;
            state.angularVelocity = createInfo.initialAngularVelocity;
            state.awake = true;
            return state;
        }
    }

    void PhysicsSystem::ReconcileBodies(
        World& world,
        uint64_t frameIndex) {
        if (service_ == nullptr) {
            return;
        }
        lastReconcileFrame_ = frameIndex;
        std::unordered_set<uint64_t> liveObjects{};
        for (const auto& objectOwner : world.GetObjects()) {
            if (!objectOwner) {
                continue;
            }

            GameObject& object = *objectOwner;
            const RuntimeObjectHandle objectHandle =
                object.GetRuntimeHandle();
            const uint64_t objectKey = objectHandle.ToValue();
            auto retained = bindings_.find(objectKey);
            BodyBinding* retainedBinding = retained != bindings_.end()
                ? &retained->second
                : nullptr;
            PhysicsBodyCreateInfo createInfo{};
            MATH::Vec3 worldScale{};
            PhysicsBodyBuildResult build =
                BuildPhysicsBodyCreateInfo(
                    object,
                    createInfo,
                    worldScale,
                    collisionGeometryStore_,
                    retainedBinding != nullptr
                        ? &retainedBinding->definitionWorldScale
                        : nullptr);
            if (!build.hasDefinition) {
                auto stale = bindings_.find(objectKey);
                if (stale != bindings_.end()) {
                    DestroyKinematicSolver(stale->second);
                    (void)service_->DestroyBody(stale->second.body);
                    bindings_.erase(stale);
                }
                failures_.erase(objectKey);
                if (runtimeStatus_ != nullptr) {
                    runtimeStatus_->Remove(objectHandle);
                }
                if (presentationTransforms_ != nullptr) {
                    presentationTransforms_->Remove(objectHandle);
                }
                if (kinematicMotion_ != nullptr) {
                    kinematicMotion_->Remove(objectHandle);
                }
                continue;
            }

            liveObjects.insert(objectKey);
            if (!build.success) {
                ReportFailure(
                    objectHandle,
                    retainedBinding,
                    createInfo.body.motionType,
                    0u,
                    build.sourceRevision,
                    build.error,
                    std::move(build.message),
                    false);
                continue;
            }

            const uint64_t signature =
                ComputePhysicsBodyDefinitionSignature(
                    createInfo,
                    worldScale);
            if (retainedBinding != nullptr &&
                retainedBinding->definitionSignature == signature) {
                failures_.erase(objectKey);
                retainedBinding->sourceRevision = build.sourceRevision;
                if (runtimeStatus_ != nullptr) {
                    const PhysicsBodyRuntimeStatus* existing =
                        runtimeStatus_->FindBodyStatus(objectHandle);
                    const bool needsRecovery = existing == nullptr ||
                        existing->state != PhysicsBodyRuntimeState::Ready ||
                        existing->error != PhysicsErrorCode::None;
                    if (needsRecovery) {
                        PhysicsBodyRuntimeStatus status{};
                        status.object = objectHandle;
                        status.body = retainedBinding->body;
                        status.requestedMotionType =
                            retainedBinding->requestedMotionType;
                        status.effectiveMotionType =
                            retainedBinding->effectiveMotionType;
                        status.state = PhysicsBodyRuntimeState::Ready;
                        status.definitionSignature = signature;
                        status.sourceRevision = build.sourceRevision;
                        status.shapeCount = static_cast<uint32_t>(
                            createInfo.shapes.size());
                        status.awake =
                            retainedBinding->currentFixedState.awake;
                        status.message =
                            "physics body definition recovered";
                        runtimeStatus_->SetBodyStatus(
                            std::move(status));
                        runtimeStatus_->SetBodyDebugShapes(
                            objectHandle,
                            createInfo.shapes);
                    }
                }
                continue;
            }
            if (!ShouldRetryFailure(
                    objectKey,
                    signature,
                    build.sourceRevision,
                    frameIndex)) {
                continue;
            }

            PhysicsBodyState preservedState{};
            const bool hasPreservedState = retainedBinding != nullptr &&
                retainedBinding->body.IsValid() &&
                service_->TryGetBodyState(
                    retainedBinding->body,
                    preservedState);
            if (hasPreservedState) {
                createInfo.initialPose = preservedState.pose;
                createInfo.initialLinearVelocity =
                    preservedState.linearVelocity;
                createInfo.initialAngularVelocity =
                    preservedState.angularVelocity;
            }

            // Last-known-good replacement: the old body remains alive until
            // the complete new definition is accepted by the backend.
            PhysicsBodyCreateResult created =
                service_->CreateBody(createInfo);
            if (!created.Succeeded()) {
                ReportFailure(
                    objectHandle,
                    retainedBinding,
                    createInfo.body.motionType,
                    signature,
                    build.sourceRevision,
                    created.error,
                    std::move(created.message),
                    created.recoverable);
                continue;
            }

            if (retainedBinding != nullptr) {
                DestroyKinematicSolver(*retainedBinding);
                (void)service_->DestroyBody(retainedBinding->body);
            }

            BodyBinding binding{};
            binding.object = objectHandle;
            binding.body = created.handle;
            binding.bodyDesc = createInfo.body;
            binding.shapes = createInfo.shapes;
            binding.requestedMotionType = createInfo.body.motionType;
            binding.effectiveMotionType = created.effectiveMotionType;
            binding.definitionSignature = signature;
            binding.sourceRevision = build.sourceRevision;
            binding.definitionWorldScale = worldScale;
            binding.lastPushedPose = createInfo.initialPose;
            binding.hasLastPushedPose = true;
            binding.currentFixedState = hasPreservedState
                ? preservedState
                : InitialStateFrom(createInfo);
            (void)service_->TryGetBodyState(
                binding.body,
                binding.currentFixedState);
            binding.previousFixedState = binding.currentFixedState;
            binding.hasFixedState = true;
            bindings_.insert_or_assign(objectKey, binding);
            failures_.erase(objectKey);

            if (runtimeStatus_ != nullptr) {
                PhysicsBodyRuntimeStatus status{};
                status.object = objectHandle;
                status.body = binding.body;
                status.requestedMotionType =
                    binding.requestedMotionType;
                status.effectiveMotionType =
                    binding.effectiveMotionType;
                status.state = PhysicsBodyRuntimeState::Ready;
                status.definitionSignature = signature;
                status.sourceRevision = build.sourceRevision;
                status.shapeCount = static_cast<uint32_t>(
                    createInfo.shapes.size());
                status.awake = binding.currentFixedState.awake;
                status.message = "physics body ready";
                runtimeStatus_->SetBodyStatus(std::move(status));
                runtimeStatus_->SetBodyDebugShapes(
                    objectHandle,
                    createInfo.shapes);
            }
        }

        for (auto it = bindings_.begin(); it != bindings_.end();) {
            if (liveObjects.contains(it->first)) {
                ++it;
                continue;
            }
            const RuntimeObjectHandle object = it->second.object;
            DestroyKinematicSolver(it->second);
            (void)service_->DestroyBody(it->second.body);
            if (presentationTransforms_ != nullptr) {
                presentationTransforms_->Remove(object);
            }
            if (runtimeStatus_ != nullptr) {
                runtimeStatus_->Remove(object);
            }
            if (kinematicMotion_ != nullptr) {
                kinematicMotion_->Remove(object);
            }
            failures_.erase(it->first);
            it = bindings_.erase(it);
        }
    }

    void PhysicsSystem::DestroyBindings() noexcept {
        if (service_ != nullptr) {
            for (auto& [_, binding] : bindings_) {
                DestroyKinematicSolver(binding);
                (void)service_->DestroyBody(binding.body);
            }
        }
        if (presentationTransforms_ != nullptr) {
            for (const auto& [_, binding] : bindings_) {
                presentationTransforms_->Remove(binding.object);
            }
        }
        bindings_.clear();
        if (kinematicMotion_ != nullptr) {
            kinematicMotion_->Clear();
        }
    }

    void PhysicsSystem::ReportFailure(
        RuntimeObjectHandle object,
        const BodyBinding* retainedBinding,
        PhysicsMotionType requestedMotionType,
        uint64_t signature,
        uint64_t sourceRevision,
        PhysicsErrorCode error,
        std::string message,
        bool recoverable) {
        const uint64_t objectKey = object.ToValue();
        const auto previous = failures_.find(objectKey);
        const bool changed = previous == failures_.end() ||
            previous->second.error != error ||
            previous->second.definitionSignature != signature ||
            previous->second.sourceRevision != sourceRevision ||
            previous->second.message != message;

        ReconcileFailure failure{};
        if (!changed) {
            failure = previous->second;
        }
        failure.error = error;
        failure.definitionSignature = signature;
        failure.sourceRevision = sourceRevision;
        failure.recoverable = recoverable;
        failure.message = message;
        ++failure.attempts;
        const uint32_t exponent = (std::min)(failure.attempts, 6u);
        const uint64_t delay = recoverable
            ? (uint64_t{ 1u } << exponent)
            : 60u;
        failure.nextRetryFrame = lastReconcileFrame_ + delay;
        failures_.insert_or_assign(objectKey, failure);

        if (changed) {
            HIKARI_LOG_ERROR(
                "[Physics] body reconciliation failed object=" +
                std::to_string(objectKey) + " error=" +
                std::to_string(static_cast<uint32_t>(error)) +
                " message=" + message);
        }
        if (runtimeStatus_ == nullptr) {
            return;
        }
        PhysicsBodyRuntimeStatus status{};
        status.object = object;
        status.requestedMotionType = requestedMotionType;
        status.effectiveMotionType = retainedBinding != nullptr
            ? retainedBinding->effectiveMotionType
            : requestedMotionType;
        status.definitionSignature = signature;
        status.sourceRevision = sourceRevision;
        status.error = error;
        status.message = std::move(message);
        if (retainedBinding != nullptr &&
            retainedBinding->body.IsValid()) {
            status.body = retainedBinding->body;
            status.state = PhysicsBodyRuntimeState::RetainedPrevious;
            status.awake = retainedBinding->currentFixedState.awake;
        } else {
            status.state = recoverable
                ? PhysicsBodyRuntimeState::PendingRetry
                : PhysicsBodyRuntimeState::Invalid;
        }
        runtimeStatus_->SetBodyStatus(std::move(status));
    }

    bool PhysicsSystem::ShouldRetryFailure(
        uint64_t objectKey,
        uint64_t definitionSignature,
        uint64_t sourceRevision,
        uint64_t frameIndex) const noexcept {
        const auto found = failures_.find(objectKey);
        return found == failures_.end() ||
            found->second.definitionSignature != definitionSignature ||
            found->second.sourceRevision != sourceRevision ||
            frameIndex >= found->second.nextRetryFrame;
    }

} // namespace HIKARI::PHYSICS
