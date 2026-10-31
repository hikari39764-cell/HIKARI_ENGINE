#pragma once

#include <string>
#include <string_view>

#include "Assets/Text/HIKARI_TextStyleBinding.h"
#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"

namespace HIKARI {

// 保存する名前。
inline constexpr TEXT::EnumName<RENDER3D::WORLD_TEXT::WorldTextFacingMode> kWorldTextFacingNames[] = {
    {RENDER3D::WORLD_TEXT::WorldTextFacingMode::Fixed, "Fixed"},
    {RENDER3D::WORLD_TEXT::WorldTextFacingMode::CameraFacing, "CameraFacing"},
    {RENDER3D::WORLD_TEXT::WorldTextFacingMode::FixedYAxis, "FixedYAxis"},
};
inline constexpr TEXT::EnumName<TEXT_RENDERING::SdfTextHorizontalAlign> kWorldTextHorizontalAlignNames[] = {
    {TEXT_RENDERING::SdfTextHorizontalAlign::Left, "Left"},
    {TEXT_RENDERING::SdfTextHorizontalAlign::Center, "Center"},
    {TEXT_RENDERING::SdfTextHorizontalAlign::Right, "Right"},
};
inline constexpr TEXT::EnumName<TEXT_RENDERING::SdfTextVerticalAlign> kWorldTextVerticalAlignNames[] = {
    {TEXT_RENDERING::SdfTextVerticalAlign::Top, "Top"},
    {TEXT_RENDERING::SdfTextVerticalAlign::Center, "Center"},
    {TEXT_RENDERING::SdfTextVerticalAlign::Bottom, "Bottom"},
};
inline constexpr TEXT::EnumName<TEXT_RENDERING::SdfTextWrap> kWorldTextWrapNames[] = {
    {TEXT_RENDERING::SdfTextWrap::None, "None"},
    {TEXT_RENDERING::SdfTextWrap::Character, "Character"},
};

// 作者値（シーン文書の properties と同じ形）。項目表（kWorldTextFields）が保存・読み取り・検査器・説明を決める。文字
// スタイルの結び付き（TextStyleBinding）は同じ階層のキーになる。
struct WorldTextSettings : TextStyleBinding {
    std::string text{"World Text"};
    std::string fontAssetPath{"Fonts/NotoSansCJKjp-Regular.otf"};
    bool visible = true;
    MATH::Vec2 layoutSize{4.0f, 1.0f};
    float fontHeight = 0.5f;
    TEXT_RENDERING::SdfTextHorizontalAlign horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Center;
    TEXT_RENDERING::SdfTextVerticalAlign verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Center;
    TEXT_RENDERING::SdfTextWrap wrap = TEXT_RENDERING::SdfTextWrap::Character;
    float lineHeightMultiplier = 1.2f;
    float letterSpacing = 0.0f;
    RENDER3D::WORLD_TEXT::WorldTextFacingMode facing = RENDER3D::WORLD_TEXT::WorldTextFacingMode::CameraFacing;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    MATH::Vec3 outlineColor{};
    float outlineWidth = 0.04f;
    float opacity = 1.0f;
    float revealAmount = 1.0f;
    MATH::Vec2 pivot{0.5f, 0.5f};
    int transparentSortPriority = 0;

    bool operator==(const WorldTextSettings&) const = default;
};

inline constexpr auto kWorldTextFields = [](auto&& field) {
    using S = WorldTextSettings;
    using namespace JSONFIELDS;
    // 共有スタイルを使う間、スタイルが決める項目は出さない（上書きを選んだ時だけ出す）。
    constexpr auto ownsEffects = [](const S& s) { return !s.style.IsValid() || s.overrideEffects; };
    constexpr auto ownsColor = [](const S& s) { return !s.style.IsValid() || s.overrideColor; };

    field("text", &S::text, Label{"Text"}, Help{"UTF-8 text displayed in world space."},
          Draw{FIELD_DRAW::MultilineText{}}, Common{});
    field("visible", &S::visible, Label{"Visible"}, Help{"Controls whether this World Text is rendered."}, Common{});
    kTextStyleBindingFields<false>(field);

    constexpr const char* kLayout = "Layout";
    field("layoutSize", &S::layoutSize, Range{0.001, 100000.0}, Label{"Layout Size"},
          Help{"Text layout width and height in world units."}, Group{kLayout});
    field("fontHeight", &S::fontHeight, Range{0.001, 10000.0}, Label{"Font Height"},
          Help{"Glyph height in world units."}, Group{kLayout});
    field("horizontalAlign", &S::horizontalAlign, kWorldTextHorizontalAlignNames, Label{"Horizontal Alignment"},
          Help{"Aligns each laid-out line within the layout width."}, Group{kLayout});
    field("verticalAlign", &S::verticalAlign, kWorldTextVerticalAlignNames, Label{"Vertical Alignment"},
          Help{"Aligns the text block within the layout height."}, Group{kLayout});
    field("wrap", &S::wrap, kWorldTextWrapNames, Label{"Wrapping"},
          Help{"Controls whether text wraps at UTF-8 character boundaries."}, Group{kLayout});
    field("lineHeightMultiplier", &S::lineHeightMultiplier, Range{0.25, 4.0}, Label{"Line Height Multiplier"},
          Help{"Spacing between text baselines relative to font height."}, Show{ownsEffects}, Group{kLayout});
    field("letterSpacing", &S::letterSpacing, Range{-10000.0, 10000.0}, Label{"Letter Spacing"},
          Help{"Additional glyph advance in world units."}, Show{ownsEffects}, Group{kLayout});

    constexpr const char* kAppearance = "Appearance";
    field("color", &S::color, Range{0.0, 16.0}, Label{"Color"}, Color{}, Help{"Linear color of the text face."},
          Show{ownsColor}, Group{kAppearance});
    field("outlineColor", &S::outlineColor, Range{0.0, 16.0}, Label{"Outline Color"}, Color{},
          Help{"Linear color of the SDF outline."}, Show{ownsEffects}, Group{kAppearance});
    field("outlineWidth", &S::outlineWidth, Range{0.0, 0.25}, Label{"Outline Width"},
          Help{"SDF outline thickness in normalized distance-field units."}, Show{ownsEffects}, Group{kAppearance});
    field("opacity", &S::opacity, Range{0.0, 1.0}, Label{"Opacity"}, Help{"Final text and outline opacity."},
          Group{kAppearance});
    field("revealAmount", &S::revealAmount, Range{0.0, 1.0}, Label{"Reveal Amount"},
          Help{"Fraction of laid-out glyphs revealed, from 0 to 1."}, Group{kAppearance});
    field("facing", &S::facing, kWorldTextFacingNames, Label{"Facing"},
          Help{"Controls whether text uses object rotation or faces each render view."}, Group{kAppearance});

    field("pivot", &S::pivot, Range{0.0, 1.0}, Label{"Pivot"},
          Help{"Placement pivot in normalized layout coordinates."}, Advanced{});
    field("transparentSortPriority", &S::transparentSortPriority, Range{-32768.0, 32767.0},
          Label{"Transparent Sort Priority"}, Help{"Higher transparent priorities are drawn later."}, Advanced{});
    field("fontAssetPath", &S::fontAssetPath, Label{"Font Asset Path"},
          Help{"Project-relative font file used when no font asset is assigned."}, Advanced{});
};

// UTF-8 の長さを切り詰め、範囲へ丸め、範囲外の列挙値を既定へ戻す。
WorldTextSettings NormalizeWorldTextSettings(WorldTextSettings settings);

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

    const std::string& GetText() const noexcept { return settings_.text; }
    void SetText(std::string text);
    TextStyleAsset ResolveAppearance(const TextAssetStore* store) const;
    const TextStyleBinding& GetStyleBinding() const noexcept { return settings_; }
    const std::string& GetFontAssetPath() const noexcept { return settings_.fontAssetPath; }
    void SetFontAssetPath(std::string path);
    bool IsVisible() const noexcept { return settings_.visible; }
    void SetVisible(bool visible);
    const MATH::Vec2& GetLayoutSize() const noexcept { return settings_.layoutSize; }
    void SetLayoutSize(MATH::Vec2 size);
    float GetFontHeight() const noexcept { return settings_.fontHeight; }
    void SetFontHeight(float height);
    HorizontalAlign GetHorizontalAlign() const noexcept { return settings_.horizontalAlign; }
    void SetHorizontalAlign(HorizontalAlign align);
    VerticalAlign GetVerticalAlign() const noexcept { return settings_.verticalAlign; }
    void SetVerticalAlign(VerticalAlign align);
    WrapMode GetWrapMode() const noexcept { return settings_.wrap; }
    void SetWrapMode(WrapMode wrap);
    float GetLineHeightMultiplier() const noexcept { return settings_.lineHeightMultiplier; }
    void SetLineHeightMultiplier(float multiplier);
    float GetLetterSpacing() const noexcept { return settings_.letterSpacing; }
    void SetLetterSpacing(float spacing);
    FacingMode GetFacingMode() const noexcept { return settings_.facing; }
    void SetFacingMode(FacingMode facing);
    const MATH::Vec3& GetColor() const noexcept { return settings_.color; }
    void SetColor(MATH::Vec3 color);
    const MATH::Vec3& GetOutlineColor() const noexcept { return settings_.outlineColor; }
    void SetOutlineColor(MATH::Vec3 color);
    float GetOutlineWidth() const noexcept { return settings_.outlineWidth; }
    void SetOutlineWidth(float width);
    float GetOpacity() const noexcept { return settings_.opacity; }
    void SetOpacity(float opacity);
    float GetRevealAmount() const noexcept { return settings_.revealAmount; }
    void SetRevealAmount(float amount);
    const MATH::Vec2& GetPivot() const noexcept { return settings_.pivot; }
    void SetPivot(MATH::Vec2 pivot);
    int GetTransparentSortPriority() const noexcept { return settings_.transparentSortPriority; }
    void SetTransparentSortPriority(int priority);

    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override;
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

private:
    // 丸めて置き換え、値が変わった時だけ描画の更新を知らせる。
    void Commit(WorldTextSettings next);

    WorldTextSettings settings_{};
    bool canvasSourceOnly_ = false;
};

} // namespace HIKARI
