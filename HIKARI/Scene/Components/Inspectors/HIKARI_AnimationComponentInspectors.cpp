// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <string>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

namespace {
float ClampInspectorTime(float value) { return (std::max)(0.0f, value); }
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

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
