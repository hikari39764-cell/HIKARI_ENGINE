#include "Runtime/Project/HIKARI_GameProjectActionBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationRootMotionService.h"
#include "Animation/Runtime/HIKARI_AnimationSocketService.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "HIKARI/Project/WorldView.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

namespace HIKARI {
    namespace {

        constexpr float kDirectionEpsilon = 1.0e-6f;

        bool IsFinite(const GameProjectFloat3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFinite(const GameProjectQuat& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }

        MATH::Vec3 ToRuntime(const GameProjectFloat3& value) noexcept {
            return { value.x, value.y, value.z };
        }

        MATH::Quat ToRuntime(const GameProjectQuat& value) noexcept {
            return { value.x, value.y, value.z, value.w };
        }

        GameProjectFloat3 ToProject(const MATH::Vec3& value) noexcept {
            return { value.x, value.y, value.z };
        }

        GameProjectQuat ToProject(const MATH::Quat& value) noexcept {
            return { value.x, value.y, value.z, value.w };
        }

        GameProjectFloat3 FiniteOr(
            const MATH::Vec3& value,
            const GameProjectFloat3& fallback = {}) noexcept {
            return std::isfinite(value.x) &&
                    std::isfinite(value.y) &&
                    std::isfinite(value.z)
                ? ToProject(value)
                : fallback;
        }

        GameProjectQuat FiniteOr(
            const MATH::Quat& value,
            const GameProjectQuat& fallback = {}) noexcept {
            return std::isfinite(value.x) &&
                    std::isfinite(value.y) &&
                    std::isfinite(value.z) &&
                    std::isfinite(value.w)
                ? ToProject(value)
                : fallback;
        }

        RuntimeObjectHandle DecodeRuntimeHandle(
            uint64_t value) noexcept {
            if (value == 0u) {
                return {};
            }
            const uint32_t encodedSlot = static_cast<uint32_t>(
                value & 0xFFFFFFFFu);
            const uint32_t generation = static_cast<uint32_t>(
                value >> 32u);
            if (encodedSlot == 0u || generation == 0u) {
                return {};
            }
            RuntimeObjectHandle result{};
            result.slot = encodedSlot - 1u;
            result.generation = generation;
            return result.IsValid() ? result : RuntimeObjectHandle{};
        }

        GameObject* FindObject(
            World* world,
            uint64_t handle) noexcept {
            if (world == nullptr) {
                return nullptr;
            }
            const RuntimeObjectHandle runtimeHandle =
                DecodeRuntimeHandle(handle);
            return runtimeHandle.IsValid()
                ? world->FindObject(runtimeHandle)
                : nullptr;
        }

        const GameObject* FindObject(
            const World* world,
            uint64_t handle) noexcept {
            if (world == nullptr) {
                return nullptr;
            }
            const RuntimeObjectHandle runtimeHandle =
                DecodeRuntimeHandle(handle);
            return runtimeHandle.IsValid()
                ? world->FindObject(runtimeHandle)
                : nullptr;
        }

        bool HasClipIfModelLoaded(
            const GameObject& object,
            std::string_view clipName) noexcept {
            const ModelComponent* model = object.GetComponent<
                ModelComponent>();
            const ModelAsset* asset = model != nullptr
                ? model->GetModelAsset()
                : nullptr;
            return asset == nullptr ||
                asset->FindAnimationClip(clipName) != nullptr;
        }

        uint64_t ResolvePublicClipId(
            const ModelAsset* model,
            const AnimationClip* clip,
            const ANIMATION::AnimationClipReference& reference) noexcept {
            if (reference.clipId.IsValid()) {
                return reference.clipId.value;
            }
            if (model != nullptr && clip != nullptr) {
                for (size_t index = 0u;
                        index < model->animations.size();
                        ++index) {
                    if (&model->animations[index] == clip) {
                        return model->GetAnimationClipId(index).value;
                    }
                }
            }
            if (reference.fallbackName.empty()) {
                return 0u;
            }
            const uint64_t hash = HASH::Fnv1aString(
                reference.fallbackName);
            return hash != 0u ? hash : 1u;
        }

        bool IsFinitePositive(float value) noexcept {
            return std::isfinite(value) && value > 0.0f;
        }

        bool IsFiniteNonNegative(float value) noexcept {
            return std::isfinite(value) && value >= 0.0f;
        }

        bool IsFiniteNonZeroDirection(
            const GameProjectFloat3& value) noexcept {
            if (!IsFinite(value)) {
                return false;
            }
            const float lengthSquared = value.x * value.x +
                value.y * value.y + value.z * value.z;
            return std::isfinite(lengthSquared) &&
                lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
        }

        bool IsFiniteNonZeroRotation(
            const GameProjectQuat& value) noexcept {
            if (!IsFinite(value)) {
                return false;
            }
            const float lengthSquared = value.x * value.x +
                value.y * value.y + value.z * value.z +
                value.w * value.w;
            return std::isfinite(lengthSquared) &&
                lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
        }

        GameProjectInputDevice ToProjectDevice(
            INPUT::InputDeviceKind device) noexcept {
            switch (device) {
            case INPUT::InputDeviceKind::KeyboardMouse:
                return GameProjectInputDevice::KeyboardMouse;
            case INPUT::InputDeviceKind::Gamepad:
                return GameProjectInputDevice::Gamepad;
            case INPUT::InputDeviceKind::None:
            default:
                return GameProjectInputDevice::None;
            }
        }

        bool ToProjectInputState(
            const INPUT::InputActionState& source,
            GameProjectInputActionState& outState) noexcept {
            if (!std::isfinite(source.x) ||
                !std::isfinite(source.y) ||
                !std::isfinite(source.heldSeconds) ||
                source.heldSeconds < 0.0f) {
                return false;
            }
            switch (source.valueType) {
            case INPUT::InputActionValueType::Button:
                outState.valueType = GameProjectInputValueType::Button;
                break;
            case INPUT::InputActionValueType::Axis1D:
                outState.valueType = GameProjectInputValueType::Axis1D;
                break;
            case INPUT::InputActionValueType::Axis2D:
                outState.valueType = GameProjectInputValueType::Axis2D;
                break;
            default:
                return false;
            }
            outState.x = source.x;
            outState.y = source.y;
            outState.heldSeconds = source.heldSeconds;
            outState.down = source.down ? 1u : 0u;
            outState.pressed = source.pressed ? 1u : 0u;
            outState.released = source.released ? 1u : 0u;
            return true;
        }

        bool ToPhysicsFilter(
            World& world,
            const GameProjectPhysicsQueryFilter& source,
            PHYSICS::PhysicsQueryFilter& outFilter) noexcept {
            outFilter = {};
            outFilter.layerMask = source.layerMask;
            outFilter.includeTriggers = source.includeTriggers != 0u;
            if (source.ignoredObjectHandle == 0u) {
                return true;
            }
            const RuntimeObjectHandle ignored = DecodeRuntimeHandle(
                source.ignoredObjectHandle);
            if (!ignored.IsValid() || world.FindObject(ignored) == nullptr) {
                return false;
            }
            outFilter.ignoredObject = ignored;
            return true;
        }

        bool ToPhysicsShape(
            const GameProjectPhysicsShape& source,
            PHYSICS::PhysicsShapeDesc& outShape) noexcept {
            if (!IsFinite(source.center) ||
                !IsFinite(source.rotation)) {
                return false;
            }
            if (!IsFiniteNonZeroRotation(source.rotation)) {
                return false;
            }
            outShape = {};
            outShape.localCenter = ToRuntime(source.center);
            outShape.localRotation = ToRuntime(source.rotation);
            switch (source.type) {
            case GameProjectPhysicsShapeType::Box:
                if (!IsFinite(source.halfExtents) ||
                    source.halfExtents.x <= 0.0f ||
                    source.halfExtents.y <= 0.0f ||
                    source.halfExtents.z <= 0.0f) {
                    return false;
                }
                outShape.type = PHYSICS::PhysicsShapeType::Box;
                outShape.halfExtents = ToRuntime(source.halfExtents);
                break;
            case GameProjectPhysicsShapeType::Sphere:
                if (!IsFinitePositive(source.radius)) {
                    return false;
                }
                outShape.type = PHYSICS::PhysicsShapeType::Sphere;
                outShape.radius = source.radius;
                break;
            case GameProjectPhysicsShapeType::Capsule:
                if (!IsFinitePositive(source.radius) ||
                    !IsFinitePositive(source.height)) {
                    return false;
                }
                outShape.type = PHYSICS::PhysicsShapeType::Capsule;
                outShape.radius = source.radius;
                outShape.height = source.height;
                break;
            default:
                return false;
            }
            outShape.isTrigger = source.isTrigger != 0u;
            return true;
        }

        GameProjectPhysicsHit ToProjectHit(
            const PHYSICS::PhysicsHit& source) noexcept {
            GameProjectPhysicsHit result{};
            result.objectHandle = source.object.ToValue();
            result.bodyHandle = source.body.ToValue();
            result.sourceShapeId = source.shapeKey.sourceShapeId;
            result.componentOrdinal = source.shapeKey.componentOrdinal;
            result.position = FiniteOr(source.position);
            result.normal = FiniteOr(source.normal);
            result.distance = std::isfinite(source.distance)
                ? source.distance
                : 0.0f;
            result.fraction = std::isfinite(source.fraction)
                ? source.fraction
                : 0.0f;
            result.shapeIndex = source.shapeIndex;
            result.isTrigger = source.isTrigger ? 1u : 0u;
            return result;
        }

        bool ToCameraObjectId(
            World& world,
            uint64_t runtimeHandle,
            SceneObjectId& outObjectId) noexcept {
            outObjectId = {};
            if (runtimeHandle == 0u) {
                return true;
            }
            GameObject* object = FindObject(&world, runtimeHandle);
            if (object == nullptr) {
                return false;
            }
            outObjectId = object->GetDocumentId();
            return outObjectId.value != 0u;
        }

    } // namespace

    InputView GameProjectSdkAccess::MakeInputView(
        World& world) noexcept {
        return InputView(&world);
    }

    AnimationView GameProjectSdkAccess::MakeAnimationView(
        World& world) noexcept {
        return AnimationView(&world);
    }

    MotionView GameProjectSdkAccess::MakeMotionView(
        World& world) noexcept {
        return MotionView(&world);
    }

    PhysicsQueryView GameProjectSdkAccess::MakePhysicsQueryView(
        World& world) noexcept {
        return PhysicsQueryView(&world);
    }

    CameraView GameProjectSdkAccess::MakeCameraView(
        World& world) noexcept {
        return CameraView(&world);
    }

    TimeDilationView GameProjectSdkAccess::MakeTimeDilationView(
        World& world) noexcept {
        return TimeDilationView(&world);
    }

    bool InputView::GetAction(
        std::string_view actionId,
        GameProjectInputActionState& outState) const noexcept {
        outState = {};
        try {
            if (world_ == nullptr || actionId.empty()) {
                return false;
            }
            auto* world = static_cast<World*>(world_);
            INPUT::InputService* input = world->Services().Find<
                INPUT::InputService>();
            if (input == nullptr) {
                return false;
            }
            const INPUT::InputActionState* state =
                input->GetSnapshot().FindAction(std::string(actionId));
            return state != nullptr && ToProjectInputState(*state, outState);
        }
        catch (...) {
            outState = {};
            return false;
        }
    }

    GameProjectInputDevice InputView::GetLastActiveDevice() const noexcept {
        try {
            if (world_ == nullptr) {
                return GameProjectInputDevice::None;
            }
            auto* world = static_cast<World*>(world_);
            const INPUT::InputService* input = world->Services().Find<
                INPUT::InputService>();
            return input != nullptr
                ? ToProjectDevice(input->GetSnapshot().GetLastActiveDevice())
                : GameProjectInputDevice::None;
        }
        catch (...) {
            return GameProjectInputDevice::None;
        }
    }

    uint64_t InputView::GetFrameIndex() const noexcept {
        try {
            if (world_ == nullptr) {
                return 0u;
            }
            auto* world = static_cast<World*>(world_);
            const INPUT::InputService* input = world->Services().Find<
                INPUT::InputService>();
            return input != nullptr
                ? input->GetSnapshot().GetFrameIndex()
                : 0u;
        }
        catch (...) {
            return 0u;
        }
    }

    bool InputView::SetGamepadVibration(
        float lowFrequency,
        float highFrequency,
        float durationSeconds) const noexcept {
        try {
            if (world_ == nullptr ||
                !std::isfinite(lowFrequency) ||
                !std::isfinite(highFrequency) ||
                !std::isfinite(durationSeconds) ||
                durationSeconds < 0.0f) {
                return false;
            }
            auto* world = static_cast<World*>(world_);
            INPUT::InputService* input = world->Services().Find<
                INPUT::InputService>();
            return input != nullptr && input->SetGamepadVibration(
                std::clamp(lowFrequency, 0.0f, 1.0f),
                std::clamp(highFrequency, 0.0f, 1.0f),
                durationSeconds);
        }
        catch (...) {
            return false;
        }
    }

    void InputView::StopGamepadVibration() const noexcept {
        try {
            if (world_ == nullptr) {
                return;
            }
            auto* world = static_cast<World*>(world_);
            if (INPUT::InputService* input = world->Services().Find<
                    INPUT::InputService>()) {
                input->StopGamepadVibration();
            }
        }
        catch (...) {
        }
    }

    bool AnimationView::Play(
        uint64_t objectHandle,
        std::string_view clipName,
        uint8_t loop,
        uint8_t restart) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            if (object == nullptr || clipName.empty()) {
                return false;
            }
            if (!HasClipIfModelLoaded(*object, clipName)) {
                return false;
            }
            AnimatorComponent* animator = object->GetComponent<
                AnimatorComponent>();
            if (animator == nullptr) {
                return false;
            }
            animator->Play(std::string(clipName), loop != 0u, restart != 0u);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::CrossFade(
        uint64_t objectHandle,
        std::string_view clipName,
        float blendSeconds,
        uint8_t loop,
        uint8_t restart) const noexcept {
        try {
            if (!IsFiniteNonNegative(blendSeconds)) {
                return false;
            }
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            if (object == nullptr || clipName.empty()) {
                return false;
            }
            if (!HasClipIfModelLoaded(*object, clipName)) {
                return false;
            }
            AnimatorComponent* animator = object->GetComponent<
                AnimatorComponent>();
            if (animator == nullptr) {
                return false;
            }
            animator->CrossFade(
                std::string(clipName),
                blendSeconds,
                loop != 0u,
                restart != 0u);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::PlayOnce(
        uint64_t objectHandle,
        std::string_view clipName,
        uint8_t restart) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            if (object == nullptr || clipName.empty()) {
                return false;
            }
            if (!HasClipIfModelLoaded(*object, clipName)) {
                return false;
            }
            AnimatorComponent* animator = object->GetComponent<
                AnimatorComponent>();
            if (animator == nullptr) {
                return false;
            }
            animator->PlayOnce(std::string(clipName), restart != 0u);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::Pause(uint64_t objectHandle) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>()
                : nullptr;
            if (animator == nullptr) {
                return false;
            }
            animator->Pause();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::Resume(uint64_t objectHandle) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>()
                : nullptr;
            if (animator == nullptr) {
                return false;
            }
            animator->Resume();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::Stop(uint64_t objectHandle) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>()
                : nullptr;
            if (animator == nullptr) {
                return false;
            }
            animator->Stop();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::SetRootMotionEnabled(
        uint64_t objectHandle,
        uint8_t enabled) const noexcept {
        try {
            GameObject* object = FindObject(
                static_cast<World*>(world_), objectHandle);
            AnimatorComponent* animator = object != nullptr
                ? object->GetComponent<AnimatorComponent>()
                : nullptr;
            if (animator == nullptr) {
                return false;
            }
            animator->SetRootMotionEnabled(enabled != 0u);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool AnimationView::GetState(
        uint64_t objectHandle,
        GameProjectAnimationState& outState) const noexcept {
        outState = {};
        try {
            auto* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, objectHandle);
            if (object == nullptr) {
                return false;
            }
            const AnimatorComponent* animator = object->GetComponent<
                AnimatorComponent>();
            if (animator == nullptr) {
                return false;
            }
            const ModelComponent* model = object->GetComponent<
                ModelComponent>();
            const ModelAsset* modelAsset = model != nullptr
                ? model->GetModelAsset()
                : nullptr;
            const ANIMATION::AnimationClipReference& reference =
                animator->GetClipReference();
            const AnimationClip* clip = modelAsset != nullptr
                ? ANIMATION::ResolveAnimationClip(*modelAsset, reference)
                : nullptr;
            const float duration = clip != nullptr
                ? clip->durationSec
                : 0.0f;
            const float time = animator->GetTime();
            if (!std::isfinite(duration) ||
                !std::isfinite(time) ||
                !std::isfinite(animator->GetSpeed())) {
                return false;
            }

            const ANIMATION::AnimationPoseService* poses = world->Services().Find<
                ANIMATION::AnimationPoseService>();
            const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot>
                snapshot = poses != nullptr
                ? poses->Find(object->GetRuntimeHandle())
                : nullptr;
            const bool snapshotMatchesCurrent = snapshot != nullptr &&
                snapshot->primaryClip == reference;
            float normalizedTime = duration > 0.0f
                ? std::clamp(time / duration, 0.0f, 1.0f)
                : 0.0f;
            if (snapshotMatchesCurrent &&
                std::isfinite(snapshot->normalizedTime)) {
                normalizedTime = std::clamp(
                    snapshot->normalizedTime,
                    0.0f,
                    1.0f);
            }
            outState.clipId = ResolvePublicClipId(
                modelAsset,
                clip,
                reference);
            outState.poseRevision = snapshotMatchesCurrent
                ? snapshot->revision
                : 0u;
            if (snapshotMatchesCurrent &&
                snapshot->primaryClip.clipId.IsValid()) {
                outState.clipId = snapshot->primaryClip.clipId.value;
            }
            outState.timeSeconds = time;
            outState.durationSeconds = duration;
            outState.normalizedTime = normalizedTime;
            outState.speed = animator->GetSpeed();
            outState.transitionWeight = snapshotMatchesCurrent &&
                    std::isfinite(snapshot->blendWeight)
                ? std::clamp(snapshot->blendWeight, 0.0f, 1.0f)
                : std::isfinite(animator->GetTransitionWeight())
                    ? std::clamp(
                        animator->GetTransitionWeight(),
                        0.0f,
                        1.0f)
                    : 1.0f;
            outState.playing = animator->IsPlaying() ? 1u : 0u;
            outState.finished = animator->IsFinished(duration) ? 1u : 0u;
            outState.transitioning = animator->IsTransitioning() ? 1u : 0u;
            outState.loop = animator->GetLoop() ? 1u : 0u;
            outState.valid = snapshotMatchesCurrent && snapshot->valid
                ? 1u
                : 0u;
            return true;
        }
        catch (...) {
            outState = {};
            return false;
        }
    }

    bool AnimationView::GetSocketTransform(
        uint64_t objectHandle,
        std::string_view socketName,
        GameProjectAnimationSocketSpace space,
        GameProjectTransform& outTransform) const noexcept {
        outTransform = {};
        try {
            World* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, objectHandle);
            const ModelComponent* model = object != nullptr
                ? object->GetComponent<ModelComponent>()
                : nullptr;
            const ModelAsset* asset = model != nullptr
                ? model->GetModelAsset()
                : nullptr;
            const ANIMATION::AnimationSocketService* sockets =
                world != nullptr
                ? world->Services().Find<
                    ANIMATION::AnimationSocketService>()
                : nullptr;
            if (object == nullptr || asset == nullptr || sockets == nullptr ||
                socketName.empty()) {
                return false;
            }
            ANIMATION::AnimationSocketSpace runtimeSpace{};
            switch (space) {
            case GameProjectAnimationSocketSpace::Local:
                runtimeSpace = ANIMATION::AnimationSocketSpace::Local;
                break;
            case GameProjectAnimationSocketSpace::World:
                runtimeSpace = ANIMATION::AnimationSocketSpace::World;
                break;
            default:
                return false;
            }
            const ANIMATION::AnimationPoseService* poses =
                world->Services().Find<ANIMATION::AnimationPoseService>();
            const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot>
                pose = poses != nullptr
                ? poses->Find(object->GetRuntimeHandle())
                : nullptr;
            Transform3D resolved{};
            if (!sockets->Resolve(
                    *object,
                    *asset,
                    pose.get(),
                    socketName,
                    runtimeSpace,
                    resolved) ||
                !MATH::IsFinite(resolved.position) ||
                !MATH::IsFinite(resolved.rotation) ||
                !MATH::IsFinite(resolved.scale)) {
                return false;
            }
            outTransform.position = ToProject(resolved.position);
            outTransform.rotation = ToProject(resolved.rotation);
            outTransform.scale = ToProject(resolved.scale);
            return true;
        }
        catch (...) {
            outTransform = {};
            return false;
        }
    }

    bool AnimationView::ConsumeRootMotion(
        uint64_t objectHandle,
        uint64_t fixedTickIndex,
        GameProjectRootMotionDelta& outDelta) const noexcept {
        outDelta = {};
        try {
            World* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, objectHandle);
            ANIMATION::AnimationRootMotionService* rootMotion =
                world != nullptr
                ? world->Services().Find<
                    ANIMATION::AnimationRootMotionService>()
                : nullptr;
            if (object == nullptr || rootMotion == nullptr) {
                return false;
            }
            ANIMATION::AnimationRootMotionDelta delta{};
            if (!rootMotion->Consume(
                    object->GetRuntimeHandle(),
                    fixedTickIndex,
                    delta) ||
                !delta.valid ||
                !MATH::IsFinite(delta.translation) ||
                !MATH::IsFinite(delta.rotation)) {
                return false;
            }
            outDelta.translation = ToProject(delta.translation);
            outDelta.rotation = ToProject(delta.rotation);
            outDelta.fixedTickIndex = delta.fixedTickIndex;
            outDelta.revision = delta.revision;
            outDelta.valid = 1u;
            return true;
        }
        catch (...) {
            outDelta = {};
            return false;
        }
    }

    bool MotionView::SubmitKinematicMove(
        const GameProjectKinematicMove& move) const noexcept {
        try {
            if (world_ == nullptr || move.sourceId == 0u ||
                !IsFinite(move.horizontalVelocity) ||
                !IsFinite(move.desiredRotation) ||
                !IsFiniteNonNegative(move.jumpSpeed) ||
                !IsFinitePositive(move.maximumFallSpeed) ||
                (move.hasDesiredRotation != 0u &&
                    !IsFiniteNonZeroRotation(move.desiredRotation))) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, move.objectHandle);
            if (object == nullptr) {
                return false;
            }
            PHYSICS::KinematicMotionService* service = world->Services().Find<
                PHYSICS::KinematicMotionService>();
            if (service == nullptr) {
                return false;
            }
            PHYSICS::KinematicMotionRequest request{};
            request.horizontalVelocity = ToRuntime(move.horizontalVelocity);
            request.desiredRotation = ToRuntime(move.desiredRotation);
            request.jumpSpeed = move.jumpSpeed;
            request.maximumFallSpeed = move.maximumFallSpeed;
            request.hasDesiredRotation = move.hasDesiredRotation != 0u;
            request.jumpRequested = move.jumpRequested != 0u;
            request.allowJumpWithoutGroundContact =
                move.allowJumpWithoutGroundContact != 0u;
            service->SubmitMove(
                object->GetRuntimeHandle(),
                move.sourceId,
                move.priority,
                request,
                move.fixedTickIndex);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool MotionView::SubmitMotionDelta(
        const GameProjectKinematicMotionDelta& delta) const noexcept {
        try {
            if (world_ == nullptr || delta.sourceId == 0u ||
                !IsFinite(delta.translation) ||
                !IsFiniteNonZeroRotation(delta.rotation)) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, delta.objectHandle);
            if (object == nullptr) {
                return false;
            }
            PHYSICS::KinematicMotionService* service =
                world->Services().Find<PHYSICS::KinematicMotionService>();
            return service != nullptr && service->SubmitMotionDelta(
                object->GetRuntimeHandle(),
                delta.sourceId,
                delta.priority,
                delta.fixedTickIndex,
                ToRuntime(delta.translation),
                ToRuntime(delta.rotation));
        }
        catch (...) {
            return false;
        }
    }

    bool MotionView::GetKinematicState(
        uint64_t objectHandle,
        GameProjectKinematicState& outState) const noexcept {
        outState = {};
        try {
            World* world = static_cast<World*>(world_);
            GameObject* object = FindObject(world, objectHandle);
            if (object == nullptr) {
                return false;
            }
            const PHYSICS::KinematicMotionService* service =
                world->Services().Find<PHYSICS::KinematicMotionService>();
            const PHYSICS::KinematicMotionState* state = service != nullptr
                ? service->FindState(object->GetRuntimeHandle())
                : nullptr;
            if (state == nullptr) {
                return false;
            }
            outState.pose.position = FiniteOr(state->pose.position);
            outState.pose.rotation = FiniteOr(state->pose.rotation);
            outState.pose.scale = FiniteOr(object->GetTransform().scale, {
                1.0f, 1.0f, 1.0f
            });
            outState.velocity = FiniteOr(state->velocity);
            outState.groundVelocity = FiniteOr(state->groundVelocity);
            outState.groundPosition = FiniteOr(state->groundPosition);
            outState.groundNormal = FiniteOr(
                state->groundNormal,
                { 0.0f, 1.0f, 0.0f });
            outState.groundObjectHandle = state->groundObject.ToValue();
            switch (state->groundState) {
            case PHYSICS::PhysicsCharacterGroundState::OnGround:
                outState.groundState = GameProjectGroundState::OnGround;
                break;
            case PHYSICS::PhysicsCharacterGroundState::OnSteepGround:
                outState.groundState = GameProjectGroundState::OnSteepGround;
                break;
            case PHYSICS::PhysicsCharacterGroundState::NotSupported:
                outState.groundState = GameProjectGroundState::NotSupported;
                break;
            case PHYSICS::PhysicsCharacterGroundState::InAir:
            default:
                outState.groundState = GameProjectGroundState::InAir;
                break;
            }
            outState.grounded = state->IsGrounded() ? 1u : 0u;
            outState.hitWall = state->hitWall ? 1u : 0u;
            outState.hitCeiling = state->hitCeiling ? 1u : 0u;
            outState.maximumHitsExceeded =
                state->maximumHitsExceeded ? 1u : 0u;
            return true;
        }
        catch (...) {
            outState = {};
            return false;
        }
    }

    bool PhysicsQueryView::Raycast(
        const GameProjectPhysicsRaycastQuery& query,
        GameProjectPhysicsHit& outHit) const noexcept {
        outHit = {};
        try {
            if (world_ == nullptr ||
                !IsFinite(query.origin) ||
                !IsFiniteNonZeroDirection(query.direction) ||
                !IsFinitePositive(query.maxDistance)) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            PHYSICS::PhysicsWorldService* service = world->Services().Find<
                PHYSICS::PhysicsWorldService>();
            if (service == nullptr) {
                return false;
            }
            PHYSICS::PhysicsRaycastQuery runtimeQuery{};
            runtimeQuery.origin = ToRuntime(query.origin);
            runtimeQuery.direction = ToRuntime(query.direction);
            runtimeQuery.maxDistance = query.maxDistance;
            if (!ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
                return false;
            }
            PHYSICS::PhysicsHit hit{};
            if (!service->Raycast(runtimeQuery, hit)) {
                return false;
            }
            outHit = ToProjectHit(hit);
            return true;
        }
        catch (...) {
            outHit = {};
            return false;
        }
    }

    bool PhysicsQueryView::ShapeCast(
        const GameProjectPhysicsShapeCastQuery& query,
        GameProjectPhysicsHit& outHit) const noexcept {
        outHit = {};
        try {
            if (world_ == nullptr ||
                !IsFinite(query.startPosition) ||
                !IsFinite(query.startRotation) ||
                !IsFiniteNonZeroRotation(query.startRotation) ||
                !IsFiniteNonZeroDirection(query.direction) ||
                !IsFinitePositive(query.maxDistance)) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            PHYSICS::PhysicsWorldService* service = world->Services().Find<
                PHYSICS::PhysicsWorldService>();
            if (service == nullptr) {
                return false;
            }
            PHYSICS::PhysicsShapeCastQuery runtimeQuery{};
            if (!ToPhysicsShape(query.shape, runtimeQuery.shape) ||
                !ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
                return false;
            }
            runtimeQuery.startPose.position = ToRuntime(query.startPosition);
            runtimeQuery.startPose.rotation = ToRuntime(query.startRotation);
            runtimeQuery.direction = ToRuntime(query.direction);
            runtimeQuery.maxDistance = query.maxDistance;
            PHYSICS::PhysicsHit hit{};
            if (!service->ShapeCast(runtimeQuery, hit)) {
                return false;
            }
            outHit = ToProjectHit(hit);
            return true;
        }
        catch (...) {
            outHit = {};
            return false;
        }
    }

    uint32_t PhysicsQueryView::Overlap(
        const GameProjectPhysicsOverlapQuery& query,
        GameProjectPhysicsHit* outHits,
        uint32_t capacity) const noexcept {
        try {
            if (world_ == nullptr || outHits == nullptr || capacity == 0u ||
                !IsFinite(query.position) ||
                !IsFinite(query.rotation) ||
                !IsFiniteNonZeroRotation(query.rotation)) {
                return 0u;
            }
            World* world = static_cast<World*>(world_);
            PHYSICS::PhysicsWorldService* service = world->Services().Find<
                PHYSICS::PhysicsWorldService>();
            if (service == nullptr) {
                return 0u;
            }
            PHYSICS::PhysicsOverlapQuery runtimeQuery{};
            if (!ToPhysicsShape(query.shape, runtimeQuery.shape) ||
                !ToPhysicsFilter(*world, query.filter, runtimeQuery.filter)) {
                return 0u;
            }
            runtimeQuery.pose.position = ToRuntime(query.position);
            runtimeQuery.pose.rotation = ToRuntime(query.rotation);
            const std::vector<PHYSICS::PhysicsHit> hits = service->Overlap(
                runtimeQuery);
            const size_t count = (std::min)(
                static_cast<size_t>(capacity),
                hits.size());
            for (size_t index = 0u; index < count; ++index) {
                outHits[index] = ToProjectHit(hits[index]);
            }
            return static_cast<uint32_t>(count);
        }
        catch (...) {
            return 0u;
        }
    }

    bool CameraView::SubmitModifier(
        const GameProjectCameraModifier& modifier) const noexcept {
        try {
            if (world_ == nullptr || modifier.sourceId == 0u ||
                !IsFinite(modifier.worldPositionOffset) ||
                !IsFinite(modifier.localPositionOffset) ||
                !IsFinite(modifier.worldTargetOffset) ||
                !IsFinite(modifier.localTargetOffset) ||
                !std::isfinite(modifier.verticalFovOffsetRadians) ||
                !std::isfinite(modifier.weight)) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            CAMERA::CameraRigService* service = world->Services().Find<
                CAMERA::CameraRigService>();
            if (service == nullptr) {
                return false;
            }
            CAMERA::CameraModifierSubmission submission{};
            if (!ToCameraObjectId(
                    *world,
                    modifier.cameraObjectHandle,
                    submission.cameraObjectId)) {
                return false;
            }
            submission.sourceId = modifier.sourceId;
            submission.priority = modifier.priority;
            submission.worldPositionOffset = ToRuntime(
                modifier.worldPositionOffset);
            submission.localPositionOffset = ToRuntime(
                modifier.localPositionOffset);
            submission.worldTargetOffset = ToRuntime(
                modifier.worldTargetOffset);
            submission.localTargetOffset = ToRuntime(
                modifier.localTargetOffset);
            submission.verticalFovOffsetRadians =
                modifier.verticalFovOffsetRadians;
            submission.weight = modifier.weight;
            submission.applyDuringOverride =
                modifier.applyDuringOverride != 0u;
            return service->SubmitModifier(submission);
        }
        catch (...) {
            return false;
        }
    }

    bool CameraView::SubmitRollModifier(
        const GameProjectCameraRollModifier& modifier) const noexcept {
        try {
            if (world_ == nullptr || modifier.sourceId == 0u ||
                !std::isfinite(modifier.localRollRadians) ||
                !std::isfinite(modifier.weight)) {
                return false;
            }
            World* world = static_cast<World*>(world_);
            CAMERA::CameraRigService* service = world->Services().Find<
                CAMERA::CameraRigService>();
            if (service == nullptr) {
                return false;
            }
            CAMERA::CameraModifierSubmission submission{};
            if (!ToCameraObjectId(
                    *world,
                    modifier.cameraObjectHandle,
                    submission.cameraObjectId)) {
                return false;
            }
            submission.sourceId = modifier.sourceId;
            submission.channelId = 1u;
            submission.priority = modifier.priority;
            submission.localRollRadians = modifier.localRollRadians;
            submission.weight = modifier.weight;
            submission.applyDuringOverride =
                modifier.applyDuringOverride != 0u;
            return service->SubmitModifier(submission);
        }
        catch (...) {
            return false;
        }
    }

    uint64_t TimeDilationView::Request(
        const GameProjectTimeDilationRequest& request) const noexcept {
        try {
            if (world_ == nullptr || request.sourceId == 0u ||
                !std::isfinite(request.scale) ||
                !std::isfinite(request.durationUnscaledSeconds) ||
                request.scale < 0.0f ||
                request.scale > 1.0f ||
                request.durationUnscaledSeconds < 0.0f) {
                return 0u;
            }
            return SERVICES::GetActionTimeDilationService().Request(
                static_cast<World*>(world_),
                request.sourceId,
                request.priority,
                request.scale,
                request.durationUnscaledSeconds);
        }
        catch (...) {
            return 0u;
        }
    }

    bool TimeDilationView::Release(uint64_t token) const noexcept {
        try {
            return world_ != nullptr &&
                SERVICES::GetActionTimeDilationService().Release(
                    static_cast<World*>(world_),
                    token);
        }
        catch (...) {
            return false;
        }
    }

    float TimeDilationView::GetEffectiveScale() const noexcept {
        try {
            return world_ != nullptr
                ? SERVICES::GetActionTimeDilationService().GetEffectiveScale(
                    static_cast<World*>(world_))
                : 1.0f;
        }
        catch (...) {
            return 1.0f;
        }
    }

} // namespace HIKARI
