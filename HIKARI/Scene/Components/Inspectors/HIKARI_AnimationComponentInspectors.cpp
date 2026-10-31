// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"

#include <string>
#include <utility>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Authoring/HIKARI_FieldInspector.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {

void AnimatorComponent::BuildInspector(IInspectorBuilder& builder) {
    AnimatorSettings next = settings_;
    if (!InspectFields(builder, next, kAnimatorFields)) {
        return;
    }
    if (next.fallbackName != settings_.fallbackName) {
        // 手で替えた名前は、解決済みの資産とクリップの組を指している保証が無い。AnimationSystem が次の更新で名前を解決して
        // 結び付け直す。
        next.modelAssetId = {};
        next.clipId = {};
        ClearTransition();
    }
    settings_ = NormalizeAnimatorSettings(std::move(next));
}

void AnimationStateMachineComponent::BuildInspector(IInspectorBuilder& builder) {
    AnimationStateMachineSettings next = settings_;
    if (InspectFields(builder, next, kAnimationStateMachineFields)) {
        settings_ = std::move(next);
    }

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
