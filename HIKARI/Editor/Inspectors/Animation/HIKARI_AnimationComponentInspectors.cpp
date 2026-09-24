#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <string>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "imgui.h"

namespace HIKARI {

namespace {
float ClampInspectorTime(float value) { return (std::max)(0.0f, value); }

const ModelAsset* FindOwnerModelAsset(const AnimatorComponent& animator) {
    const GameObject* owner = animator.GetOwner();
    if (owner == nullptr) {
        return nullptr;
    }
    const ModelComponent* model = owner->GetComponent<ModelComponent>();
    return model != nullptr ? model->GetModelAsset() : nullptr;
}

const AnimationClip* FindClip(const ModelAsset* asset, const ANIMATION::AnimationClipReference& reference) {
    return asset != nullptr ? ANIMATION::ResolveAnimationClip(*asset, reference) : nullptr;
}
} // namespace

void AnimatorComponent::BuildInspector(IInspectorBuilder& builder) {
    if (builder.String("Clip", clip_.fallbackName)) {
        // A hand-edited name is no longer guaranteed to describe the
        // previously resolved asset/clip pair. The AnimationSystem will
        // resolve and bind the new name on its next update.
        clip_.modelAssetId = {};
        clip_.clipId = {};
        ClearTransition();
    }
    builder.Float("Time Sec", timeSec_);
    builder.Float("Speed", speed_);
    builder.Float("Default Blend Sec", defaultBlendDurationSec_);
    builder.Bool("Loop", loop_);
    builder.Bool("Auto Play", autoPlay_);
    builder.Bool("Playing", playing_);
    builder.Bool("Finished", finished_);
    timeSec_ = ClampInspectorTime(timeSec_);
    defaultBlendDurationSec_ = (std::max)(0.0f, defaultBlendDurationSec_);
}

void AnimatorComponent::RenderImGui() {
    const ModelAsset* modelAsset = FindOwnerModelAsset(*this);
    const AnimationClip* currentClip = FindClip(modelAsset, clip_);
    int currentIndex = -1;
    if (modelAsset != nullptr) {
        for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
            if (modelAsset->GetAnimationClipId(i) == clip_.clipId ||
                modelAsset->animations[i].name == clip_.fallbackName) {
                currentIndex = static_cast<int>(i);
                currentClip = &modelAsset->animations[i];
                break;
            }
        }
    }

    const char* preview = clip_.fallbackName.empty() ? "<none>" : clip_.fallbackName.c_str();
    if (modelAsset != nullptr && !modelAsset->animations.empty()) {
        if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.clip.from.model.3dbc63", "Clip From Model").c_str(),
                              preview)) {
            for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
                const AnimationClip& clip = modelAsset->animations[i];
                const bool selected = static_cast<int>(i) == currentIndex;
                std::string label = clip.name + " (" + std::to_string(clip.durationSec) + "s)";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    CrossFade(ANIMATION::MakeAnimationClipReference(*modelAsset, i), defaultBlendDurationSec_, loop_,
                              true);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.animation.clip.no.clips.on.current.model.e981b1").c_str());
    }

    if (currentClip != nullptr) {
        const float duration = currentClip->durationSec;
        const float normalized = duration > 0.0f ? std::clamp(timeSec_ / duration, 0.0f, 1.0f) : 0.0f;
        ImGui::Text(TEXT::Localize("editor.ui.current.clip.duration.2f.619417").c_str(), duration);
        ImGui::Text(TEXT::Localize("editor.ui.progress.2f.2f.74a7e9").c_str(), timeSec_, duration);
        ImGui::Text(TEXT::Localize("editor.ui.normalized.time.3f.0823a1").c_str(), normalized);
        ImGui::ProgressBar(normalized);
    } else if (!clip_.fallbackName.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                           TEXT::Localize("editor.ui.clip.not.found.in.current.model.7abe27").c_str());
    }

    if (IsTransitioning()) {
        ImGui::Text(TEXT::Localize("editor.ui.blend.0f.67edcb").c_str(), GetTransitionWeight() * 100.0f);
        ImGui::ProgressBar(GetTransitionWeight());
    }

    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Play", "Play").c_str())) {
        PlayCurrent(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Pause", "Pause").c_str())) {
        Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.vfx.text.Resume", "Resume").c_str())) {
        Resume();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ai.task.stop", "Stop").c_str())) {
        Stop();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.reset.time.1bbc50", "Reset Time").c_str())) {
        ResetTime();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.play.once.fcbde2", "Play Once").c_str())) {
        PlayOnce(clip_.fallbackName, true);
    }
}

void AnimationStateMachineComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool("Enabled", enabled_);
    builder.AssetIdPicker("State Machine Asset", AssetType::AnimationStateMachine, assetGuid_);
    builder.Bool("Play On Start", playOnStart_);
    builder.Bool("Sync Character Motion", syncCharacterMotion_);

    const InspectorContext& context = builder.GetContext();
    if (context.worldServices != nullptr && context.runtimeObject != nullptr) {
        const auto* service = context.worldServices->Find<ANIMATION::AnimationStateMachineRuntimeService>();
        const auto* instance = service != nullptr ? service->Find(*context.runtimeObject) : nullptr;
        if (instance != nullptr) {
            const auto& snapshot = instance->GetSnapshot();
            builder.Text(snapshot.initialized ? "Runtime State: " + snapshot.currentStateName
                                              : "Runtime State: Not initialized");
            builder.Text(snapshot.running ? "Playback: Running" : "Playback: Stopped");
            builder.Text("Motion: " +
                         (snapshot.primaryClipName.empty() ? std::string("<none>") : snapshot.primaryClipName));
            if (snapshot.motionType == ANIMATION::AnimationStateMotionType::BlendTree1D) {
                builder.Text("Blend " + snapshot.blendParameterName + ": " + std::to_string(snapshot.blendInputValue));
            }
            if (!snapshot.lastError.empty()) {
                builder.Text("Runtime: " + snapshot.lastError);
            }
        }
    }
}

void AnimationStateMachineComponent::RenderImGui() {
    const GameObject* owner = GetOwner();
    const World* world = owner != nullptr ? owner->GetWorld() : nullptr;
    const auto* service =
        world != nullptr ? world->Services().Find<ANIMATION::AnimationStateMachineRuntimeService>() : nullptr;
    const auto* instance = service != nullptr ? service->Find(owner->GetRuntimeHandle()) : nullptr;
    if (instance == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.runtime.state.is.not.active.f94347").c_str());
        return;
    }
    const auto& snapshot = instance->GetSnapshot();
    ImGui::Text(TEXT::Localize("ui.quality.stateFormat").c_str(),
                snapshot.currentStateName.empty() ? "<none>" : snapshot.currentStateName.c_str());
    ImGui::Text(TEXT::Localize("editor.ui.transitions.llu.759fe8").c_str(),
                static_cast<unsigned long long>(snapshot.transitionCount));
    ImGui::Text(TEXT::Localize("editor.ui.motion.s.d81c1c").c_str(),
                snapshot.primaryClipName.empty() ? "<none>" : snapshot.primaryClipName.c_str());
    if (snapshot.motionType == ANIMATION::AnimationStateMotionType::BlendTree1D) {
        ImGui::Text("%s %.3f -> %.0f%% %s", snapshot.blendParameterName.c_str(), snapshot.blendInputValue,
                    snapshot.motionBlendWeight * 100.0f, snapshot.secondaryClipName.c_str());
    }
    ImGui::TextDisabled("%s", snapshot.running ? "Running" : "Stopped");
    if (!snapshot.lastError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", snapshot.lastError.c_str());
    }
}

} // namespace HIKARI
