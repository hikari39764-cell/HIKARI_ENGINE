#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Core/Text/HIKARI_EnumNames.h"

#include <utility>

#include "Core/Text/HIKARI_Utf8.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#include "Core/Text/HIKARI_Localization.h"
#endif

namespace HIKARI {

namespace {
constexpr size_t kMaximumFontAssetPathBytes = 1024u;
} // namespace

WorldTextSettings NormalizeWorldTextSettings(WorldTextSettings settings) {
    settings = JSONFIELDS::Sanitize(settings, kWorldTextFields);
    TEXT::TruncateUtf8(settings.text, TEXT_RENDERING::kSdfTextMaximumUtf8Bytes);
    TEXT::TruncateUtf8(settings.fontAssetPath, kMaximumFontAssetPathBytes);
    // 実行時の API は列挙値を数値のまま渡せるので、名前の表に無い値は既定へ戻す。
    if (!TEXT::IsNamedEnum(kWorldTextHorizontalAlignNames, settings.horizontalAlign)) {
        settings.horizontalAlign = TEXT_RENDERING::SdfTextHorizontalAlign::Left;
    }
    if (!TEXT::IsNamedEnum(kWorldTextVerticalAlignNames, settings.verticalAlign)) {
        settings.verticalAlign = TEXT_RENDERING::SdfTextVerticalAlign::Top;
    }
    if (!TEXT::IsNamedEnum(kWorldTextWrapNames, settings.wrap)) {
        settings.wrap = TEXT_RENDERING::SdfTextWrap::None;
    }
    if (!TEXT::IsNamedEnum(kWorldTextFacingNames, settings.facing)) {
        settings.facing = RENDER3D::WORLD_TEXT::WorldTextFacingMode::CameraFacing;
    }
    return settings;
}

TextStyleAsset WorldTextComponent::ResolveAppearance(const TextAssetStore* store) const {
    TextStyleAsset local;
    std::string error;
    const auto shared = store ? store->LoadStyle(settings_.style, error) : nullptr;
    local.pixelHeight = shared ? shared->pixelHeight : 48.f;
    const auto color = GetColor(), outline = GetOutlineColor();
    local.color = {color.x, color.y, color.z, 1};
    local.effects.lineHeight = GetLineHeightMultiplier();
    local.effects.letterSpacing = GetLetterSpacing() * local.pixelHeight / GetFontHeight();
    local.effects.outlineWidth = GetOutlineWidth() * local.pixelHeight;
    local.effects.outline = {outline.x, outline.y, outline.z, 1};
    local.effects.shadow.a = 0;
    local.effects.reveal = GetRevealAmount();
    local.effects.wrap = static_cast<GameProjectUiTextWrap>(GetWrapMode());
    TextStyleBinding binding = settings_;
    // World の寸法は fontHeight が所有し、共有スタイルの基準 pixel size は変更しない。
    binding.overrideSize = false;
    return ResolveTextStyle(store, binding, local);
}

void WorldTextComponent::Commit(WorldTextSettings next) {
    next = NormalizeWorldTextSettings(std::move(next));
    if (next == settings_) {
        return;
    }
    settings_ = std::move(next);
    NotifyRenderStateDirty();
}

void WorldTextComponent::SetText(std::string text) {
    WorldTextSettings next = settings_;
    next.text = std::move(text);
    Commit(std::move(next));
}

void WorldTextComponent::SetFontAssetPath(std::string path) {
    WorldTextSettings next = settings_;
    next.fontAssetPath = std::move(path);
    Commit(std::move(next));
}

void WorldTextComponent::SetVisible(bool visible) {
    WorldTextSettings next = settings_;
    next.visible = visible;
    Commit(std::move(next));
}

void WorldTextComponent::SetLayoutSize(MATH::Vec2 size) {
    WorldTextSettings next = settings_;
    next.layoutSize = size;
    Commit(std::move(next));
}

void WorldTextComponent::SetFontHeight(float height) {
    WorldTextSettings next = settings_;
    next.fontHeight = height;
    Commit(std::move(next));
}

void WorldTextComponent::SetHorizontalAlign(HorizontalAlign align) {
    WorldTextSettings next = settings_;
    next.horizontalAlign = align;
    Commit(std::move(next));
}

void WorldTextComponent::SetVerticalAlign(VerticalAlign align) {
    WorldTextSettings next = settings_;
    next.verticalAlign = align;
    Commit(std::move(next));
}

void WorldTextComponent::SetWrapMode(WrapMode wrap) {
    WorldTextSettings next = settings_;
    next.wrap = wrap;
    Commit(std::move(next));
}

void WorldTextComponent::SetLineHeightMultiplier(float multiplier) {
    WorldTextSettings next = settings_;
    next.lineHeightMultiplier = multiplier;
    Commit(std::move(next));
}

void WorldTextComponent::SetLetterSpacing(float spacing) {
    WorldTextSettings next = settings_;
    next.letterSpacing = spacing;
    Commit(std::move(next));
}

void WorldTextComponent::SetFacingMode(FacingMode facing) {
    WorldTextSettings next = settings_;
    next.facing = facing;
    Commit(std::move(next));
}

void WorldTextComponent::SetColor(MATH::Vec3 color) {
    WorldTextSettings next = settings_;
    next.color = color;
    Commit(std::move(next));
}

void WorldTextComponent::SetOutlineColor(MATH::Vec3 color) {
    WorldTextSettings next = settings_;
    next.outlineColor = color;
    Commit(std::move(next));
}

void WorldTextComponent::SetOutlineWidth(float width) {
    WorldTextSettings next = settings_;
    next.outlineWidth = width;
    Commit(std::move(next));
}

void WorldTextComponent::SetOpacity(float opacity) {
    WorldTextSettings next = settings_;
    next.opacity = opacity;
    Commit(std::move(next));
}

void WorldTextComponent::SetRevealAmount(float amount) {
    WorldTextSettings next = settings_;
    next.revealAmount = amount;
    Commit(std::move(next));
}

void WorldTextComponent::SetPivot(MATH::Vec2 pivot) {
    WorldTextSettings next = settings_;
    next.pivot = pivot;
    Commit(std::move(next));
}

void WorldTextComponent::SetTransparentSortPriority(int priority) {
    WorldTextSettings next = settings_;
    next.transparentSortPriority = priority;
    Commit(std::move(next));
}

bool WorldTextComponent::QueryGeometryFit(GeometryFitDesc& outFit) const noexcept {
    const MATH::Vec2 size = settings_.layoutSize;
    outFit = {};
    outFit.shape = GeometryFitShape::Box;
    outFit.center = {(0.5f - settings_.pivot.x) * size.x, (0.5f - settings_.pivot.y) * size.y, 0.0f};
    outFit.size = {size.x, size.y, 0.02f};
    outFit.radius = 0.01f;
    outFit.height = size.y;
    return true;
}

void WorldTextComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kWorldTextFields);
}

void WorldTextComponent::Deserialize(const nlohmann::json& in) {
    WorldTextSettings decoded{};
    JSONFIELDS::Read(in, decoded, kWorldTextFields);
    settings_ = NormalizeWorldTextSettings(std::move(decoded));
    NotifyRenderStateDirty();
}

#if defined(HIKARI_WITH_EDITOR)
void WorldTextComponent::BuildInspector(IInspectorBuilder& builder) {
    WorldTextSettings next = settings_;
    bool changed = InspectFields(builder, next, kWorldTextFields);
    if (next.style.IsValid() && builder.Button(TEXT::Localize("textstyle.restore"))) {
        next.overrideFont = next.overrideSize = next.overrideColor = next.overrideEffects = false;
        changed = true;
    }
    if (changed) {
        Commit(std::move(next));
    }
}
#endif

} // namespace HIKARI
