#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
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
#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;
namespace {
template <class Edit>
bool EditAnimator(void* world, uint64_t handle, Edit&& edit) noexcept {
    try {
        auto* object = FindObject(static_cast<World*>(world), handle);
        auto* animator = object ? object->GetComponent<AnimatorComponent>() : nullptr;
        return animator && edit(*object, *animator);
    } catch (...) {
        return false;
    }
}

bool HasClipIfModelLoaded(const GameObject& object, std::string_view clipName) noexcept {
    const ModelComponent* model = object.GetComponent<ModelComponent>();
    const ModelAsset* asset = model != nullptr ? model->GetModelAsset() : nullptr;
    return asset == nullptr || asset->FindAnimationClip(clipName) != nullptr;
}

uint64_t ResolvePublicClipId(const ModelAsset* model, const AnimationClip* clip,
                             const ANIMATION::AnimationClipReference& reference) noexcept {
    if (reference.clipId.IsValid()) {
        return reference.clipId.value;
    }
    if (model != nullptr && clip != nullptr) {
        for (size_t index = 0u; index < model->animations.size(); ++index) {
            if (&model->animations[index] == clip) {
                return model->GetAnimationClipId(index).value;
            }
        }
    }
    if (reference.fallbackName.empty()) {
        return 0u;
    }
    const uint64_t hash = HASH::Fnv1aString(reference.fallbackName);
    return hash != 0u ? hash : 1u;
}
} // namespace

AnimationView GameProjectSdkAccess::MakeAnimationView(World& world) noexcept { return AnimationView(&world); }

AnimationView WorldView::GetAnimation() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeAnimationView(*world) : AnimationView{};
}

bool AnimationView::Play(uint64_t objectHandle, std::string_view clipName, uint8_t loop,
                         uint8_t restart) const noexcept {
    if (clipName.empty())
        return false;
    return EditAnimator(world_, objectHandle, [&](GameObject& object, AnimatorComponent& animator) {
        if (!HasClipIfModelLoaded(object, clipName))
            return false;
        animator.Play(std::string(clipName), loop != 0u, restart != 0u);
        return true;
    });
}

bool AnimationView::CrossFade(uint64_t objectHandle, std::string_view clipName, float blendSeconds, uint8_t loop,
                              uint8_t restart) const noexcept {
    if (clipName.empty())
        return false;
    if (!IsFiniteNonNegative(blendSeconds))
        return false;
    return EditAnimator(world_, objectHandle, [&](GameObject& object, AnimatorComponent& animator) {
        if (!HasClipIfModelLoaded(object, clipName))
            return false;
        animator.CrossFade(std::string(clipName), blendSeconds, loop != 0u, restart != 0u);
        return true;
    });
}

bool AnimationView::PlayOnce(uint64_t objectHandle, std::string_view clipName, uint8_t restart) const noexcept {
    if (clipName.empty())
        return false;
    return EditAnimator(world_, objectHandle, [&](GameObject& object, AnimatorComponent& animator) {
        if (!HasClipIfModelLoaded(object, clipName))
            return false;
        animator.PlayOnce(std::string(clipName), restart != 0u);
        return true;
    });
}

bool AnimationView::Pause(uint64_t objectHandle) const noexcept {
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.Pause();
        return true;
    });
}

bool AnimationView::Resume(uint64_t objectHandle) const noexcept {
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.Resume();
        return true;
    });
}

bool AnimationView::Stop(uint64_t objectHandle) const noexcept {
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.Stop();
        return true;
    });
}

bool AnimationView::SetSpeed(uint64_t objectHandle, float speed) const noexcept {
    if (!std::isfinite(speed) || speed < 0.05f || speed > 4.0f)
        return false;
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.SetSpeed(speed);
        return true;
    });
}

bool AnimationView::SetMaskedPoseLayer(uint64_t objectHandle, std::string_view clipName,
                                       std::string_view maskRootNodeName, float normalizedTime, float targetWeight,
                                       float blendSeconds) const noexcept {
    try {
        if (clipName.empty() || maskRootNodeName.empty() || !std::isfinite(normalizedTime) || normalizedTime < 0.0f ||
            normalizedTime > 1.0f || !std::isfinite(targetWeight) || targetWeight < 0.0f || targetWeight > 1.0f ||
            !IsFiniteNonNegative(blendSeconds)) {
            return false;
        }
        GameObject* object = FindObject(static_cast<World*>(world_), objectHandle);
        if (object == nullptr) {
            return false;
        }
        AnimatorComponent* animator = object->GetComponent<AnimatorComponent>();
        if (animator == nullptr) {
            return false;
        }
        const ModelComponent* modelComponent = object->GetComponent<ModelComponent>();
        const ModelAsset* modelAsset = modelComponent != nullptr ? modelComponent->GetModelAsset() : nullptr;
        if (modelAsset != nullptr) {
            return animator->SetMaskedPoseLayerResolved(clipName, maskRootNodeName, normalizedTime, targetWeight,
                                                        blendSeconds, *modelAsset);
        }
        animator->SetMaskedPoseLayer(std::string(clipName), std::string(maskRootNodeName), normalizedTime, targetWeight,
                                     blendSeconds);
        return true;
    } catch (...) {
        return false;
    }
}

bool AnimationView::SetMaskedPoseLayerWithRootAlignment(
    uint64_t objectHandle, std::string_view clipName, std::string_view maskRootNodeName, float normalizedTime,
    float targetWeight, float blendSeconds, GameProjectAnimationLayerRootAlignment rootAlignment) const noexcept {
    try {
        if (clipName.empty() || maskRootNodeName.empty() || !std::isfinite(normalizedTime) || normalizedTime < 0.0f ||
            normalizedTime > 1.0f || !std::isfinite(targetWeight) || targetWeight < 0.0f || targetWeight > 1.0f ||
            !IsFiniteNonNegative(blendSeconds)) {
            return false;
        }
        ANIMATION::AnimationSubtreeRootAlignment runtimeAlignment{};
        switch (rootAlignment) {
        case GameProjectAnimationLayerRootAlignment::ParentLocal:
            runtimeAlignment = ANIMATION::AnimationSubtreeRootAlignment::ParentLocal;
            break;
        case GameProjectAnimationLayerRootAlignment::PreserveModelRotation:
            runtimeAlignment = ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation;
            break;
        default: return false;
        }
        GameObject* object = FindObject(static_cast<World*>(world_), objectHandle);
        if (object == nullptr) {
            return false;
        }
        AnimatorComponent* animator = object->GetComponent<AnimatorComponent>();
        if (animator == nullptr) {
            return false;
        }
        const ModelComponent* modelComponent = object->GetComponent<ModelComponent>();
        const ModelAsset* modelAsset = modelComponent != nullptr ? modelComponent->GetModelAsset() : nullptr;
        if (modelAsset != nullptr) {
            return animator->SetMaskedPoseLayerResolved(clipName, maskRootNodeName, normalizedTime, targetWeight,
                                                        blendSeconds, *modelAsset, runtimeAlignment);
        }
        animator->SetMaskedPoseLayer(std::string(clipName), std::string(maskRootNodeName), normalizedTime, targetWeight,
                                     blendSeconds, runtimeAlignment);
        return true;
    } catch (...) {
        return false;
    }
}

bool AnimationView::ClearMaskedPoseLayer(uint64_t objectHandle, float blendOutSeconds) const noexcept {
    if (!IsFiniteNonNegative(blendOutSeconds))
        return false;
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.ClearMaskedPoseLayer(blendOutSeconds);
        return true;
    });
}

bool AnimationView::SetRootMotionEnabled(uint64_t objectHandle, uint8_t enabled) const noexcept {
    return EditAnimator(world_, objectHandle, [&](GameObject&, AnimatorComponent& animator) {
        animator.SetRootMotionEnabled(enabled != 0u);
        return true;
    });
}

bool AnimationView::GetState(uint64_t objectHandle, GameProjectAnimationState& outState) const noexcept {
    outState = {};
    try {
        auto* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        if (object == nullptr) {
            return false;
        }
        const AnimatorComponent* animator = object->GetComponent<AnimatorComponent>();
        if (animator == nullptr) {
            return false;
        }
        const ModelComponent* model = object->GetComponent<ModelComponent>();
        const ModelAsset* modelAsset = model != nullptr ? model->GetModelAsset() : nullptr;
        const ANIMATION::AnimationClipReference& reference = animator->GetClipReference();
        const AnimationClip* clip =
            modelAsset != nullptr ? ANIMATION::ResolveAnimationClip(*modelAsset, reference) : nullptr;
        const float duration = clip != nullptr ? clip->durationSec : 0.0f;
        const float time = animator->GetTime();
        if (!std::isfinite(duration) || !std::isfinite(time) || !std::isfinite(animator->GetSpeed())) {
            return false;
        }

        const ANIMATION::AnimationPoseService* poses = world->Services().Find<ANIMATION::AnimationPoseService>();
        const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> snapshot =
            poses != nullptr ? poses->Find(object->GetRuntimeHandle()) : nullptr;
        const bool snapshotMatchesCurrent = snapshot != nullptr && snapshot->primaryClip == reference;
        float normalizedTime = duration > 0.0f ? std::clamp(time / duration, 0.0f, 1.0f) : 0.0f;
        if (snapshotMatchesCurrent && std::isfinite(snapshot->normalizedTime)) {
            normalizedTime = std::clamp(snapshot->normalizedTime, 0.0f, 1.0f);
        }
        outState.clipId = ResolvePublicClipId(modelAsset, clip, reference);
        outState.poseRevision = snapshotMatchesCurrent ? snapshot->revision : 0u;
        if (snapshotMatchesCurrent && snapshot->primaryClip.clipId.IsValid()) {
            outState.clipId = snapshot->primaryClip.clipId.value;
        }
        outState.timeSeconds = time;
        outState.durationSeconds = duration;
        outState.normalizedTime = normalizedTime;
        outState.speed = animator->GetSpeed();
        outState.transitionWeight = snapshotMatchesCurrent && std::isfinite(snapshot->blendWeight)
                                        ? std::clamp(snapshot->blendWeight, 0.0f, 1.0f)
                                    : std::isfinite(animator->GetTransitionWeight())
                                        ? std::clamp(animator->GetTransitionWeight(), 0.0f, 1.0f)
                                        : 1.0f;
        outState.playing = animator->IsPlaying() ? 1u : 0u;
        outState.finished = animator->IsFinished(duration) ? 1u : 0u;
        outState.transitioning = animator->IsTransitioning() ? 1u : 0u;
        outState.loop = animator->GetLoop() ? 1u : 0u;
        outState.valid = snapshotMatchesCurrent && snapshot->valid ? 1u : 0u;
        return true;
    } catch (...) {
        outState = {};
        return false;
    }
}
} // namespace HIKARI
