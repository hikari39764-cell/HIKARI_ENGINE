#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"

namespace HIKARI {

class GameObject;
class UiButtonComponent;
class UiCanvasComponent;
struct TextStyleAsset;

namespace BINDING {
class RuntimeBindingStore;
}
namespace GAME_UI {
class GameUiPresentationService;
}
namespace INPUT {
class InputService;
}
struct RuntimePlayStateService;

inline constexpr std::string_view kUiSceneSystemId = "UiSceneSystem";

class UiSceneSystem final : public ISystem {
public:
    std::string_view GetName() const override { return kUiSceneSystemId; }
    void OnWorldAttached(World& world) override;
    void OnWorldDetached(World& world) override;
    // 入力を component Update より前に公開し、同一フレームで消費させる。
    void PreUpdate(World& world, const FrameContext& frame) override;
    void PreRender(World& world, const FrameContext& frame) override;

public:
    using ResolvedLayout = UiResolvedLayout;
    // 編集時も同じ配置と描画要求を生成し、実行中の入力状態には触れない。
    void BuildPresentation(World& world, const FrameContext& frame, GAME_UI::GameUiPresentationService& presentation,
                           SceneObjectId canvas = {}, float timeSeconds = 0);
    ResolvedLayout ResolveLayout(World& world, GameObject& object) { return layouts_.Resolve(world, object); }
    void SetPreviewAnimations(const UiAnimationPlayer* player, SceneObjectId root = {}) {
        layouts_.SetAnimationPlayer(player);
        layouts_.SetPreviewRoot(root);
        layouts_.SetAuthoringPreview(true);
    }
    void SetPreviewBindings(const BINDING::RuntimeBindingStore* bindings) {
        previewBindings_ = bindings;
        layouts_.SetPreviewBindings(bindings);
    }
    void SetPreviewTextStyle(SceneObjectId object, const TextStyleAsset* style) {
        previewTextObject_ = object;
        previewTextStyle_ = style;
    }
    UiAnimationPlayer& Animations() { return animations_; }
    RuntimeObjectHandle GetFocusedControl() const noexcept { return focusedControl_; }
    void ReleaseInputOwnership() noexcept;
    uint32_t RejectedTextRequests() const { return rejectedTextRequests_; }

    using TextCaretStop = TEXT_RENDERING::SdfTextCaretStop;
    struct ControlEntry {
        GameObject* object = nullptr;
        UiButtonComponent* button = nullptr;
        UiTextFieldComponent* textField = nullptr;
        ResolvedLayout layout{};
    };

private:
    const BINDING::RuntimeBindingStore* previewBindings_ = nullptr;
    SceneObjectId previewTextObject_{};
    const TextStyleAsset* previewTextStyle_ = nullptr;
    std::vector<TextCaretStop> ReadTextCaretStops(const ControlEntry& control) const;
    void SubmitImages(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                      float timeSeconds);
    void SubmitTexts(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                     float timeSeconds);

    void BuildButtons(World& world);
    ControlEntry* FindControl(RuntimeObjectHandle handle) noexcept;
    ControlEntry* HitTest(float screenX, float screenY, uint32_t logicalWidth, uint32_t logicalHeight) noexcept;
    void ProcessButtons(World& world, const FrameContext& frame);
    void MoveFocus(int direction) noexcept;
    void ActivateButton(RuntimeObjectHandle handle, uint64_t frameIndex);
    void ProcessTextField(World& world, const FrameContext& frame, ControlEntry& control, bool pointerValid,
                          const INPUT::InputPointerState& pointer, uint32_t logicalWidth, uint32_t logicalHeight);
    void ChangeFocus(RuntimeObjectHandle control, bool resetTextInput) noexcept;
    void ResetButtonStates(World& world);
    void AcquireMouseCapture();
    void ReleaseMouseCapture();
    void ConsumeFocusRequests(World& world);
    void ResetInteraction() noexcept;

    INPUT::InputService* input_ = nullptr;
    World* world_ = nullptr;
    GAME_UI::GameUiPresentationService* presentation_ = nullptr;
    BINDING::RuntimeBindingStore* bindings_ = nullptr;
    const RuntimePlayStateService* playState_ = nullptr;
    UiAnimationPlayer animations_;
    UiLayoutResolver layouts_{&animations_};
    uint32_t rejectedTextRequests_ = 0;
    std::vector<ControlEntry> controls_{};
    RuntimeObjectHandle focusedControl_{};
    RuntimeObjectHandle pressedButton_{};
    struct ModalFocusFrame {
        SceneObjectId modal{};
        RuntimeObjectHandle priorFocus{};
    };
    std::vector<ModalFocusFrame> modalFocusStack_{};
    std::unordered_map<uint64_t, RuntimeObjectHandle> canvasFocus_{};
    SceneObjectId activeMenuCanvas_{};
    SceneObjectId activeModal_{};
    int activeMenuOrder_ = (std::numeric_limits<int>::min)();
    float elapsedSeconds_ = 0.0f;
    float previousNavigationY_ = 0.0f;
    float previousPointerX_ = 0, previousPointerY_ = 0;
    bool pointerInitialized_ = false;
    bool previousPrimaryDown_ = false;
    bool focusChangedThisFrame_ = false;
    std::array<uint8_t, 256u> previousKeyboard_{};
    bool previousKeyboardInitialized_ = false;
    bool ownsMouseCapture_ = false;
    bool authoringPreview_ = false;
    bool previousUiInteraction_ = false, previousUiContext_ = false;
    INPUT::MouseCaptureMode previousMouseCaptureMode_ = INPUT::MouseCaptureMode::Free;
};

std::unique_ptr<ISystem> CreateUiSceneSystem();

} // namespace HIKARI
