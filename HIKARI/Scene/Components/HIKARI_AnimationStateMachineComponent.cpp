#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"

#include <utility>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void AnimationStateMachineComponent::Serialize(
        nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["stateMachineAssetGuid"] = assetGuid_;
        out["playOnStart"] = playOnStart_;
        out["syncCharacterMotion"] = syncCharacterMotion_;
    }

    void AnimationStateMachineComponent::Deserialize(
        const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        assetGuid_ = in.value("stateMachineAssetGuid", assetGuid_);
        playOnStart_ = in.value("playOnStart", playOnStart_);
        syncCharacterMotion_ = in.value(
            "syncCharacterMotion", syncCharacterMotion_);
        lastDiagnostic_.clear();
    }

    void AnimationStateMachineComponent::BuildInspector(
        IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.AssetIdPicker(
            "State Machine Asset",
            AssetType::AnimationStateMachine,
            assetGuid_);
        builder.Bool("Play On Start", playOnStart_);
        builder.Bool("Sync Character Motion", syncCharacterMotion_);

        const InspectorContext& context = builder.GetContext();
        if (context.worldServices != nullptr &&
            context.runtimeObject != nullptr) {
            const auto* service = context.worldServices->Find<
                ANIMATION::AnimationStateMachineRuntimeService>();
            const auto* instance = service != nullptr
                ? service->Find(*context.runtimeObject)
                : nullptr;
            if (instance != nullptr) {
                const auto& snapshot = instance->GetSnapshot();
                builder.Text(snapshot.initialized
                    ? "Runtime State: " + snapshot.currentStateName
                    : "Runtime State: Not initialized");
                builder.Text(snapshot.running
                    ? "Playback: Running"
                    : "Playback: Stopped");
                builder.Text("Motion: " +
                    (snapshot.primaryClipName.empty()
                        ? std::string("<none>")
                        : snapshot.primaryClipName));
                if (snapshot.motionType == ANIMATION::
                        AnimationStateMotionType::BlendTree1D) {
                    builder.Text(
                        "Blend " + snapshot.blendParameterName + ": " +
                        std::to_string(snapshot.blendInputValue));
                }
                if (!snapshot.lastError.empty()) {
                    builder.Text("Runtime: " + snapshot.lastError);
                }
            }
        }
    }

    void AnimationStateMachineComponent::RenderImGui() {
#if defined(HIKARI_WITH_EDITOR)
        const GameObject* owner = GetOwner();
        const World* world = owner != nullptr ? owner->GetWorld() : nullptr;
        const auto* service = world != nullptr
            ? world->Services().Find<
                ANIMATION::AnimationStateMachineRuntimeService>()
            : nullptr;
        const auto* instance = service != nullptr
            ? service->Find(owner->GetRuntimeHandle())
            : nullptr;
        if (instance == nullptr) {
            ImGui::TextDisabled("Runtime state is not active");
            return;
        }
        const auto& snapshot = instance->GetSnapshot();
        ImGui::Text(
            "State: %s",
            snapshot.currentStateName.empty()
                ? "<none>"
                : snapshot.currentStateName.c_str());
        ImGui::Text("Transitions: %llu",
            static_cast<unsigned long long>(snapshot.transitionCount));
        ImGui::Text("Motion: %s",
            snapshot.primaryClipName.empty()
                ? "<none>"
                : snapshot.primaryClipName.c_str());
        if (snapshot.motionType ==
            ANIMATION::AnimationStateMotionType::BlendTree1D) {
            ImGui::Text(
                "%s %.3f -> %.0f%% %s",
                snapshot.blendParameterName.c_str(),
                snapshot.blendInputValue,
                snapshot.motionBlendWeight * 100.0f,
                snapshot.secondaryClipName.c_str());
        }
        ImGui::TextDisabled(
            "%s",
            snapshot.running ? "Running" : "Stopped");
        if (!snapshot.lastError.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "%s",
                snapshot.lastError.c_str());
        }
#endif
    }

    bool AnimationStateMachineComponent::IsEnabled() const noexcept {
        return enabled_;
    }

    void AnimationStateMachineComponent::SetEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

    const std::string&
        AnimationStateMachineComponent::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    void AnimationStateMachineComponent::SetAssetGuid(std::string guid) {
        assetGuid_ = std::move(guid);
    }

    bool AnimationStateMachineComponent::GetPlayOnStart() const noexcept {
        return playOnStart_;
    }

    bool AnimationStateMachineComponent::GetSyncCharacterMotion()
        const noexcept {
        return syncCharacterMotion_;
    }

} // namespace HIKARI
