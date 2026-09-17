#include "Scene/Ui/HIKARI_UiSceneSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/UiTypes.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"

namespace HIKARI {
    namespace {

        bool ContainsPoint(
            const UiSceneSystem::ResolvedLayout& layout,
            float canvasX,
            float canvasY) noexcept {
            if (!layout.valid || layout.width <= 0.0f ||
                layout.height <= 0.0f || !std::isfinite(canvasX) ||
                !std::isfinite(canvasY)) {
                return false;
            }
            const float pivotX = layout.x + layout.width * layout.pivotX;
            const float pivotY = layout.y + layout.height * layout.pivotY;
            const float dx = canvasX - pivotX;
            const float dy = canvasY - pivotY;
            const float cosine = std::cos(layout.rotationRadians);
            const float sine = std::sin(layout.rotationRadians);
            const float localX = pivotX + dx * cosine + dy * sine;
            const float localY = pivotY - dx * sine + dy * cosine;
            return localX >= layout.x &&
                localX <= layout.x + layout.width &&
                localY >= layout.y &&
                localY <= layout.y + layout.height;
        }

    } // namespace

    void UiSceneSystem::OnWorldAttached(World& world) {
        input_ = world.Services().Find<INPUT::InputService>();
        presentation_ = world.Services().Find<
            GAME_UI::GameUiPresentationService>();
        bindings_ = world.Services().Find<BINDING::RuntimeBindingStore>();
        playState_ = world.Services().Find<RuntimePlayStateService>();
        elapsedSeconds_ = 0.0f;
        ResetInteraction();
    }

    void UiSceneSystem::OnWorldDetached(World&) {
        ReleaseMouseCapture();
        input_ = nullptr;
        presentation_ = nullptr;
        bindings_ = nullptr;
        playState_ = nullptr;
        elapsedSeconds_ = 0.0f;
        layouts_.clear();
        buttons_.clear();
        ResetInteraction();
    }

    void UiSceneSystem::PreUpdate(
        World& world,
        const FrameContext& frame) {
        if (presentation_ == nullptr || input_ == nullptr ||
            playState_ == nullptr || !playState_->IsActive() || input_->IsDeveloperInputCaptured()) {
            if (presentation_) presentation_->BeginInteractionFrame(frame.frameIndex);
            ReleaseMouseCapture();
            ResetButtonStates(world);
            ResetInteraction();
            return;
        }
        presentation_->BeginInteractionFrame(frame.frameIndex);
        if (std::isfinite(frame.unscaledDt) && frame.unscaledDt > 0.0f) {
            elapsedSeconds_ += std::min(frame.unscaledDt, 1.0f);
        }
        BuildButtons(world);
        if (buttons_.empty()) {
            ReleaseMouseCapture();
            ResetInteraction();
            return;
        }
        const bool hasInteractableButton = std::any_of(
            buttons_.begin(),
            buttons_.end(),
            [](const ButtonEntry& entry) {
                return entry.button->IsEnabled() &&
                    entry.button->IsInteractable() &&
                    entry.layout.interactable;
            });
        if (!hasInteractableButton) {
            ReleaseMouseCapture();
            ResetInteraction();
            ProcessButtons(world, frame);
            return;
        }
        AcquireMouseCapture();
        ProcessButtons(world, frame);
    }

    void UiSceneSystem::BuildButtons(World& world) {
        layouts_.clear();
        buttons_.clear();
        (void)world.ForEachObjectWith<UiRectComponent, UiButtonComponent>(
            WorldComponentQueryAnchor::SmallestUniqueBucket,
            [&](GameObject& object, UiRectComponent&, UiButtonComponent& button) {
                ResolvedLayout layout = ResolveLayout(world, object);
                if (layout.valid) {
                    buttons_.push_back({ &object, &button, layout });
                }
            });
        std::stable_sort(
            buttons_.begin(),
            buttons_.end(),
            [](const ButtonEntry& lhs, const ButtonEntry& rhs) {
                if (lhs.button->GetNavigationOrder() !=
                        rhs.button->GetNavigationOrder()) {
                    return lhs.button->GetNavigationOrder() <
                        rhs.button->GetNavigationOrder();
                }
                return lhs.layout.order < rhs.layout.order;
            });
        if (focusedButton_.IsValid() &&
            std::any_of(buttons_.begin(), buttons_.end(),
                [this](const ButtonEntry& entry) {
                    return entry.object->GetRuntimeHandle() == focusedButton_ && entry.button->IsEnabled() &&
                        entry.button->IsInteractable() && entry.layout.interactable;
                })) {
            return;
        }
        const auto preferred = std::find_if(
            buttons_.begin(), buttons_.end(),
            [](const ButtonEntry& entry) {
                return entry.button->IsEnabled() &&
                    entry.button->IsInteractable() &&
                    entry.layout.interactable &&
                    entry.button->IsDefaultFocus();
            });
        const auto first = preferred != buttons_.end()
            ? preferred
            : std::find_if(buttons_.begin(), buttons_.end(),
                [](const ButtonEntry& entry) {
                    return entry.button->IsEnabled() &&
                        entry.button->IsInteractable() &&
                        entry.layout.interactable;
                });
        focusedButton_ = first != buttons_.end()
            ? first->object->GetRuntimeHandle()
            : RuntimeObjectHandle{};
    }

    UiSceneSystem::ButtonEntry* UiSceneSystem::FindButton(
        RuntimeObjectHandle handle) noexcept {
        const auto found = std::find_if(
            buttons_.begin(), buttons_.end(),
            [handle](const ButtonEntry& entry) {
                return entry.object->GetRuntimeHandle() == handle;
            });
        return found != buttons_.end() ? &*found : nullptr;
    }

    UiSceneSystem::ButtonEntry* UiSceneSystem::HitTest(
        float screenX,
        float screenY,
        uint32_t logicalWidth,
        uint32_t logicalHeight) noexcept {
        ButtonEntry* best = nullptr;
        for (ButtonEntry& entry : buttons_) {
            if (!entry.button->IsEnabled() ||
                !entry.button->IsInteractable() ||
                !entry.layout.interactable) {
                continue;
            }
            GameProjectUiCanvasFit fit{};
            float canvasX = 0.0f;
            float canvasY = 0.0f;
            if (!ResolveGameProjectUiCanvasFit(
                    logicalWidth,
                    logicalHeight,
                    entry.layout.referenceWidth,
                    entry.layout.referenceHeight,
                    fit) ||
                !ProjectGameUiScreenPointToCanvas(
                    fit, screenX, screenY, canvasX, canvasY) ||
                !ContainsPoint(entry.layout, canvasX, canvasY)) {
                continue;
            }
            if (best == nullptr || entry.layout.order >= best->layout.order) {
                best = &entry;
            }
        }
        return best;
    }

    void UiSceneSystem::ProcessButtons(
        World&,
        const FrameContext& frame) {
        const int width = SERVICES::GetLogicalScreenWidth();
        const int height = SERVICES::GetLogicalScreenHeight();
        INPUT::InputPointerState pointer{};
        const bool pointerValid = width > 0 && height > 0 &&
            input_->GetSnapshot().TryGetPointerState(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height),
                pointer);
        ButtonEntry* hovered = pointerValid
            ? HitTest(
                pointer.x,
                pointer.y,
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height))
            : nullptr;
        const bool pointerMoved = pointerValid && (!pointerInitialized_ ||
            std::abs(pointer.x - previousPointerX_) > 0.5f || std::abs(pointer.y - previousPointerY_) > 0.5f);
        if (hovered != nullptr && (pointerMoved || (pointer.primaryDown && !previousPrimaryDown_))) {
            focusedButton_ = hovered->object->GetRuntimeHandle();
        }
        if (pointerValid) { previousPointerX_ = pointer.x; previousPointerY_ = pointer.y; }

        const std::array<float, 2u> navigation =
            input_->GetSnapshot().GetAxis2D(input_->GetActionMap().FindAction("UI.Navigate") ? "UI.Navigate" : "Gameplay.Move");
        if (std::abs(navigation[1]) >= 0.55f &&
            std::abs(previousNavigationY_) < 0.55f) {
            MoveFocus(navigation[1] > 0.0f ? -1 : 1);
        }
        previousNavigationY_ = navigation[1];

        const bool primaryDown = pointerValid && pointer.primaryDown;
        const bool primaryPressed = pointerValid && pointerInitialized_ &&
            primaryDown && !previousPrimaryDown_;
        const bool primaryReleased = pointerValid && pointerInitialized_ &&
            !primaryDown && previousPrimaryDown_;
        if (pointerValid) {
            previousPrimaryDown_ = primaryDown;
            pointerInitialized_ = true;
        } else {
            previousPrimaryDown_ = false;
            pointerInitialized_ = false;
            pressedButton_ = {};
        }
        if (primaryPressed) {
            pressedButton_ = hovered != nullptr
                ? hovered->object->GetRuntimeHandle()
                : RuntimeObjectHandle{};
        }
        RuntimeObjectHandle activation{};
        if (primaryReleased &&
            pressedButton_.IsValid() && hovered != nullptr &&
            hovered->object->GetRuntimeHandle() == pressedButton_) {
            activation = pressedButton_;
        }
        if (!primaryDown) pressedButton_ = {};

        ButtonEntry* focused = FindButton(focusedButton_);
        if (!activation.IsValid() && focused != nullptr &&
            focused->button->IsEnabled() &&
            focused->button->IsInteractable() &&
            focused->layout.interactable &&
            !focused->button->GetSubmitActionId().empty() &&
            input_->GetSnapshot().IsPressed(
                focused->button->GetSubmitActionId() == "Gameplay.Interact" &&
                    input_->GetActionMap().FindAction("UI.Submit") ? "UI.Submit" : focused->button->GetSubmitActionId())) {
            activation = focusedButton_;
        }

        for (ButtonEntry& entry : buttons_) {
            const RuntimeObjectHandle handle =
                entry.object->GetRuntimeHandle();
            const bool isHovered = hovered != nullptr &&
                handle == hovered->object->GetRuntimeHandle();
            const UiButtonVisualState state =
                !entry.button->IsEnabled() ||
                !entry.button->IsInteractable() ||
                !entry.layout.interactable
                    ? UiButtonVisualState::Disabled
                    : handle == pressedButton_ && primaryDown
                        ? UiButtonVisualState::Pressed
                        : isHovered || handle == focusedButton_
                            ? UiButtonVisualState::Hovered
                            : UiButtonVisualState::Normal;
            entry.button->SetVisualState(state);
            entry.button->AdvanceVisual(frame.unscaledDt);
        }
        if (activation.IsValid()) {
            ActivateButton(activation, frame.frameIndex);
        }
    }

    void UiSceneSystem::MoveFocus(int direction) noexcept {
        if (buttons_.empty() || direction == 0) return;
        size_t current = 0u;
        for (size_t index = 0u; index < buttons_.size(); ++index) {
            if (buttons_[index].object->GetRuntimeHandle() == focusedButton_) {
                current = index;
                break;
            }
        }
        for (size_t step = 1u; step <= buttons_.size(); ++step) {
            const size_t index = direction > 0
                ? (current + step) % buttons_.size()
                : (current + buttons_.size() -
                    (step % buttons_.size())) % buttons_.size();
            const ButtonEntry& candidate = buttons_[index];
            if (candidate.button->IsEnabled() &&
                candidate.button->IsInteractable() &&
                candidate.layout.interactable) {
                focusedButton_ = candidate.object->GetRuntimeHandle();
                return;
            }
        }
    }

    void UiSceneSystem::ActivateButton(
        RuntimeObjectHandle handle,
        uint64_t frameIndex) {
        ButtonEntry* entry = FindButton(handle);
        if (entry == nullptr || presentation_ == nullptr) return;
        (void)presentation_->PublishButtonActivation(handle, frameIndex);
        if (bindings_ != nullptr &&
            !entry->button->GetOnClickEventId().empty()) {
            (void)bindings_->Emit(entry->button->GetOnClickEventId(), handle);
        }
    }

    void UiSceneSystem::ResetButtonStates(World& world) {
        (void)world.ForEachObjectWith<UiButtonComponent>(
            [](GameObject&, UiButtonComponent& button) {
                button.SetVisualState(UiButtonVisualState::Normal);
                button.AdvanceVisual(2.0f);
            });
    }

    void UiSceneSystem::AcquireMouseCapture() {
        if (input_ == nullptr) return;
        if (!ownsMouseCapture_) {
            previousMouseCaptureMode_ = input_->GetMouseCaptureMode();
            previousUiInteraction_ = input_->IsUiInteractionActive();
            previousUiContext_ = input_->Contexts().IsActive("UI");
        }
        input_->SetUiInteractionActive(true);
        if (input_->GetActionMap().FindContext("UI")) input_->Contexts().SetActive("UI", true);
        input_->SetMouseCaptureMode(INPUT::MouseCaptureMode::Free);
        input_->ClearMouseCaptureRegion();
        ownsMouseCapture_ = true;
    }

    void UiSceneSystem::ReleaseMouseCapture() {
        if (!ownsMouseCapture_ || input_ == nullptr) return;
        input_->SetMouseCaptureMode(previousMouseCaptureMode_);
        input_->SetUiInteractionActive(previousUiInteraction_);
        input_->Contexts().SetActive("UI", previousUiContext_);
        ownsMouseCapture_ = false;
        previousMouseCaptureMode_ = INPUT::MouseCaptureMode::Free;
    }

    void UiSceneSystem::ResetInteraction() noexcept {
        focusedButton_ = {};
        pressedButton_ = {};
        pointerInitialized_ = false;
        previousPrimaryDown_ = false;
        previousNavigationY_ = 0.0f;
    }

    std::unique_ptr<ISystem> CreateUiSceneSystem() {
        return std::make_unique<UiSceneSystem>();
    }

} // namespace HIKARI
