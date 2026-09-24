#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Animation/Runtime/HIKARI_AnimationNodeOverrideService.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationRootMotionService.h"
#include "Animation/Runtime/HIKARI_AnimationSocketService.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;

bool AnimationView::GetSocketTransform(uint64_t objectHandle, std::string_view socketName,
                                       GameProjectAnimationSocketSpace space,
                                       GameProjectTransform& outTransform) const noexcept {
    outTransform = {};
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        const ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr;
        const ModelAsset* asset = model != nullptr ? model->GetModelAsset() : nullptr;
        const ANIMATION::AnimationSocketService* sockets =
            world != nullptr ? world->Services().Find<ANIMATION::AnimationSocketService>() : nullptr;
        if (object == nullptr || asset == nullptr || sockets == nullptr || socketName.empty()) {
            return false;
        }
        ANIMATION::AnimationSocketSpace runtimeSpace{};
        MATH::Mat4 authoritativeObjectWorld{};
        bool usePresentationWorld = false;
        switch (space) {
        case GameProjectAnimationSocketSpace::Local: runtimeSpace = ANIMATION::AnimationSocketSpace::Local; break;
        case GameProjectAnimationSocketSpace::World: runtimeSpace = ANIMATION::AnimationSocketSpace::World; break;
        case GameProjectAnimationSocketSpace::PresentationWorld: {
            authoritativeObjectWorld = object->GetTransform().GetWorldMatrix();
            const PresentationTransformService* presentationTransforms =
                world->Services().Find<PresentationTransformService>();
            MATH::Mat4 presentationWorld{};
            if (presentationTransforms != nullptr &&
                presentationTransforms->TryGetWorldMatrix(object->GetRuntimeHandle(), presentationWorld) &&
                MATH::IsFinite(presentationWorld)) {
                authoritativeObjectWorld = presentationWorld;
            }
            runtimeSpace = ANIMATION::AnimationSocketSpace::World;
            usePresentationWorld = true;
            break;
        }
        default: return false;
        }
        const ANIMATION::AnimationPoseService* poses = world->Services().Find<ANIMATION::AnimationPoseService>();
        const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> pose =
            poses != nullptr ? poses->Find(object->GetRuntimeHandle()) : nullptr;
        Transform3D resolved{};
        const bool socketResolved =
            usePresentationWorld
                ? sockets->Resolve(*asset, pose.get(), authoritativeObjectWorld, socketName, runtimeSpace, resolved)
                : sockets->Resolve(*object, *asset, pose.get(), socketName, runtimeSpace, resolved);
        if (!socketResolved || !MATH::IsFinite(resolved.position) || !MATH::IsFinite(resolved.rotation) ||
            !MATH::IsFinite(resolved.scale)) {
            return false;
        }
        outTransform.position = ToProject(resolved.position);
        outTransform.rotation = ToProject(resolved.rotation);
        outTransform.scale = ToProject(resolved.scale);
        return true;
    } catch (...) {
        outTransform = {};
        return false;
    }
}

bool AnimationView::SetEvaluationEnabled(uint64_t objectHandle, uint8_t enabled) const noexcept {
    try {
        GameObject* object = FindObject(static_cast<World*>(world_), objectHandle);
        AnimatorComponent* animator = object != nullptr ? object->GetComponent<AnimatorComponent>() : nullptr;
        if (animator == nullptr) {
            return false;
        }
        animator->SetRuntimeEvaluationEnabled(enabled != 0u);
        return true;
    } catch (...) {
        return false;
    }
}

bool AnimationView::SetNodeLocalTransformOffset(uint64_t objectHandle, std::string_view nodeName,
                                                const GameProjectTransform& offset) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        ANIMATION::AnimationNodeOverrideService* overrides =
            world != nullptr ? world->Services().Find<ANIMATION::AnimationNodeOverrideService>() : nullptr;
        if (object == nullptr || overrides == nullptr || nodeName.empty() || !IsValidTransform(offset)) {
            return false;
        }
        Transform3D runtimeOffset{};
        runtimeOffset.position = ToRuntime(offset.position);
        runtimeOffset.rotation = MATH::NormalizeQ(ToRuntime(offset.rotation));
        runtimeOffset.scale = ToRuntime(offset.scale);
        return overrides->SetLocalOffset(object->GetRuntimeHandle(), nodeName, runtimeOffset);
    } catch (...) {
        return false;
    }
}

bool AnimationView::SetNodeParentLocalTransformOffset(uint64_t objectHandle, std::string_view nodeName,
                                                      const GameProjectTransform& offset) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        ANIMATION::AnimationNodeOverrideService* overrides =
            world != nullptr ? world->Services().Find<ANIMATION::AnimationNodeOverrideService>() : nullptr;
        if (object == nullptr || overrides == nullptr || nodeName.empty() || !IsValidTransform(offset)) {
            return false;
        }
        Transform3D runtimeOffset{};
        runtimeOffset.position = ToRuntime(offset.position);
        runtimeOffset.rotation = MATH::NormalizeQ(ToRuntime(offset.rotation));
        runtimeOffset.scale = ToRuntime(offset.scale);
        return overrides->SetParentLocalOffset(object->GetRuntimeHandle(), nodeName, runtimeOffset);
    } catch (...) {
        return false;
    }
}

bool AnimationView::ClearNodeLocalTransformOffset(uint64_t objectHandle, std::string_view nodeName) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        ANIMATION::AnimationNodeOverrideService* overrides =
            world != nullptr ? world->Services().Find<ANIMATION::AnimationNodeOverrideService>() : nullptr;
        return object != nullptr && overrides != nullptr &&
               overrides->ClearLocalOffset(object->GetRuntimeHandle(), nodeName);
    } catch (...) {
        return false;
    }
}

bool AnimationView::SampleSocketTransformsAtTime(uint64_t objectHandle, std::string_view clipName, float timeSeconds,
                                                 uint8_t stripRootMotion, const std::string_view* socketNames,
                                                 uint32_t socketCount, GameProjectAnimationSocketSpace space,
                                                 GameProjectTransform* outTransforms) const noexcept {
    constexpr uint32_t kMaximumSampledSockets = 8u;
    if (outTransforms != nullptr) {
        for (uint32_t index = 0u; index < socketCount; ++index) {
            outTransforms[index] = {};
        }
    }
    try {
        if (world_ == nullptr || clipName.empty() || !std::isfinite(timeSeconds) || timeSeconds < 0.0f ||
            stripRootMotion > 1u || socketNames == nullptr || outTransforms == nullptr || socketCount == 0u ||
            socketCount > kMaximumSampledSockets) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        const ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr;
        const ModelAsset* asset = model != nullptr ? model->GetModelAsset() : nullptr;
        if (object == nullptr || asset == nullptr) {
            return false;
        }

        ANIMATION::AnimationClipReference reference{};
        reference.fallbackName = std::string(clipName);
        ANIMATION::AnimationLocalPose sampledPose{};
        if (!ANIMATION::SampleAnimationClip(*asset, reference, timeSeconds, false, sampledPose)) {
            return false;
        }
        if (stripRootMotion != 0u && !ANIMATION::POSE_EVALUATION::StripRootMotionFromPose(*asset, sampledPose)) {
            return false;
        }
        const ANIMATION::AnimationNodeOverrideService* overrides =
            world->Services().Find<ANIMATION::AnimationNodeOverrideService>();
        if (overrides != nullptr) {
            (void)overrides->Apply(*asset, object->GetRuntimeHandle(), sampledPose);
        }
        std::vector<MATH::Mat4> globals{};
        if (!ANIMATION::POSE_EVALUATION::BuildNodeGlobalMatrices(*asset, &sampledPose, globals)) {
            return false;
        }

        bool worldSpace = false;
        MATH::Mat4 objectWorld = MATH::Mat4::Identity();
        switch (space) {
        case GameProjectAnimationSocketSpace::Local: break;
        case GameProjectAnimationSocketSpace::World:
            objectWorld = object->GetTransform().GetWorldMatrix();
            worldSpace = true;
            break;
        case GameProjectAnimationSocketSpace::PresentationWorld: {
            objectWorld = object->GetTransform().GetWorldMatrix();
            const PresentationTransformService* presentation = world->Services().Find<PresentationTransformService>();
            MATH::Mat4 presented{};
            if (presentation != nullptr && presentation->TryGetWorldMatrix(object->GetRuntimeHandle(), presented) &&
                MATH::IsFinite(presented)) {
                objectWorld = presented;
            }
            worldSpace = true;
            break;
        }
        default: return false;
        }
        if (worldSpace && !MATH::IsFinite(objectWorld)) {
            return false;
        }

        std::array<Transform3D, kMaximumSampledSockets> resolved{};
        for (uint32_t index = 0u; index < socketCount; ++index) {
            int nodeIndex = -1;
            if (socketNames[index].empty() ||
                !ANIMATION::POSE_EVALUATION::ResolveNodeByExactName(*asset, socketNames[index], nodeIndex) ||
                nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= globals.size()) {
                return false;
            }
            const MATH::Mat4 socketMatrix = worldSpace ? objectWorld * globals[static_cast<size_t>(nodeIndex)]
                                                       : globals[static_cast<size_t>(nodeIndex)];
            if (!ANIMATION::POSE_EVALUATION::DecomposeTransform(socketMatrix, resolved[index]) ||
                !MATH::IsFinite(resolved[index].position) || !MATH::IsFinite(resolved[index].rotation) ||
                !MATH::IsFinite(resolved[index].scale)) {
                return false;
            }
        }
        for (uint32_t index = 0u; index < socketCount; ++index) {
            outTransforms[index].position = ToProject(resolved[index].position);
            outTransforms[index].rotation = ToProject(resolved[index].rotation);
            outTransforms[index].scale = ToProject(resolved[index].scale);
        }
        return true;
    } catch (...) {
        if (outTransforms != nullptr) {
            for (uint32_t index = 0u; index < socketCount; ++index) {
                outTransforms[index] = {};
            }
        }
        return false;
    }
}

bool AnimationView::SampleRootMotionDeltaAtTime(uint64_t objectHandle, std::string_view clipName,
                                                float previousTimeSeconds, float currentTimeSeconds,
                                                GameProjectRootMotionDelta& outDelta) const noexcept {
    outDelta = {};
    try {
        if (world_ == nullptr || clipName.empty() || !std::isfinite(previousTimeSeconds) ||
            !std::isfinite(currentTimeSeconds) || previousTimeSeconds < 0.0f ||
            currentTimeSeconds < previousTimeSeconds) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        const ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr;
        const ModelAsset* asset = model != nullptr ? model->GetModelAsset() : nullptr;
        if (object == nullptr || asset == nullptr) {
            return false;
        }

        ANIMATION::AnimationClipReference reference{};
        reference.fallbackName = std::string(clipName);
        ANIMATION::AnimationPoseSnapshot previous{};
        ANIMATION::AnimationPoseSnapshot current{};
        if (!ANIMATION::SampleAnimationClip(*asset, reference, previousTimeSeconds, false, previous.localPose,
                                            &previous.normalizedTime) ||
            !ANIMATION::SampleAnimationClip(*asset, reference, currentTimeSeconds, false, current.localPose,
                                            &current.normalizedTime) ||
            !ANIMATION::POSE_EVALUATION::CaptureRootMotionSample(*asset, previous.localPose,
                                                                 previous.rootMotionSample) ||
            !ANIMATION::POSE_EVALUATION::CaptureRootMotionSample(*asset, current.localPose, current.rootMotionSample)) {
            return false;
        }
        previous.primaryClip = reference;
        current.primaryClip = reference;
        previous.valid = true;
        current.valid = true;
        previous.loop = false;
        current.loop = false;
        previous.playbackSpeed = 1.0f;
        current.playbackSpeed = 1.0f;
        ANIMATION::AnimationRootMotionDelta sampled{};
        if (!ANIMATION::EvaluateAnimationRootMotion(*asset, &previous, current, sampled) || !sampled.valid ||
            !MATH::IsFinite(sampled.translation) || !MATH::IsFinite(sampled.rotation)) {
            return false;
        }
        outDelta.translation = ToProject(sampled.translation);
        outDelta.rotation = ToProject(sampled.rotation);
        outDelta.valid = 1u;
        return true;
    } catch (...) {
        outDelta = {};
        return false;
    }
}

bool AnimationView::ConsumeRootMotion(uint64_t objectHandle, uint64_t fixedTickIndex,
                                      GameProjectRootMotionDelta& outDelta) const noexcept {
    outDelta = {};
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        ANIMATION::AnimationRootMotionService* rootMotion =
            world != nullptr ? world->Services().Find<ANIMATION::AnimationRootMotionService>() : nullptr;
        if (object == nullptr || rootMotion == nullptr) {
            return false;
        }
        ANIMATION::AnimationRootMotionDelta delta{};
        if (!rootMotion->Consume(object->GetRuntimeHandle(), fixedTickIndex, delta) || !delta.valid ||
            !MATH::IsFinite(delta.translation) || !MATH::IsFinite(delta.rotation)) {
            return false;
        }
        outDelta.translation = ToProject(delta.translation);
        outDelta.rotation = ToProject(delta.rotation);
        outDelta.fixedTickIndex = delta.fixedTickIndex;
        outDelta.revision = delta.revision;
        outDelta.valid = 1u;
        return true;
    } catch (...) {
        outDelta = {};
        return false;
    }
}
} // namespace HIKARI
