#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class GameObject;
    class UiButtonComponent;
    class UiCanvasComponent;

    namespace BINDING { class RuntimeBindingStore; }
    namespace GAME_UI { class GameUiPresentationService; }
    namespace INPUT { class InputService; }
    struct RuntimePlayStateService;

    inline constexpr std::string_view kUiSceneSystemId = "UiSceneSystem";

    class UiSceneSystem final : public ISystem {
    public:
        std::string_view GetName() const override {
            return kUiSceneSystemId;
        }
        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void Update(World& world, const FrameContext& frame) override;
        void PreRender(World& world, const FrameContext& frame) override;

    public:
        struct ResolvedLayout {
            float referenceWidth = 0.0f;
            float referenceHeight = 0.0f;
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float pivotX = 0.5f;
            float pivotY = 0.5f;
            float rotationRadians = 0.0f;
            int order = 0;
            bool interactable = false;
            bool valid = false;
        };

        struct ButtonEntry {
            GameObject* object = nullptr;
            UiButtonComponent* button = nullptr;
            ResolvedLayout layout{};
        };

    private:

        UiCanvasComponent* FindCanvas(
            World& world,
            GameObject& object,
            GameObject*& outOwner) noexcept;
        ResolvedLayout ResolveLayout(
            World& world,
            GameObject& object,
            uint32_t depth = 0u) noexcept;
        void SubmitImages(World& world, const FrameContext& frame);
        void SubmitTexts(World& world, const FrameContext& frame);

        void BuildButtons(World& world);
        ButtonEntry* FindButton(RuntimeObjectHandle handle) noexcept;
        ButtonEntry* HitTest(
            float screenX,
            float screenY,
            uint32_t logicalWidth,
            uint32_t logicalHeight) noexcept;
        void ProcessButtons(World& world, const FrameContext& frame);
        void MoveFocus(int direction) noexcept;
        void ActivateButton(
            RuntimeObjectHandle handle,
            uint64_t frameIndex);
        void ResetButtonStates(World& world);
        void AcquireMouseCapture();
        void ReleaseMouseCapture();
        void ResetInteraction() noexcept;

        INPUT::InputService* input_ = nullptr;
        GAME_UI::GameUiPresentationService* presentation_ = nullptr;
        BINDING::RuntimeBindingStore* bindings_ = nullptr;
        const RuntimePlayStateService* playState_ = nullptr;
        std::unordered_map<uint64_t, ResolvedLayout> layouts_{};
        std::vector<ButtonEntry> buttons_{};
        RuntimeObjectHandle focusedButton_{};
        RuntimeObjectHandle pressedButton_{};
        float elapsedSeconds_ = 0.0f;
        float previousNavigationY_ = 0.0f;
        bool pointerInitialized_ = false;
        bool previousPrimaryDown_ = false;
        bool ownsMouseCapture_ = false;
        INPUT::MouseCaptureMode previousMouseCaptureMode_ =
            INPUT::MouseCaptureMode::Free;
    };

    std::unique_ptr<ISystem> CreateUiSceneSystem();

} // namespace HIKARI
