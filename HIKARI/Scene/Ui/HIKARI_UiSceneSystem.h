#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"

namespace HIKARI {

class GameObject;
class UiButtonComponent;
class UiCanvasComponent;

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
    }
    UiAnimationPlayer& Animations() { return animations_; }
    uint32_t RejectedTextRequests() const { return rejectedTextRequests_; }

    struct ButtonEntry {
        GameObject* object = nullptr;
        UiButtonComponent* button = nullptr;
        ResolvedLayout layout{};
    };

private:
    void SubmitImages(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                      float timeSeconds);
    void SubmitTexts(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                     float timeSeconds);

    void BuildButtons(World& world);
    ButtonEntry* FindButton(RuntimeObjectHandle handle) noexcept;
    ButtonEntry* HitTest(float screenX, float screenY, uint32_t logicalWidth, uint32_t logicalHeight) noexcept;
    void ProcessButtons(World& world, const FrameContext& frame);
    void MoveFocus(int direction) noexcept;
    void ActivateButton(RuntimeObjectHandle handle, uint64_t frameIndex);
    void ResetButtonStates(World& world);
    void AcquireMouseCapture();
    void ReleaseMouseCapture();
    void ResetInteraction() noexcept;

    INPUT::InputService* input_ = nullptr;
    GAME_UI::GameUiPresentationService* presentation_ = nullptr;
    BINDING::RuntimeBindingStore* bindings_ = nullptr;
    const RuntimePlayStateService* playState_ = nullptr;
    UiAnimationPlayer animations_;
    UiLayoutResolver layouts_{&animations_};
    uint32_t rejectedTextRequests_ = 0;
    std::vector<ButtonEntry> buttons_{};
    RuntimeObjectHandle focusedButton_{};
    RuntimeObjectHandle pressedButton_{};
    float elapsedSeconds_ = 0.0f;
    float previousNavigationY_ = 0.0f;
    float previousPointerX_ = 0, previousPointerY_ = 0;
    bool pointerInitialized_ = false;
    bool previousPrimaryDown_ = false;
    bool ownsMouseCapture_ = false;
    bool previousUiInteraction_ = false, previousUiContext_ = false;
    INPUT::MouseCaptureMode previousMouseCaptureMode_ = INPUT::MouseCaptureMode::Free;
};

std::unique_ptr<ISystem> CreateUiSceneSystem();

} // namespace HIKARI
