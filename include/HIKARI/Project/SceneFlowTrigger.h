#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/RuntimeExtension.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "HIKARI/Project/RuntimeFeatureRegistration.h"
#include "HIKARI/Project/UiView.h"

namespace HIKARI::SCENE_FLOW {

inline constexpr std::string_view kFeatureId = "HIKARI.SceneFlow";
inline constexpr std::string_view kComponentType = "HIKARI.SceneFlow.Trigger";
inline constexpr ComponentTypeId kComponentTypeId = MakeComponentTypeId(kComponentType);
inline constexpr std::string_view kSystemId = "HIKARI.SceneFlow.TriggerSystem";

class SceneSwitchTriggerComponent final : public IGameComponent {
public:
    void LoadProperties(const PropertyReader& properties) override {
        enabled_ = properties.ReadBool("enabled", enabled_);
        targetSceneGuid_ = properties.ReadString("targetSceneGuid", targetSceneGuid_);
        targetSpawnPointId_ = properties.ReadString("targetSpawnPointId", targetSpawnPointId_);
        actionEnabled_ = properties.ReadBool("actionEnabled", actionEnabled_);
        uiButtonEnabled_ = properties.ReadBool("uiButtonEnabled", uiButtonEnabled_);
        actionId_ = properties.ReadString("actionId", actionId_);
        pointerRegionEnabled_ = properties.ReadBool("pointerRegionEnabled", pointerRegionEnabled_);
        pointerMinimumX_ = UnitValue(properties.ReadFloat("pointerMinimumX", pointerMinimumX_), 0.0f);
        pointerMinimumY_ = UnitValue(properties.ReadFloat("pointerMinimumY", pointerMinimumY_), 0.0f);
        pointerMaximumX_ = UnitValue(properties.ReadFloat("pointerMaximumX", pointerMaximumX_), 1.0f);
        pointerMaximumY_ = UnitValue(properties.ReadFloat("pointerMaximumY", pointerMaximumY_), 1.0f);
        NormalizePointerRegion();
        useTransition_ = properties.ReadBool("useTransition", useTransition_);
        transitionProfileId_ = properties.ReadString("transitionProfileId", transitionProfileId_);
        oneShotPerWorld_ = properties.ReadBool("oneShotPerWorld", oneShotPerWorld_);
        const float cooldown = properties.ReadFloat("cooldownSeconds", cooldownSeconds_);
        cooldownSeconds_ = std::isfinite(cooldown) ? std::max(0.0f, cooldown) : 0.5f;
        pending_ = false;
        consumed_ = false;
        cooldownRemaining_ = 0.0f;
    }

    void SaveProperties(PropertyWriter& properties) const override {
        properties.WriteInt("version", 1);
        properties.WriteBool("enabled", enabled_);
        properties.WriteString("targetSceneGuid", targetSceneGuid_);
        properties.WriteString("targetSpawnPointId", targetSpawnPointId_);
        properties.WriteBool("actionEnabled", actionEnabled_);
        properties.WriteBool("uiButtonEnabled", uiButtonEnabled_);
        properties.WriteString("actionId", actionId_);
        properties.WriteBool("pointerRegionEnabled", pointerRegionEnabled_);
        properties.WriteFloat("pointerMinimumX", pointerMinimumX_);
        properties.WriteFloat("pointerMinimumY", pointerMinimumY_);
        properties.WriteFloat("pointerMaximumX", pointerMaximumX_);
        properties.WriteFloat("pointerMaximumY", pointerMaximumY_);
        properties.WriteBool("useTransition", useTransition_);
        properties.WriteString("transitionProfileId", transitionProfileId_);
        properties.WriteBool("oneShotPerWorld", oneShotPerWorld_);
        properties.WriteFloat("cooldownSeconds", cooldownSeconds_);
    }

    void BuildInspector(GameProjectInspector& inspector) override {
        if (!inspector.Section("Scene Switch Trigger", true)) {
            return;
        }
        inspector.Bool("Enabled", enabled_);
        EditSceneGuid(inspector);
        EditString<kGameProjectSpawnPointIdCapacity>(inspector, "Target Spawn Point", targetSpawnPointId_);
        inspector.Bool("Input Action Trigger", actionEnabled_);
        if (actionEnabled_) {
            EditActionId(inspector);
        }
        inspector.Bool("UI Button Trigger", uiButtonEnabled_);
        inspector.Bool("Pointer Region Trigger", pointerRegionEnabled_);
        if (pointerRegionEnabled_) {
            inspector.Vec2("Pointer Region Minimum", pointerMinimumX_, pointerMinimumY_);
            inspector.Vec2("Pointer Region Maximum", pointerMaximumX_, pointerMaximumY_);
            NormalizePointerRegion();
            inspector.Text("Pointer regions use normalized top-left coordinates "
                           "and require UI/Free cursor mode. The owning UI should "
                           "call InputView::SetUiInteractionActive(1).");
        }
        inspector.Bool("Use Transition", useTransition_);
        if (useTransition_) {
            EditString<129u>(inspector, "Transition Profile", transitionProfileId_);
        }
        inspector.Bool("One Accepted Request Per World", oneShotPerWorld_);
        inspector.FloatRange("Cooldown Seconds", cooldownSeconds_, 0.0f, 30.0f, 0.05f);
        cooldownSeconds_ = std::isfinite(cooldownSeconds_) ? std::clamp(cooldownSeconds_, 0.0f, 30.0f) : 0.5f;
        if (!IsCanonicalAssetGuid(targetSceneGuid_)) {
            inspector.Text("Assign a valid Scene asset before Play.");
        }
        if (!actionEnabled_ && !pointerRegionEnabled_ && !uiButtonEnabled_) {
            inspector.Text("Enable at least one trigger source.");
        }
        inspector.Text("One-shot and cooldown begin when the transition request is "
                       "accepted; they do not imply that loading later succeeded.");
    }

    bool IsEnabled() const noexcept { return enabled_; }
    bool IsActionEnabled() const noexcept { return actionEnabled_; }
    bool IsUiButtonEnabled() const noexcept { return uiButtonEnabled_; }
    bool IsPointerRegionEnabled() const noexcept { return pointerRegionEnabled_; }
    const std::string& GetActionId() const noexcept { return actionId_; }
    const std::string& GetTargetSceneGuid() const noexcept { return targetSceneGuid_; }
    const std::string& GetTargetSpawnPointId() const noexcept { return targetSpawnPointId_; }
    const std::string& GetTransitionProfileId() const noexcept { return transitionProfileId_; }
    bool ShouldUseTransition() const noexcept { return useTransition_; }
    bool CanRequest() const noexcept { return enabled_ && !consumed_ && cooldownRemaining_ <= 0.0f; }
    bool HasPendingRequest() const noexcept { return pending_; }
    void SetPendingRequest() noexcept { pending_ = true; }
    void ClearPendingRequest() noexcept { pending_ = false; }
    void MarkRequestAccepted() noexcept {
        pending_ = false;
        cooldownRemaining_ = cooldownSeconds_;
        consumed_ = oneShotPerWorld_;
    }
    void AdvanceCooldown(float unscaledDeltaSeconds) noexcept {
        if (std::isfinite(unscaledDeltaSeconds) && unscaledDeltaSeconds > 0.0f && cooldownRemaining_ > 0.0f) {
            cooldownRemaining_ = std::max(0.0f, cooldownRemaining_ - unscaledDeltaSeconds);
        }
    }
    bool ContainsPointer(float x, float y) const noexcept {
        return std::isfinite(x) && std::isfinite(y) && x >= pointerMinimumX_ && x <= pointerMaximumX_ &&
               y >= pointerMinimumY_ && y <= pointerMaximumY_;
    }

    static bool IsCanonicalAssetGuid(std::string_view value) noexcept {
        if (value.size() != 32u) {
            return false;
        }
        return std::ranges::all_of(value, [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                   (character >= 'A' && character <= 'F');
        });
    }

private:
    static float UnitValue(float value, float fallback) noexcept {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
    }

    void NormalizePointerRegion() noexcept {
        pointerMinimumX_ = UnitValue(pointerMinimumX_, 0.0f);
        pointerMinimumY_ = UnitValue(pointerMinimumY_, 0.0f);
        pointerMaximumX_ = UnitValue(pointerMaximumX_, 1.0f);
        pointerMaximumY_ = UnitValue(pointerMaximumY_, 1.0f);
        if (pointerMinimumX_ > pointerMaximumX_) {
            std::swap(pointerMinimumX_, pointerMaximumX_);
        }
        if (pointerMinimumY_ > pointerMaximumY_) {
            std::swap(pointerMinimumY_, pointerMaximumY_);
        }
        cooldownSeconds_ = std::max(0.0f, cooldownSeconds_);
    }

    template <size_t Capacity>
    static void CopyToBuffer(const std::string& source, std::array<char, Capacity>& destination) noexcept {
        std::snprintf(destination.data(), destination.size(), "%s", source.c_str());
    }

    template <size_t Capacity>
    static void EditString(GameProjectInspector& inspector, std::string_view label, std::string& value) {
        std::array<char, Capacity> buffer{};
        CopyToBuffer(value, buffer);
        if (inspector.StringBuffer(label, buffer.data(), buffer.size())) {
            value = buffer.data();
        }
    }

    void EditSceneGuid(GameProjectInspector& inspector) {
        std::array<char, kGameProjectSceneGuidCapacity> buffer{};
        CopyToBuffer(targetSceneGuid_, buffer);
        if (inspector.SceneAssetGuid("targetSceneGuid", "Target Scene", buffer.data(), buffer.size())) {
            targetSceneGuid_ = buffer.data();
        }
    }

    void EditActionId(GameProjectInspector& inspector) {
        std::array<char, 96u> buffer{};
        CopyToBuffer(actionId_, buffer);
        if (inspector.InputActionId("actionId", "Input Action", GameProjectInputValueType::Button, buffer.data(),
                                    buffer.size())) {
            actionId_ = buffer.data();
        }
    }

    std::string targetSceneGuid_{};
    std::string targetSpawnPointId_{};
    std::string actionId_{"Gameplay.Interact"};
    std::string transitionProfileId_{"noise_wipe"};
    float pointerMinimumX_ = 0.0f;
    float pointerMinimumY_ = 0.0f;
    float pointerMaximumX_ = 1.0f;
    float pointerMaximumY_ = 1.0f;
    float cooldownSeconds_ = 0.5f;
    float cooldownRemaining_ = 0.0f;
    bool enabled_ = true;
    bool actionEnabled_ = true;
    bool uiButtonEnabled_ = true;
    bool pointerRegionEnabled_ = false;
    bool useTransition_ = true;
    bool oneShotPerWorld_ = true;
    bool pending_ = false;
    bool consumed_ = false;
};

class SceneSwitchTriggerSystem final : public IGameSystem {
public:
    void OnWorldDetached(WorldView&) override {
        pointerInitialized_ = false;
        previousPrimaryDown_ = false;
    }

    void Update(WorldView& world, const FrameContext& frame) override {
        if (!world.GetRuntimeSession().IsPlayActive()) {
            pointerInitialized_ = false;
            previousPrimaryDown_ = false;
            return;
        }
        const InputView input = world.GetInput();
        const UiView ui = world.GetUi();
        const SceneFlowView sceneFlow = world.GetSceneFlow();
        if (!sceneFlow.IsValid()) {
            return;
        }

        GameProjectPointerState pointer{};
        const bool pointerValid = input.IsValid() && input.GetPointerState(pointer) && pointer.valid != 0u;
        bool primaryPressed = false;
        if (pointerValid) {
            primaryPressed = pointerInitialized_ && pointer.primaryDown != 0u && !previousPrimaryDown_;
            previousPrimaryDown_ = pointer.primaryDown != 0u;
            pointerInitialized_ = true;
        } else {
            pointerInitialized_ = false;
            previousPrimaryDown_ = false;
        }

        const float unscaledDeltaSeconds =
            std::isfinite(frame.unscaledDt) ? std::clamp(frame.unscaledDt, 0.0f, 1.0f) : 0.0f;
        (void)world.ForEachComponent<SceneSwitchTriggerComponent>(
            kComponentTypeId, [&](ObjectView object, SceneSwitchTriggerComponent& trigger) {
                trigger.AdvanceCooldown(unscaledDeltaSeconds);
                if (!trigger.CanRequest()) {
                    return;
                }
                GameProjectInputActionState action{};
                const bool actionPressed =
                    trigger.IsActionEnabled() && input.IsValid() && !trigger.GetActionId().empty() &&
                    input.GetAction(trigger.GetActionId(), action) &&
                    action.valueType == GameProjectInputValueType::Button && action.pressed != 0u;
                const bool pointerPressed = trigger.IsPointerRegionEnabled() && pointerValid && primaryPressed &&
                                            trigger.ContainsPointer(pointer.normalizedX, pointer.normalizedY);
                const bool uiActivated =
                    trigger.IsUiButtonEnabled() && ui.IsValid() && ui.WasActivated(object.GetHandle());
                if (actionPressed || pointerPressed || uiActivated) {
                    trigger.SetPendingRequest();
                }
                if (!trigger.HasPendingRequest() || sceneFlow.IsTransitioning()) {
                    return;
                }
                if (!SceneSwitchTriggerComponent::IsCanonicalAssetGuid(trigger.GetTargetSceneGuid())) {
                    trigger.ClearPendingRequest();
                    return;
                }
                if (sceneFlow.RequestScene(trigger.GetTargetSceneGuid(), trigger.GetTargetSpawnPointId(),
                                           trigger.GetTransitionProfileId(), 0u,
                                           trigger.ShouldUseTransition() ? 1u : 0u)) {
                    trigger.MarkRequestAccepted();
                } else {
                    // Busy was checked above. A remaining rejection is a
                    // permanent request/configuration error for this edge.
                    trigger.ClearPendingRequest();
                }
            });
    }

private:
    bool pointerInitialized_ = false;
    bool previousPrimaryDown_ = false;
};

class SceneFlowRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return kFeatureId; }
    std::string_view GetDisplayName() const noexcept override { return "Scene Flow"; }
    std::string_view GetDescription() const noexcept override {
        return "Project-owned UI button, input and pointer triggers "
               "over the engine Scene Flow request gate.";
    }
    std::span<const std::string_view> GetComponentTypeNames() const noexcept override {
        static constexpr std::array<std::string_view, 1u> names{kComponentType};
        return names;
    }
    std::span<const std::string_view> GetSystemIds() const noexcept override {
        static constexpr std::array<std::string_view, 1u> ids{kSystemId};
        return ids;
    }
    bool Register(RuntimeFeatureContext& context) override {
        GameComponentDescriptor component{};
        component.typeName = kComponentType;
        component.typeId = kComponentTypeId;
        component.allowMultiple = true;
        component.displayName = "Scene Switch Trigger";
        component.category = "Scene Flow";
        component.description = "Requests a Scene by UI button, action or normalized "
                                "pointer region.";
        component.featureId = kFeatureId;

        GameSystemDescriptor system{};
        system.systemId = kSystemId;
        system.displayName = "Scene Switch Trigger";
        system.featureId = kFeatureId;

        bool success = context.RegisterComponent<SceneSwitchTriggerComponent>(component);
        success = context.RegisterSystem<SceneSwitchTriggerSystem>(system) && success;
        success = context.BindComponentSystem({kComponentType, kSystemId, 80}) && success;
        return success;
    }
};

class SceneFlowRuntimeExtension final : public IRuntimeExtension {
public:
    std::string_view GetExtensionId() const noexcept override { return "HIKARI.SceneFlow.Extension"; }
    bool RegisterRuntimeFeatures(RuntimeFeatureCatalog& catalog) override {
        return RegisterRuntimeFeature<SceneFlowRuntimeFeature>(catalog);
    }
};

inline std::unique_ptr<IRuntimeExtension> CreateSceneFlowRuntimeExtension() {
    return std::make_unique<SceneFlowRuntimeExtension>();
}

} // namespace HIKARI::SCENE_FLOW
