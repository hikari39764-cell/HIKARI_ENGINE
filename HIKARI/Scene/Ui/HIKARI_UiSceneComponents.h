#pragma once

#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "HIKARI/Project/UiTypes.h"
#include "Render3D/HIKARI_Math3D.h"
#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    inline constexpr std::string_view kUiCanvasComponentType =
        "UiCanvasComponent";
    inline constexpr std::string_view kUiRectComponentType =
        "UiRectComponent";
    inline constexpr std::string_view kUiImageComponentType =
        "UiImageComponent";
    inline constexpr std::string_view kUiTextComponentType =
        "UiTextComponent";
    inline constexpr std::string_view kUiButtonComponentType =
        "UiButtonComponent";
    inline constexpr ComponentTypeId kUiCanvasComponentTypeId =
        MakeComponentTypeId(kUiCanvasComponentType);
    inline constexpr ComponentTypeId kUiRectComponentTypeId =
        MakeComponentTypeId(kUiRectComponentType);
    inline constexpr ComponentTypeId kUiImageComponentTypeId =
        MakeComponentTypeId(kUiImageComponentType);
    inline constexpr ComponentTypeId kUiTextComponentTypeId =
        MakeComponentTypeId(kUiTextComponentType);
    inline constexpr ComponentTypeId kUiButtonComponentTypeId =
        MakeComponentTypeId(kUiButtonComponentType);
    inline constexpr int kUiCanvasSortOrderLimit = 2000;
    inline constexpr int kUiElementOrderLimit = 200000;
    inline constexpr int kUiCanvasOrderStride = 1000000;

    enum class UiButtonVisualState : uint8_t {
        Normal = 0u,
        Hovered,
        Pressed,
        Disabled,
    };

    class UiCanvasComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return kUiCanvasComponentType;
        }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        bool IsInteractable() const noexcept { return interactable_; }
        float GetReferenceWidth() const noexcept { return referenceWidth_; }
        void SetEnabled(bool value) noexcept { enabled_ = value; }
        float GetReferenceHeight() const noexcept { return referenceHeight_; }
        int GetSortOrder() const noexcept { return sortOrder_; }

    private:
        float referenceWidth_ = 1920.0f;
        float referenceHeight_ = 1080.0f;
        int sortOrder_ = 0;
        bool enabled_ = true;
        bool interactable_ = true;
    };

    class UiRectComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return kUiRectComponentType;
        }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        const MATH::Vec2& GetAnchorMinimum() const noexcept {
            return anchorMinimum_;
        }
        const MATH::Vec2& GetAnchorMaximum() const noexcept {
            return anchorMaximum_;
        }
        const MATH::Vec2& GetOffsetMinimum() const noexcept {
            return offsetMinimum_;
        }
        const MATH::Vec2& GetOffsetMaximum() const noexcept {
            return offsetMaximum_;
        }
        const MATH::Vec2& GetPivot() const noexcept { return pivot_; }
        float GetRotationDegrees() const noexcept { return rotationDegrees_; }
        int GetOrder() const noexcept { return order_; }

    private:
        void Sanitize() noexcept;

        MATH::Vec2 anchorMinimum_{ 0.5f, 0.5f };
        MATH::Vec2 anchorMaximum_{ 0.5f, 0.5f };
        MATH::Vec2 offsetMinimum_{ -160.0f, -40.0f };
        MATH::Vec2 offsetMaximum_{ 160.0f, 40.0f };
        MATH::Vec2 pivot_{ 0.5f, 0.5f };
        float rotationDegrees_ = 0.0f;
        int order_ = 0;
        bool enabled_ = true;
    };

    class UiImageComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return kUiImageComponentType;
        }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        const AssetGuid& GetTextureAssetGuid() const noexcept {
            return textureAssetGuid_;
        }
        const std::string& GetTextureAssetPath() const noexcept {
            return textureAssetPath_;
        }
        const GameProjectUiColor& GetTint() const noexcept { return tint_; }
        int GetOrderOffset() const noexcept { return orderOffset_; }

    private:
        AssetGuid textureAssetGuid_{};
        std::string textureAssetPath_{ "generated://solid/ffffffff" };
        GameProjectUiColor tint_{ 0.08f, 0.12f, 0.20f, 0.96f };
        int orderOffset_ = 0;
        bool enabled_ = true;
    };

    class UiTextComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return kUiTextComponentType;
        }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        const std::string& GetText() const noexcept { return text_; }
        bool SetText(std::string_view value) {
            if (value.size() > 16384 || value.find('\0') != std::string_view::npos) return false;
            if (text_ != value) text_.assign(value);
            return true;
        }
        const std::string& GetFontAssetPath() const noexcept {
            return fontAssetPath_;
        }
        float GetPixelHeight() const noexcept { return pixelHeight_; }
        const GameProjectUiColor& GetColor() const noexcept { return color_; }
        GameProjectUiTextAlign GetHorizontalAlign() const noexcept {
            return horizontalAlign_;
        }
        GameProjectUiVerticalAlign GetVerticalAlign() const noexcept {
            return verticalAlign_;
        }
        int GetOrderOffset() const noexcept { return orderOffset_; }

    private:
        std::string text_{ "Button" };
        std::string fontAssetPath_{};
        GameProjectUiColor color_{};
        float pixelHeight_ = 34.0f;
        int orderOffset_ = 1;
        GameProjectUiTextAlign horizontalAlign_ =
            GameProjectUiTextAlign::Center;
        GameProjectUiVerticalAlign verticalAlign_ =
            GameProjectUiVerticalAlign::Center;
        bool enabled_ = true;
    };

    class UiButtonComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return kUiButtonComponentType;
        }
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        bool IsEnabled() const noexcept { return enabled_; }
        bool IsInteractable() const noexcept { return interactable_; }
        bool IsDefaultFocus() const noexcept { return defaultFocus_; }
        void SetInteractable(bool value) noexcept { interactable_ = value; }
        int GetNavigationOrder() const noexcept { return navigationOrder_; }
        const std::string& GetSubmitActionId() const noexcept {
            return submitActionId_;
        }
        const std::string& GetOnClickEventId() const noexcept {
            return onClickEventId_;
        }
        UiButtonVisualState GetVisualState() const noexcept {
            return visualState_;
        }
        const GameProjectUiColor& GetStateTint() const noexcept;
        const GameProjectUiColor& GetAnimatedTint() const noexcept { return animatedTint_; }
        void AdvanceVisual(float unscaledDt) noexcept;
        void SetVisualState(UiButtonVisualState state) noexcept {
            if (visualState_ == state) return;
            transitionFrom_ = animatedTint_;
            transitionElapsed_ = 0;
            visualState_ = state;
        }

    private:
        std::string submitActionId_{ "Gameplay.Interact" };
        std::string onClickEventId_{};
        GameProjectUiColor normalTint_{};
        GameProjectUiColor hoverTint_{ 0.72f, 0.88f, 1.0f, 1.0f };
        GameProjectUiColor pressedTint_{ 0.45f, 0.72f, 1.0f, 1.0f };
        GameProjectUiColor disabledTint_{ 0.38f, 0.38f, 0.42f, 0.65f };
        int navigationOrder_ = 0;
        bool enabled_ = true;
        bool interactable_ = true;
        bool defaultFocus_ = false;
        UiButtonVisualState visualState_ = UiButtonVisualState::Normal;
        GameProjectUiColor animatedTint_{};
        GameProjectUiColor transitionFrom_{};
        float transitionElapsed_ = 0;
        float transitionSeconds_ = 0.1f;
    };

} // namespace HIKARI
