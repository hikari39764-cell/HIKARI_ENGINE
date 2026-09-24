#pragma once

#include "Assets/Text/HIKARI_TextStyleBinding.h"
#include <string>
#include <string_view>

#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

namespace WorldTextComponentPropertyIds {
inline constexpr char Text[] = "text";
inline constexpr char FontAssetPath[] = "fontAssetPath";
inline constexpr char Visible[] = "visible";
inline constexpr char LayoutSize[] = "layoutSize";
inline constexpr char FontHeight[] = "fontHeight";
inline constexpr char HorizontalAlign[] = "horizontalAlign";
inline constexpr char VerticalAlign[] = "verticalAlign";
inline constexpr char Wrap[] = "wrap";
inline constexpr char LineHeightMultiplier[] = "lineHeightMultiplier";
inline constexpr char LetterSpacing[] = "letterSpacing";
inline constexpr char Facing[] = "facing";
inline constexpr char Color[] = "color";
inline constexpr char OutlineColor[] = "outlineColor";
inline constexpr char OutlineWidth[] = "outlineWidth";
inline constexpr char Opacity[] = "opacity";
inline constexpr char RevealAmount[] = "revealAmount";
inline constexpr char Pivot[] = "pivot";
inline constexpr char TransparentSortPriority[] = "transparentSortPriority";
} // namespace WorldTextComponentPropertyIds

class WorldTextComponent final : public IComponent, public IGeometryFitProvider {
public:
    using FacingMode = RENDER3D::WORLD_TEXT::WorldTextFacingMode;
    using HorizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign;
    using VerticalAlign = TEXT_RENDERING::SdfTextVerticalAlign;
    using WrapMode = TEXT_RENDERING::SdfTextWrap;

    bool IsCanvasSourceOnly() const noexcept { return canvasSourceOnly_; }
    void SetCanvasSourceOnly(bool value) {
        if (canvasSourceOnly_ == value)
            return;
        canvasSourceOnly_ = value;
        NotifyRenderStateDirty();
    }
    std::string_view GetTypeName() const override { return "WorldTextComponent"; }

    const std::string& GetText() const noexcept { return text_; }
    void SetText(std::string text);
    TextStyleAsset ResolveAppearance(const TextAssetStore* store) const;
    const TextStyleBinding& GetStyleBinding() const noexcept { return binding_; }
    const std::string& GetFontAssetPath() const noexcept { return fontAssetPath_; }
    void SetFontAssetPath(std::string path);
    bool IsVisible() const noexcept { return visible_; }
    void SetVisible(bool visible);
    const MATH::Vec2& GetLayoutSize() const noexcept { return layoutSize_; }
    void SetLayoutSize(MATH::Vec2 size);
    float GetFontHeight() const noexcept { return fontHeight_; }
    void SetFontHeight(float height);
    HorizontalAlign GetHorizontalAlign() const noexcept { return horizontalAlign_; }
    void SetHorizontalAlign(HorizontalAlign align);
    VerticalAlign GetVerticalAlign() const noexcept { return verticalAlign_; }
    void SetVerticalAlign(VerticalAlign align);
    WrapMode GetWrapMode() const noexcept { return wrap_; }
    void SetWrapMode(WrapMode wrap);
    float GetLineHeightMultiplier() const noexcept { return lineHeightMultiplier_; }
    void SetLineHeightMultiplier(float multiplier);
    float GetLetterSpacing() const noexcept { return letterSpacing_; }
    void SetLetterSpacing(float spacing);
    FacingMode GetFacingMode() const noexcept { return facing_; }
    void SetFacingMode(FacingMode facing);
    const MATH::Vec3& GetColor() const noexcept { return color_; }
    void SetColor(MATH::Vec3 color);
    const MATH::Vec3& GetOutlineColor() const noexcept { return outlineColor_; }
    void SetOutlineColor(MATH::Vec3 color);
    float GetOutlineWidth() const noexcept { return outlineWidth_; }
    void SetOutlineWidth(float width);
    float GetOpacity() const noexcept { return opacity_; }
    void SetOpacity(float opacity);
    float GetRevealAmount() const noexcept { return revealAmount_; }
    void SetRevealAmount(float amount);
    const MATH::Vec2& GetPivot() const noexcept { return pivot_; }
    void SetPivot(MATH::Vec2 pivot);
    int GetTransparentSortPriority() const noexcept { return transparentSortPriority_; }
    void SetTransparentSortPriority(int priority);

    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

private:
    void Sanitize() noexcept;
    void NotifyRenderStateDirty();

    TextStyleBinding binding_;
    std::string text_{"World Text"};
    std::string fontAssetPath_{"Fonts/NotoSansCJKjp-Regular.otf"};
    MATH::Vec2 layoutSize_{4.0f, 1.0f};
    MATH::Vec2 pivot_{0.5f, 0.5f};
    MATH::Vec3 color_{1.0f, 1.0f, 1.0f};
    MATH::Vec3 outlineColor_{};
    float fontHeight_ = 0.5f;
    float lineHeightMultiplier_ = 1.2f;
    float letterSpacing_ = 0.0f;
    float outlineWidth_ = 0.04f;
    float opacity_ = 1.0f;
    float revealAmount_ = 1.0f;
    int transparentSortPriority_ = 0;
    HorizontalAlign horizontalAlign_ = HorizontalAlign::Center;
    VerticalAlign verticalAlign_ = VerticalAlign::Center;
    WrapMode wrap_ = WrapMode::Character;
    FacingMode facing_ = FacingMode::CameraFacing;
    bool visible_ = true;

private:
    bool canvasSourceOnly_ = false;
};

} // namespace HIKARI
