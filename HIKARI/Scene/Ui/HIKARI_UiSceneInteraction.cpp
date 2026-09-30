#include "Scene/Ui/HIKARI_UiSceneSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <limits>

#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/UiTypes.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"
#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"

namespace HIKARI {
namespace {

bool IsVisibleWithinClip(const UiResolvedLayout& layout) noexcept;

bool IsFocusable(const UiSceneSystem::ControlEntry& entry) noexcept {
    if (!entry.object || !entry.layout.valid || !entry.layout.clipSupported || !entry.layout.inputEnabled ||
        !entry.layout.interactable || !IsVisibleWithinClip(entry.layout))
        return false;
    if (entry.textField)
        return entry.textField->IsEnabled() && entry.object->GetComponent<UiTextComponent>() != nullptr;
    return entry.button && entry.button->IsEnabled() && entry.button->IsInteractable();
}

bool IsVisibleWithinClip(const UiResolvedLayout& layout) noexcept {
    if (!layout.clipEnabled)
        return true;
    if (!layout.clipSupported || !std::isfinite(layout.clipLeft) || !std::isfinite(layout.clipTop) ||
        !std::isfinite(layout.clipRight) || !std::isfinite(layout.clipBottom) || layout.clipRight <= layout.clipLeft ||
        layout.clipBottom <= layout.clipTop)
        return false;

    struct Point {
        float x;
        float y;
    };
    std::array<Point, 4> control{{{layout.x, layout.y},
                                  {layout.x + layout.width, layout.y},
                                  {layout.x + layout.width, layout.y + layout.height},
                                  {layout.x, layout.y + layout.height}}};
    for (auto& point : control) {
        layout.transform.Transform(point.x, point.y);
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return false;
    }
    const std::array<Point, 4> clip{{{layout.clipLeft, layout.clipTop},
                                     {layout.clipRight, layout.clipTop},
                                     {layout.clipRight, layout.clipBottom},
                                     {layout.clipLeft, layout.clipBottom}}};
    const auto overlaps = [&](float axisX, float axisY) {
        float controlMinimum = (std::numeric_limits<float>::max)();
        float controlMaximum = (std::numeric_limits<float>::lowest)();
        float clipMinimum = (std::numeric_limits<float>::max)();
        float clipMaximum = (std::numeric_limits<float>::lowest)();
        for (const Point point : control) {
            const float projection = point.x * axisX + point.y * axisY;
            controlMinimum = (std::min)(controlMinimum, projection);
            controlMaximum = (std::max)(controlMaximum, projection);
        }
        for (const Point point : clip) {
            const float projection = point.x * axisX + point.y * axisY;
            clipMinimum = (std::min)(clipMinimum, projection);
            clipMaximum = (std::max)(clipMaximum, projection);
        }
        return controlMaximum > clipMinimum && clipMaximum > controlMinimum;
    };
    if (!overlaps(1.0f, 0.0f) || !overlaps(0.0f, 1.0f))
        return false;
    for (size_t index = 0; index < control.size(); ++index) {
        const Point from = control[index];
        const Point to = control[(index + 1u) % control.size()];
        if (!overlaps(-(to.y - from.y), to.x - from.x))
            return false;
    }
    return true;
}

bool IsDefaultFocus(const UiSceneSystem::ControlEntry& entry) noexcept {
    return entry.button && entry.button->IsDefaultFocus();
}

int NavigationOrder(const UiSceneSystem::ControlEntry& entry) noexcept {
    return entry.button ? entry.button->GetNavigationOrder() : 0;
}

bool IsDescendantOf(const GameObject* candidate, const GameObject* ancestor) noexcept {
    for (auto* parent = candidate; parent; parent = parent->GetParent())
        if (parent == ancestor)
            return true;
    return false;
}

constexpr size_t kVirtualKeyBackspace = 0x08u;
constexpr size_t kVirtualKeyControl = 0x11u;
constexpr size_t kVirtualKeyHome = 0x24u;
constexpr size_t kVirtualKeyLeft = 0x25u;
constexpr size_t kVirtualKeyRight = 0x27u;
constexpr size_t kVirtualKeyEnd = 0x23u;
constexpr size_t kVirtualKeyDelete = 0x2Eu;
constexpr size_t kVirtualKeyReturn = 0x0Du;
constexpr size_t kVirtualKeyShift = 0x10u;
constexpr size_t kVirtualKeyA = 0x41u;

} // namespace

void UiSceneSystem::OnWorldAttached(World& world) {
    world_ = &world;
    world.Services().Register(animations_);
    if (!world.Services().Register(*this)) {
        HIKARI_LOG_ERROR("[UI] Could not register the active UI system with its World; input is disabled.");
        input_ = nullptr;
        presentation_ = nullptr;
        bindings_ = nullptr;
        playState_ = nullptr;
        return;
    }
    input_ = world.Services().Find<INPUT::InputService>();
    presentation_ = world.Services().Find<GAME_UI::GameUiPresentationService>();
    bindings_ = world.Services().Find<BINDING::RuntimeBindingStore>();
    playState_ = world.Services().Find<RuntimePlayStateService>();
    elapsedSeconds_ = 0.0f;
    ResetInteraction();
}

void UiSceneSystem::OnWorldDetached(World& world) {
    world.Services().Remove<UiAnimationPlayer>(&animations_);
    ReleaseInputOwnership();
    world.Services().Remove<UiSceneSystem>(this);
    animations_.Reset();
    input_ = nullptr;
    presentation_ = nullptr;
    bindings_ = nullptr;
    playState_ = nullptr;
    world_ = nullptr;
    elapsedSeconds_ = 0.0f;
    layouts_.Clear();
    controls_.clear();
    ResetInteraction();
}

void UiSceneSystem::PreUpdate(World& world, const FrameContext& frame) {
    if (playState_ && playState_->IsActive())
        animations_.Tick(world, frame);
    else
        animations_.Reset();
    if (presentation_ == nullptr || input_ == nullptr || playState_ == nullptr || !playState_->IsActive() ||
        input_->IsDeveloperInputCaptured()) {
        if (presentation_)
            presentation_->BeginInteractionFrame(frame.frameIndex);
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
    if (!activeMenuCanvas_.value) {
        ReleaseMouseCapture();
        ResetInteraction();
        return;
    }
    AcquireMouseCapture();
    ProcessButtons(world, frame);
}

void UiSceneSystem::BuildButtons(World& world) {
    layouts_.Clear();
    controls_.clear();
    SceneObjectId previousCanvas = activeMenuCanvas_;
    activeMenuCanvas_ = {};
    activeMenuOrder_ = (std::numeric_limits<int>::min)();
    (void)world.ForEachObjectWith<UiCanvasComponent>([&](GameObject& object, UiCanvasComponent&) {
        const ResolvedLayout layout = ResolveLayout(world, object);
        if (layout.valid && layout.inputEnabled && layout.interactable && layout.order >= activeMenuOrder_) {
            activeMenuCanvas_ = object.GetDocumentId();
            activeMenuOrder_ = layout.order;
        }
    });
    if (previousCanvas != activeMenuCanvas_) {
        if (previousCanvas.value && focusedControl_.IsValid())
            canvasFocus_[previousCanvas.value] = focusedControl_;
        std::erase_if(canvasFocus_, [&](const auto& entry) { return !world.FindObject(SceneObjectId{entry.first}); });
        const auto saved = canvasFocus_.find(activeMenuCanvas_.value);
        ChangeFocus(saved == canvasFocus_.end() ? RuntimeObjectHandle{} : saved->second, true);
        modalFocusStack_.clear();
        activeModal_ = {};
    }
    if (!activeMenuCanvas_.value)
        return;

    GameObject* modal = nullptr;
    int modalOrder = (std::numeric_limits<int>::min)();
    (void)world.ForEachObjectWith<UiGroupComponent>([&](GameObject& object, UiGroupComponent& group) {
        if (!group.IsModal() || !group.IsVisible() || !group.IsInteractable())
            return;
        const auto layout = ResolveLayout(world, object);
        if (!layout.valid || !layout.inputEnabled || !layout.interactable || layout.canvas != activeMenuCanvas_)
            return;
        if (modal == nullptr || IsDescendantOf(&object, modal) ||
            (!IsDescendantOf(modal, &object) && layout.order >= modalOrder)) {
            modal = &object;
            modalOrder = layout.order;
        }
    });
    const SceneObjectId nextModal = modal ? modal->GetDocumentId() : SceneObjectId{};
    if (nextModal != activeModal_) {
        const GameObject* priorModal = activeModal_.value ? world.FindObject(activeModal_) : nullptr;
        const auto activeFrame = std::find_if(modalFocusStack_.begin(), modalFocusStack_.end(),
                                              [&](const ModalFocusFrame& item) { return item.modal == activeModal_; });
        bool closingNested = !modal;
        if (priorModal && modal)
            closingNested = IsDescendantOf(priorModal, modal);
        else if (!priorModal && modal && activeFrame != modalFocusStack_.end() &&
                 activeFrame != modalFocusStack_.begin())
            closingNested = std::prev(activeFrame)->modal == nextModal;
        if (closingNested) {
            if (activeFrame != modalFocusStack_.end()) {
                const RuntimeObjectHandle restore = activeFrame->priorFocus;
                if (modal)
                    modalFocusStack_.erase(activeFrame, modalFocusStack_.end());
                else
                    modalFocusStack_.clear();
                ChangeFocus(restore, true);
            } else if (!modal) {
                modalFocusStack_.clear();
            }
        } else if (modal) {
            const bool nested = priorModal && IsDescendantOf(modal, priorModal);
            if (!nested)
                modalFocusStack_.clear();
            modalFocusStack_.push_back({nextModal, focusedControl_});
        } else {
            modalFocusStack_.clear();
        }
        activeModal_ = nextModal;
    }
    (void)world.ForEachObjectWith<UiRectComponent, UiButtonComponent>(
        WorldComponentQueryAnchor::SmallestUniqueBucket,
        [&](GameObject& object, UiRectComponent&, UiButtonComponent& button) {
            if (object.GetComponent<UiTextFieldComponent>())
                return;
            ResolvedLayout layout = ResolveLayout(world, object);
            if (layout.valid && layout.inputEnabled && layout.canvas == activeMenuCanvas_) {
                if (modal && !IsDescendantOf(&object, modal))
                    layout.interactable = false;
                controls_.push_back({&object, &button, nullptr, layout});
            }
        });
    (void)world.ForEachObjectWith<UiRectComponent, UiTextFieldComponent>(
        WorldComponentQueryAnchor::SmallestUniqueBucket,
        [&](GameObject& object, UiRectComponent&, UiTextFieldComponent& field) {
            ResolvedLayout layout = ResolveLayout(world, object);
            if (layout.valid && layout.inputEnabled && layout.canvas == activeMenuCanvas_) {
                if (modal && !IsDescendantOf(&object, modal))
                    layout.interactable = false;
                controls_.push_back({&object, nullptr, &field, layout});
            }
        });
    std::stable_sort(controls_.begin(), controls_.end(), [](const ControlEntry& lhs, const ControlEntry& rhs) {
        if (NavigationOrder(lhs) != NavigationOrder(rhs))
            return NavigationOrder(lhs) < NavigationOrder(rhs);
        return lhs.layout.order < rhs.layout.order;
    });
    ConsumeFocusRequests(world);
    if (ControlEntry* focused = FindControl(focusedControl_); focused && IsFocusable(*focused))
        return;
    const auto preferred = std::find_if(controls_.begin(), controls_.end(), [](const ControlEntry& entry) {
        return IsFocusable(entry) && IsDefaultFocus(entry);
    });
    const auto first = preferred != controls_.end()
                           ? preferred
                           : std::find_if(controls_.begin(), controls_.end(),
                                          [](const ControlEntry& entry) { return IsFocusable(entry); });
    if (focusedControl_.IsValid())
        ChangeFocus({}, true);
    if (first != controls_.end())
        ChangeFocus(first->object->GetRuntimeHandle(), true);
}

void UiSceneSystem::ConsumeFocusRequests(World& world) {
    (void)world.ForEachObjectWith<UiCanvasComponent>([&](GameObject& canvasObject, UiCanvasComponent& canvas) {
        if (canvasObject.GetDocumentId() != activeMenuCanvas_)
            return;
        const RuntimeObjectHandle requested = canvas.ConsumePendingFocus();
        if (!requested.IsValid())
            return;
        ControlEntry* entry = FindControl(requested);
        if (entry && IsFocusable(*entry))
            ChangeFocus(requested, true);
    });
}

UiSceneSystem::ControlEntry* UiSceneSystem::FindControl(RuntimeObjectHandle handle) noexcept {
    const auto found = std::find_if(controls_.begin(), controls_.end(), [handle](const ControlEntry& entry) {
        return entry.object->GetRuntimeHandle() == handle;
    });
    return found != controls_.end() ? &*found : nullptr;
}

UiSceneSystem::ControlEntry* UiSceneSystem::HitTest(float screenX, float screenY, uint32_t logicalWidth,
                                                    uint32_t logicalHeight) noexcept {
    ControlEntry* best = nullptr;
    for (ControlEntry& entry : controls_) {
        if (!IsFocusable(entry)) {
            continue;
        }
        GameProjectUiCanvasFit fit{};
        float canvasX = 0.0f;
        float canvasY = 0.0f;
        if (!ResolveGameProjectUiCanvasFit(logicalWidth, logicalHeight, entry.layout.referenceWidth,
                                           entry.layout.referenceHeight, fit) ||
            !ProjectGameUiScreenPointToCanvas(fit, screenX, screenY, canvasX, canvasY) ||
            !ContainsUiPoint(entry.layout, canvasX, canvasY)) {
            continue;
        }
        if (best == nullptr || entry.layout.order >= best->layout.order) {
            best = &entry;
        }
    }
    return best;
}

void UiSceneSystem::ProcessButtons(World& world, const FrameContext& frame) {
    uint32_t width = 0, height = 0;
    INPUT::InputPointerState pointer{};
    const bool pointerValid = SERVICES::TryGetGamePointer(*input_, pointer, width, height);
    const auto& snapshot = input_->GetSnapshot();
    ControlEntry* hovered =
        pointerValid ? HitTest(pointer.x, pointer.y, static_cast<uint32_t>(width), static_cast<uint32_t>(height))
                     : nullptr;
    const bool primaryDown = pointerValid && pointer.primaryDown;
    const bool primaryPressed = pointerValid && pointerInitialized_ && primaryDown && !previousPrimaryDown_;
    const bool primaryReleased = pointerValid && pointerInitialized_ && !primaryDown && previousPrimaryDown_;
    const bool pointerMoved = pointerValid && (!pointerInitialized_ || std::abs(pointer.x - previousPointerX_) > 0.5f ||
                                               std::abs(pointer.y - previousPointerY_) > 0.5f);
    ControlEntry* priorFocus = FindControl(focusedControl_);
    const bool textFieldHasFocus = priorFocus && priorFocus->textField;
    if (hovered && (primaryPressed || (pointerMoved && !textFieldHasFocus && !hovered->textField)))
        ChangeFocus(hovered->object->GetRuntimeHandle(), true);
    if (primaryPressed) {
        if (hovered && hovered->textField) {
            GameProjectUiCanvasFit fit{};
            float canvasX = 0.0f, canvasY = 0.0f;
            if (ResolveGameProjectUiCanvasFit(width, height, hovered->layout.referenceWidth,
                                              hovered->layout.referenceHeight, fit) &&
                ProjectGameUiScreenPointToCanvas(fit, pointer.x, pointer.y, canvasX, canvasY) &&
                hovered->layout.transform.InverseTransform(canvasX, canvasY)) {
                size_t nearest = hovered->textField->GetCaretByteOffset();
                float nearestDistance = (std::numeric_limits<float>::max)();
                {
                    for (const TextCaretStop& stop : ReadTextCaretStops(*hovered)) {
                        const float dx = std::abs(stop.x - canvasX);
                        const float dy = std::abs(stop.y + stop.height * 0.5f - canvasY);
                        const float distance = dx + dy * 2.0f;
                        if (distance < nearestDistance) {
                            nearestDistance = distance;
                            nearest = hovered->textField->ToCommittedOffset(stop.byteOffset);
                        }
                    }
                }
                if (auto* text = hovered->object->GetComponent<UiTextComponent>())
                    hovered->textField->SetCaret(*text, nearest,
                                                 snapshot.GetDevices().keyboard[kVirtualKeyShift] != 0u);
            }
        } else if (!hovered) {
            ChangeFocus({}, true);
        }
    }
    if (pointerValid) {
        previousPointerX_ = pointer.x;
        previousPointerY_ = pointer.y;
    }

    const float wheel = snapshot.GetDevices().mouseWheel;
    if (pointerValid && std::isfinite(wheel) && std::abs(wheel) > 1.0e-4f) {
        UiScrollViewComponent* target = nullptr;
        GameObject* targetObject = nullptr;
        GameObject* modalRoot = activeModal_.value ? world.FindObject(activeModal_) : nullptr;
        size_t targetDepth = 0u;
        int targetOrder = (std::numeric_limits<int>::min)();
        (void)world.ForEachObjectWith<UiRectComponent, UiScrollViewComponent>(
            WorldComponentQueryAnchor::SmallestUniqueBucket,
            [&](GameObject& object, UiRectComponent&, UiScrollViewComponent& scroll) {
                if (!scroll.IsEnabled())
                    return;
                const ResolvedLayout layout = ResolveLayout(world, object);
                GameProjectUiCanvasFit fit{};
                float canvasX = 0.0f, canvasY = 0.0f;
                if (!layout.valid || !layout.inputEnabled || !layout.interactable ||
                    layout.canvas != activeMenuCanvas_ || (modalRoot && !IsDescendantOf(&object, modalRoot)) ||
                    !ResolveGameProjectUiCanvasFit(width, height, layout.referenceWidth, layout.referenceHeight, fit) ||
                    !ProjectGameUiScreenPointToCanvas(fit, pointer.x, pointer.y, canvasX, canvasY) ||
                    !ContainsUiPoint(layout, canvasX, canvasY))
                    return;
                size_t depth = 0u;
                for (auto* ancestor = object.GetParent(); ancestor; ancestor = ancestor->GetParent())
                    ++depth;
                if (!target || depth > targetDepth || (depth == targetDepth && layout.order > targetOrder)) {
                    target = &scroll;
                    targetObject = &object;
                    targetDepth = depth;
                    targetOrder = layout.order;
                }
            });
        if (target && targetObject) {
            const ResolvedLayout viewport = ResolveLayout(world, *targetObject);
            const float extent = target->GetAxis() == UiScrollAxis::Vertical ? viewport.height : viewport.width;
            target->ScrollBy(-wheel * target->GetScrollSpeed(), extent);
            layouts_.Clear();
        }
    }

    ControlEntry* keyboardFocus = FindControl(focusedControl_);
    const bool fieldOwnsNavigation =
        keyboardFocus && keyboardFocus->textField &&
        (keyboardFocus->textField->IsComposing() || !snapshot.GetTextInputEvents().empty());
    if (!fieldOwnsNavigation) {
        const std::array<float, 2u> navigation =
            snapshot.GetAxis2D(input_->GetActionMap().FindAction("UI.Navigate") ? "UI.Navigate" : "Gameplay.Move");
        if (std::abs(navigation[1]) >= 0.55f && std::abs(previousNavigationY_) < 0.55f)
            MoveFocus(navigation[1] > 0.0f ? -1 : 1);
        previousNavigationY_ = navigation[1];
    } else {
        previousNavigationY_ = 0.0f;
    }

    if (pointerValid) {
        previousPrimaryDown_ = primaryDown;
        pointerInitialized_ = true;
    } else {
        previousPrimaryDown_ = false;
        pointerInitialized_ = false;
        pressedButton_ = {};
    }
    if (primaryPressed)
        pressedButton_ = hovered && hovered->button ? hovered->object->GetRuntimeHandle() : RuntimeObjectHandle{};
    RuntimeObjectHandle activation{};
    if (primaryReleased && pressedButton_.IsValid() && hovered && hovered->button &&
        hovered->object->GetRuntimeHandle() == pressedButton_)
        activation = pressedButton_;
    if (!primaryDown)
        pressedButton_ = {};

    ControlEntry* focused = FindControl(focusedControl_);
    if (focused && focused->textField) {
        ProcessTextField(world, frame, *focused, pointerValid, pointer, width, height);
    } else if (!activation.IsValid() && focused && focused->button && !focused->button->GetSubmitActionId().empty() &&
               snapshot.IsPressed(focused->button->GetSubmitActionId() == "Gameplay.Interact" &&
                                          input_->GetActionMap().FindAction("UI.Submit")
                                      ? "UI.Submit"
                                      : focused->button->GetSubmitActionId())) {
        activation = focusedControl_;
    }

    for (ControlEntry& entry : controls_) {
        if (!entry.button)
            continue;
        const RuntimeObjectHandle handle = entry.object->GetRuntimeHandle();
        const bool isHovered = hovered && handle == hovered->object->GetRuntimeHandle();
        const UiButtonVisualState state =
            !entry.button->IsEnabled() || !entry.button->IsInteractable() || !entry.layout.interactable
                ? UiButtonVisualState::Disabled
            : handle == pressedButton_ && primaryDown ? UiButtonVisualState::Pressed
            : isHovered || handle == focusedControl_  ? UiButtonVisualState::Hovered
                                                      : UiButtonVisualState::Normal;
        entry.button->SetVisualState(state);
    }
    if (activation.IsValid())
        ActivateButton(activation, frame.frameIndex);
    previousKeyboard_ = snapshot.GetDevices().keyboard;
    previousKeyboardInitialized_ = true;
    focusChangedThisFrame_ = false;
    if (!focused || !focused->textField)
        input_->SetImeCaretAnchor(INPUT::ImeCaretAnchor{});
    animations_.Tick(world, FrameContext{});
}

std::vector<UiSceneSystem::TextCaretStop> UiSceneSystem::ReadTextCaretStops(const ControlEntry& control) const {
    if (!control.textField || !control.object)
        return {};
    const auto* text = control.object->GetComponent<UiTextComponent>();
    if (!text)
        return {};
    const auto& feedback = control.textField->LayoutFeedback();
    std::lock_guard lock(feedback->mutex);
    const auto& expected = feedback->expected;
    if (!feedback->ready || expected.utf8Text != control.textField->DisplayText(*text) ||
        expected.x != control.layout.x || expected.y != control.layout.y || expected.maxWidth != control.layout.width ||
        expected.maxHeight != control.layout.height)
        return {};
    return feedback->stops;
}

void UiSceneSystem::ProcessTextField(World&, const FrameContext& frame, ControlEntry& control, bool pointerValid,
                                     const INPUT::InputPointerState&, uint32_t logicalWidth, uint32_t logicalHeight) {
    if (!control.textField || !control.object || focusChangedThisFrame_)
        return;
    auto* text = control.object->GetComponent<UiTextComponent>();
    if (!text)
        return;

    const bool wasComposing = control.textField->IsComposing();
    bool compositionEvent = false;
    bool committed = false;
    for (const INPUT::TextInputEvent& event : input_->GetSnapshot().GetTextInputEvents()) {
        switch (event.kind) {
        case INPUT::TextInputEventKind::CompositionStarted:
            control.textField->SetComposition({}, true);
            compositionEvent = true;
            break;
        case INPUT::TextInputEventKind::CompositionUpdated:
            control.textField->SetComposition(event.utf8, true);
            compositionEvent = true;
            break;
        case INPUT::TextInputEventKind::Committed:
            control.textField->InsertCommittedText(*text, event.utf8);
            committed = true;
            compositionEvent = true;
            break;
        case INPUT::TextInputEventKind::CompositionEnded:
            control.textField->ClearComposition();
            compositionEvent = true;
            break;
        }
    }

    const auto& keyboard = input_->GetSnapshot().GetDevices().keyboard;
    const auto pressed = [&](size_t key) {
        return previousKeyboardInitialized_ && key < keyboard.size() && keyboard[key] != 0u &&
               previousKeyboard_[key] == 0u;
    };
    const bool composing = wasComposing || control.textField->IsComposing() || compositionEvent;
    if (!composing && previousKeyboardInitialized_) {
        const bool shift = keyboard[kVirtualKeyShift] != 0u;
        const bool controlDown = keyboard[kVirtualKeyControl] != 0u;
        if (controlDown && pressed(kVirtualKeyA))
            control.textField->SelectAll(*text);
        else if (pressed(kVirtualKeyLeft))
            control.textField->MoveCaret(*text, -1, shift);
        else if (pressed(kVirtualKeyRight))
            control.textField->MoveCaret(*text, 1, shift);
        else if (pressed(kVirtualKeyHome))
            control.textField->SetCaret(*text, 0u, shift);
        else if (pressed(kVirtualKeyEnd))
            control.textField->SetCaret(*text, text->GetText().size(), shift);
        else if (pressed(kVirtualKeyBackspace))
            control.textField->DeleteBackward(*text);
        else if (pressed(kVirtualKeyDelete))
            control.textField->DeleteForward(*text);
    }

    const bool enterPressed = pressed(kVirtualKeyReturn) || input_->GetSnapshot().IsPressed("UI.Submit");
    if (!composing && !committed && enterPressed && bindings_ && !control.textField->GetSubmitEventId().empty())
        (void)bindings_->Emit(control.textField->GetSubmitEventId(), control.object->GetRuntimeHandle());

    INPUT::ImeCaretAnchor anchor{};
    if (pointerValid && logicalWidth && logicalHeight) {
        TextCaretStop stop{};
        bool found = false;
        {
            const auto stops = ReadTextCaretStops(control);
            const auto caret = std::find_if(stops.begin(), stops.end(), [&](const TextCaretStop& item) {
                return item.byteOffset == control.textField->DisplayCaret();
            });
            if (caret != stops.end()) {
                stop = *caret;
                found = true;
            }
        }
        if (!found) {
            stop.x = control.layout.x;
            stop.y = control.layout.y;
            stop.height = control.object->GetComponent<UiTextComponent>()->GetPixelHeight();
        }
        control.layout.transform.Transform(stop.x, stop.y);
        GameProjectUiCanvasFit fit{};
        if (ResolveGameProjectUiCanvasFit(logicalWidth, logicalHeight, control.layout.referenceWidth,
                                          control.layout.referenceHeight, fit)) {
            anchor.clientX = static_cast<int32_t>(std::lround(fit.offsetX + stop.x * fit.scale));
            anchor.clientY =
                static_cast<int32_t>(std::lround(fit.offsetY + stop.y * fit.scale + stop.height * fit.scale));
            anchor.valid = true;
        }
    }
    input_->SetImeCaretAnchor(anchor);
    (void)frame;
}

void UiSceneSystem::MoveFocus(int direction) noexcept {
    if (controls_.empty() || direction == 0)
        return;
    size_t current = 0u;
    for (size_t index = 0u; index < controls_.size(); ++index) {
        if (controls_[index].object->GetRuntimeHandle() == focusedControl_) {
            current = index;
            break;
        }
    }
    for (size_t step = 1u; step <= controls_.size(); ++step) {
        const size_t index = direction > 0
                                 ? (current + step) % controls_.size()
                                 : (current + controls_.size() - (step % controls_.size())) % controls_.size();
        const ControlEntry& candidate = controls_[index];
        if (IsFocusable(candidate)) {
            ChangeFocus(candidate.object->GetRuntimeHandle(), true);
            return;
        }
    }
}

void UiSceneSystem::ActivateButton(RuntimeObjectHandle handle, uint64_t frameIndex) {
    ControlEntry* entry = FindControl(handle);
    if (entry == nullptr || !entry->button || presentation_ == nullptr)
        return;
    (void)presentation_->PublishButtonActivation(handle, frameIndex);
    if (auto* world = entry->object->GetWorld())
        entry->button->ApplyGroupAction(*world);
    if (bindings_ != nullptr && !entry->button->GetOnClickEventId().empty()) {
        (void)bindings_->Emit(entry->button->GetOnClickEventId(), handle);
    }
}

void UiSceneSystem::ResetButtonStates(World& world) {
    (void)world.ForEachObjectWith<UiButtonComponent>(
        [](GameObject&, UiButtonComponent& button) { button.SetVisualState(UiButtonVisualState::Normal); });
}

void UiSceneSystem::AcquireMouseCapture() {
    if (input_ == nullptr)
        return;
    if (!ownsMouseCapture_) {
        previousMouseCaptureMode_ = input_->GetMouseCaptureMode();
        previousUiInteraction_ = input_->IsUiInteractionActive();
        previousUiContext_ = input_->Contexts().IsActive("UI");
    }
    input_->SetUiInteractionActive(true);
    if (input_->GetActionMap().FindContext("UI"))
        input_->Contexts().SetActive("UI", true);
    input_->SetMouseCaptureMode(INPUT::MouseCaptureMode::Free);
    ownsMouseCapture_ = true;
}

void UiSceneSystem::ReleaseMouseCapture() {
    if (!ownsMouseCapture_ || input_ == nullptr)
        return;
    // Play 終了や開発者操作への切替後に、古いゲーム入力の拘束を復活させない。
    const bool runtimeOwnsInput = playState_ && playState_->IsActive() && !input_->IsDeveloperInputCaptured();
    if (runtimeOwnsInput && input_->GetMouseCaptureMode() == INPUT::MouseCaptureMode::Free)
        input_->SetMouseCaptureMode(previousMouseCaptureMode_);
    if (input_->IsUiInteractionActive())
        input_->SetUiInteractionActive(previousUiInteraction_);
    if (input_->Contexts().IsActive("UI"))
        input_->Contexts().SetActive("UI", previousUiContext_);
    ownsMouseCapture_ = false;
    previousMouseCaptureMode_ = INPUT::MouseCaptureMode::Free;
}

void UiSceneSystem::ChangeFocus(RuntimeObjectHandle control, bool resetTextInput) noexcept {
    if (focusedControl_ == control)
        return;
    if (world_)
        if (GameObject* previous = world_->FindObject(focusedControl_))
            if (auto* field = previous->GetComponent<UiTextFieldComponent>())
                field->ClearComposition();
    focusedControl_ = control;
    pressedButton_ = {};
    focusChangedThisFrame_ = true;
    previousKeyboardInitialized_ = false;
    if (resetTextInput && input_) {
        input_->DiscardTextInputEvents();
        input_->ResetTextInput();
        input_->SetImeCaretAnchor(INPUT::ImeCaretAnchor{});
    }
    if (ControlEntry* entry = FindControl(control); entry && entry->textField) {
        entry->textField->ResetEditingState();
        if (auto* text = entry->object->GetComponent<UiTextComponent>())
            entry->textField->SetCaret(*text, text->GetText().size(), false);
    }
}

void UiSceneSystem::ReleaseInputOwnership() noexcept {
    ReleaseMouseCapture();
    ChangeFocus({}, true);
    pressedButton_ = {};
    activeMenuCanvas_ = {};
    activeModal_ = {};
    modalFocusStack_.clear();
    canvasFocus_.clear();
    activeMenuOrder_ = (std::numeric_limits<int>::min)();
    ResetInteraction();
}

void UiSceneSystem::ResetInteraction() noexcept {
    ChangeFocus({}, true);
    pressedButton_ = {};
    pointerInitialized_ = false;
    previousPrimaryDown_ = false;
    previousNavigationY_ = 0.0f;
    previousKeyboardInitialized_ = false;
    focusChangedThisFrame_ = false;
}

std::unique_ptr<ISystem> CreateUiSceneSystem() { return std::make_unique<UiSceneSystem>(); }

} // namespace HIKARI
