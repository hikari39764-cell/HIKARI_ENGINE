#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/GameSystem.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

// UI ボタン・入力アクション・画面領域から Scene Flow の切替要求を出す組み込み Feature。
// 公開 SDK だけで書かれており、プロジェクトの組件と同じ実行経路を通る。
namespace HIKARI {
namespace {

constexpr std::string_view kFeatureId = "HIKARI.SceneFlow";
constexpr std::string_view kComponentType = "HIKARI.SceneFlow.Trigger";
constexpr ComponentTypeId kComponentTypeId = MakeComponentTypeId(kComponentType);
constexpr std::string_view kSystemId = "HIKARI.SceneFlow.TriggerSystem";

bool IsCanonicalAssetGuid(std::string_view value) noexcept {
    return value.size() == 32u && std::ranges::all_of(value, [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

class SceneSwitchTriggerComponent final : public IGameComponent {
public:
    void DescribeProperties(PropertyFields& fields) override {
        if (!fields.Section("Scene Switch Trigger", true)) {
            return;
        }
        fields.Bool("enabled", "Enabled", enabled_);
        fields.Asset("targetSceneGuid", "Target Scene", GameProjectAssetKind::Scene, targetSceneGuid_);
        fields.Text("targetSpawnPointId", "Target Spawn Point", targetSpawnPointId_);
        fields.Bool("actionEnabled", "Input Action Trigger", actionEnabled_);
        if (actionEnabled_ || !fields.IsInspecting()) {
            fields.InputAction("actionId", "Input Action", GameProjectInputValueType::Button, actionId_);
        }
        fields.Bool("pointerRegionEnabled", "Pointer Region Trigger", pointerRegionEnabled_);
        if (pointerRegionEnabled_ || !fields.IsInspecting()) {
            fields.Float("pointerMinimumX", "Pointer Region Min X", pointerMinimumX_, 0.0f, 1.0f, 0.01f);
            fields.Float("pointerMinimumY", "Pointer Region Min Y", pointerMinimumY_, 0.0f, 1.0f, 0.01f);
            fields.Float("pointerMaximumX", "Pointer Region Max X", pointerMaximumX_, 0.0f, 1.0f, 0.01f);
            fields.Float("pointerMaximumY", "Pointer Region Max Y", pointerMaximumY_, 0.0f, 1.0f, 0.01f);
            fields.Note("Pointer regions use normalized top-left coordinates and require UI/Free cursor mode. "
                        "The owning UI should call InputView::SetUiInteractionActive(1).");
        }
        fields.Bool("useTransition", "Use Transition", useTransition_);
        if (useTransition_ || !fields.IsInspecting()) {
            fields.Text("transitionProfileId", "Transition Profile", transitionProfileId_);
        }
        fields.Bool("oneShotPerWorld", "One Accepted Request Per World", oneShotPerWorld_);
        fields.Float("cooldownSeconds", "Cooldown Seconds", cooldownSeconds_, 0.0f, 30.0f, 0.05f);
        NormalizePointerRegion();
        if (fields.IsLoading()) {
            pending_ = false;
            consumed_ = false;
            cooldownRemaining_ = 0.0f;
        }
        if (!IsCanonicalAssetGuid(targetSceneGuid_)) {
            fields.Note("Assign a valid Scene asset before Play.");
        }
        if (!actionEnabled_ && !pointerRegionEnabled_) {
            fields.Note("Enable at least one trigger source.");
        }
        fields.Note("One-shot and cooldown begin when the transition request is accepted; they do not imply that "
                    "loading later succeeded.");
    }

    bool IsActionEnabled() const noexcept { return actionEnabled_; }
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

private:
    void NormalizePointerRegion() noexcept {
        if (pointerMinimumX_ > pointerMaximumX_) {
            std::swap(pointerMinimumX_, pointerMaximumX_);
        }
        if (pointerMinimumY_ > pointerMaximumY_) {
            std::swap(pointerMinimumY_, pointerMaximumY_);
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
            kComponentTypeId, [&](ObjectView, SceneSwitchTriggerComponent& trigger) {
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
                if (actionPressed || pointerPressed) {
                    trigger.SetPendingRequest();
                }
                if (!trigger.HasPendingRequest() || sceneFlow.IsTransitioning()) {
                    return;
                }
                if (!IsCanonicalAssetGuid(trigger.GetTargetSceneGuid())) {
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
        return "UI button, input and pointer triggers over the engine Scene Flow request gate.";
    }
    bool Register(RuntimeFeatureContext& context) override {
        GameComponentDescriptor component{};
        component.typeName = kComponentType;
        component.allowMultiple = true;
        component.displayName = "Scene Switch Trigger";
        component.category = "Scene Flow";
        component.description = "Requests a Scene by UI button, action or normalized pointer region.";
        GameSystemDescriptor system{};
        system.systemId = kSystemId;
        system.displayName = "Scene Switch Trigger";
        return context.RegisterComponent<SceneSwitchTriggerComponent>(component) &&
               context.RegisterSystem<SceneSwitchTriggerSystem>(system) &&
               context.BindComponentSystem({kComponentType, kSystemId, 80});
    }
};

} // namespace

std::unique_ptr<IRuntimeFeature> CreateSceneFlowRuntimeFeature() { return std::make_unique<SceneFlowRuntimeFeature>(); }

} // namespace HIKARI
