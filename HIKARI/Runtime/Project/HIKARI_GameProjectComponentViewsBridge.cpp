#include "HIKARI/Project/ComponentViews.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "HIKARI/Project/WorldView.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectComponentAccess.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"

namespace HIKARI {

using Code = GameProjectWorldResultCode;
using PROJECT::INTERNAL::MutateComponent;
using PROJECT::INTERNAL::ReadComponent;

namespace {

GameProjectLightType ToProjectLightType(RENDER3D::LIGHTING::SceneLightType type) noexcept {
    switch (type) {
    case RENDER3D::LIGHTING::SceneLightType::Spot: return GameProjectLightType::Spot;
    case RENDER3D::LIGHTING::SceneLightType::Tube: return GameProjectLightType::Tube;
    case RENDER3D::LIGHTING::SceneLightType::Rect: return GameProjectLightType::Rect;
    case RENDER3D::LIGHTING::SceneLightType::Point: break;
    }
    return GameProjectLightType::Point;
}

bool Finite(GameProjectFloat3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsValidNodeName(std::string_view nodeName) noexcept {
    return !nodeName.empty() && nodeName.size() <= kGameProjectModelNodeNameMaximumBytes;
}

} // namespace

static_assert(static_cast<uint32_t>(GameProjectWorldSpriteFacingMode::FixedYAxis) ==
              static_cast<uint8_t>(WorldSpriteComponent::FacingMode::FixedYAxis));
static_assert(static_cast<uint32_t>(GameProjectWorldSpriteAlphaMode::Blend) ==
              static_cast<uint8_t>(WorldSpriteComponent::AlphaMode::Blend));

LightView ObjectView::GetLight() const noexcept { return LightView(handle_); }
DecalView ObjectView::GetDecal() const noexcept { return DecalView(handle_); }
ModelView ObjectView::GetModel() const noexcept { return ModelView(handle_); }
WorldSpriteView ObjectView::GetWorldSprite() const noexcept { return WorldSpriteView(handle_); }
WorldTextView ObjectView::GetWorldText() const noexcept { return WorldTextView(handle_); }

// ---- Light ----

bool LightView::TryGetState(GameProjectLightState& outState) const noexcept {
    const uint32_t bytes = outState.structSize;
    const uint32_t version = outState.version;
    if (version != kGameProjectLightStateVersion || bytes != kGameProjectLightStateV4Size) {
        return false;
    }
    GameProjectLightState snapshot{};
    snapshot.structSize = bytes;
    snapshot.version = version;
    // 失敗時も一貫した空の値を返す。
    std::memcpy(&outState, &snapshot, bytes);
    return ReadComponent<LightComponent>(object_, [&](const LightComponent& light) {
        const MATH::Vec3& color = light.GetColor();
        snapshot.type = ToProjectLightType(light.GetLightType());
        snapshot.enabled = light.IsEnabled() ? 1u : 0u;
        snapshot.color = {color.x, color.y, color.z};
        snapshot.intensity = light.GetIntensity();
        snapshot.range = light.GetRange();
        snapshot.innerConeAngleDegrees = light.GetInnerConeAngleDegrees();
        snapshot.outerConeAngleDegrees = light.GetOuterConeAngleDegrees();
        snapshot.angularFalloff = light.GetAngularFalloff();
        snapshot.priority = light.GetPriority();
        snapshot.stateRevision = light.GetStateRevision();
        snapshot.sourceRadius = light.GetSourceRadius();
        snapshot.castsShadows = light.CastsShadows() ? 1u : 0u;
        const LightSettings& settings = light.GetSettings();
        snapshot.length = settings.length;
        snapshot.height = settings.height;
        PROJECT::DETAIL::WriteAssetGuidText(settings.iesProfile, snapshot.iesProfileAssetGuid);
        PROJECT::DETAIL::WriteAssetGuidText(settings.cookie, snapshot.cookieAssetGuid);
        std::memcpy(&outState, &snapshot, bytes);
        return true;
    });
}

GameProjectWorldResult LightView::ApplyPatch(const GameProjectLightPatch& patch) const noexcept {
    return MutateComponent<LightComponent>(object_, [&](LightComponent& light) {
        using Field = GameProjectLightPatchField;
        using RENDER3D::LIGHTING::SceneLightType;
        const uint32_t mask = patch.fieldMask;
        const auto Selected = [mask](Field field) { return (mask & GameProjectLightPatchFields(field)) != 0u; };
        std::string iesProfile;
        std::string cookie;
        if (patch.structSize != kGameProjectLightPatchV3Size || patch.version != kGameProjectLightPatchVersion ||
            mask == 0u || (mask & ~kGameProjectLightPatchKnownFields) != 0u || NUMERIC::AnyNonZero(patch.reserved0) ||
            patch.reserved1 != 0u || patch.reserved2 != 0u || NUMERIC::AnyNonZero(patch.reserved3) ||
            (Selected(Field::Enabled) && patch.enabled > 1u) || (Selected(Field::Color) && !Finite(patch.color)) ||
            (Selected(Field::Intensity) && !std::isfinite(patch.intensity)) ||
            (Selected(Field::Range) && !std::isfinite(patch.range)) ||
            (Selected(Field::SpotCone) &&
             (!std::isfinite(patch.innerConeAngleDegrees) || !std::isfinite(patch.outerConeAngleDegrees))) ||
            (Selected(Field::AngularFalloff) && !std::isfinite(patch.angularFalloff)) ||
            (Selected(Field::CastShadows) && patch.castsShadows > 1u) ||
            (Selected(Field::SourceRadius) && !std::isfinite(patch.sourceRadius)) ||
            (Selected(Field::AreaExtent) && (!std::isfinite(patch.length) || !std::isfinite(patch.height))) ||
            (Selected(Field::IesProfile) &&
             !PROJECT::DETAIL::ReadAssetGuidText(patch.iesProfileAssetGuid, iesProfile)) ||
            (Selected(Field::Cookie) && !PROJECT::DETAIL::ReadAssetGuidText(patch.cookieAssetGuid, cookie))) {
            return Code::InvalidRequest;
        }
        const SceneLightType type = light.GetLightType();
        const bool spot = type == SceneLightType::Spot;
        const bool area = type == SceneLightType::Tube || type == SceneLightType::Rect;
        if (((Selected(Field::SpotCone) || Selected(Field::AngularFalloff) || Selected(Field::Cookie)) && !spot) ||
            (Selected(Field::AreaExtent) && !area) || (Selected(Field::IesProfile) && area)) {
            return Code::Unsupported;
        }
        // 範囲への丸めと円錐角の順序は作者値と同じ NormalizeLightSettings が行う。
        LightSettings next = light.GetSettings();
        if (Selected(Field::Enabled)) {
            next.enabled = patch.enabled != 0u;
        }
        if (Selected(Field::Color)) {
            next.color = {patch.color.x, patch.color.y, patch.color.z};
        }
        if (Selected(Field::Intensity)) {
            SetLightBrightness(next, patch.intensity);
        }
        if (Selected(Field::Range)) {
            next.range = patch.range;
        }
        if (Selected(Field::SpotCone)) {
            next.innerConeAngleDegrees = patch.innerConeAngleDegrees;
            next.outerConeAngleDegrees = patch.outerConeAngleDegrees;
        }
        if (Selected(Field::AngularFalloff)) {
            next.angularFalloff = patch.angularFalloff;
        }
        if (Selected(Field::CastShadows)) {
            next.castsShadows = patch.castsShadows != 0u;
        }
        if (Selected(Field::SourceRadius)) {
            next.sourceRadius = patch.sourceRadius;
        }
        if (Selected(Field::AreaExtent)) {
            next.length = patch.length;
            next.height = patch.height;
        }
        if (Selected(Field::IesProfile)) {
            next.iesProfile = std::move(iesProfile);
        }
        if (Selected(Field::Cookie)) {
            next.cookie = std::move(cookie);
        }
        light.SetSettings(std::move(next));
        return Code::Success;
    });
}

// ---- Decal ----

bool DecalView::TryGetState(GameProjectDecalState& outState) const noexcept {
    if (outState.version != kGameProjectDecalStateVersion || outState.structSize != kGameProjectDecalStateV1Size) {
        return false;
    }
    outState = GameProjectDecalState{};
    return ReadComponent<DecalComponent>(object_, [&](const DecalComponent& decal) {
        const RENDER3D::DECAL::DecalSettings& settings = decal.GetSettings();
        GameProjectDecalState state{};
        state.enabled = settings.enabled ? 1u : 0u;
        state.size = {settings.size.x, settings.size.y, settings.size.z};
        state.color = {settings.color.x, settings.color.y, settings.color.z};
        state.opacity = settings.opacity;
        state.colorWeight = settings.colorWeight;
        state.normalWeight = settings.normalWeight;
        state.materialWeight = settings.materialWeight;
        state.roughness = settings.roughness;
        state.metallic = settings.metallic;
        state.angleFadeDegrees = settings.angleFadeDegrees;
        state.sortOrder = settings.sortOrder;
        PROJECT::DETAIL::WriteAssetGuidText(settings.baseColorTexture, state.baseColorTextureAssetGuid);
        PROJECT::DETAIL::WriteAssetGuidText(settings.normalTexture, state.normalTextureAssetGuid);
        outState = state;
        return true;
    });
}

GameProjectWorldResult DecalView::ApplyPatch(const GameProjectDecalPatch& patch) const noexcept {
    return MutateComponent<DecalComponent>(object_, [&](DecalComponent& decal) {
        using Field = GameProjectDecalPatchField;
        const uint32_t mask = patch.fieldMask;
        const auto Selected = [mask](Field field) { return (mask & GameProjectDecalPatchFields(field)) != 0u; };
        std::string baseColorTexture;
        std::string normalTexture;
        if (patch.structSize != kGameProjectDecalPatchV1Size || patch.version != kGameProjectDecalPatchVersion ||
            mask == 0u || (mask & ~kGameProjectDecalPatchKnownFields) != 0u || NUMERIC::AnyNonZero(patch.reserved0) ||
            NUMERIC::AnyNonZero(patch.reserved1) || (Selected(Field::Enabled) && patch.enabled > 1u) ||
            (Selected(Field::Size) && !Finite(patch.size)) || (Selected(Field::Color) && !Finite(patch.color)) ||
            (Selected(Field::Opacity) && !std::isfinite(patch.opacity)) ||
            (Selected(Field::Weights) && (!std::isfinite(patch.colorWeight) || !std::isfinite(patch.normalWeight) ||
                                          !std::isfinite(patch.materialWeight))) ||
            (Selected(Field::Material) && (!std::isfinite(patch.roughness) || !std::isfinite(patch.metallic))) ||
            (Selected(Field::AngleFade) && !std::isfinite(patch.angleFadeDegrees)) ||
            (Selected(Field::BaseColorTexture) &&
             !PROJECT::DETAIL::ReadAssetGuidText(patch.baseColorTextureAssetGuid, baseColorTexture)) ||
            (Selected(Field::NormalTexture) &&
             !PROJECT::DETAIL::ReadAssetGuidText(patch.normalTextureAssetGuid, normalTexture))) {
            return Code::InvalidRequest;
        }
        // 範囲への丸めは作者値と同じ NormalizeDecalSettings が行う。
        RENDER3D::DECAL::DecalSettings next = decal.GetSettings();
        if (Selected(Field::Enabled)) {
            next.enabled = patch.enabled != 0u;
        }
        if (Selected(Field::Size)) {
            next.size = {patch.size.x, patch.size.y, patch.size.z};
        }
        if (Selected(Field::BaseColorTexture)) {
            next.baseColorTexture = std::move(baseColorTexture);
        }
        if (Selected(Field::NormalTexture)) {
            next.normalTexture = std::move(normalTexture);
        }
        if (Selected(Field::Color)) {
            next.color = {patch.color.x, patch.color.y, patch.color.z};
        }
        if (Selected(Field::Opacity)) {
            next.opacity = patch.opacity;
        }
        if (Selected(Field::Weights)) {
            next.colorWeight = patch.colorWeight;
            next.normalWeight = patch.normalWeight;
            next.materialWeight = patch.materialWeight;
        }
        if (Selected(Field::Material)) {
            next.roughness = patch.roughness;
            next.metallic = patch.metallic;
        }
        if (Selected(Field::AngleFade)) {
            next.angleFadeDegrees = patch.angleFadeDegrees;
        }
        if (Selected(Field::SortOrder)) {
            next.sortOrder = patch.sortOrder;
        }
        decal.SetSettings(next);
        return Code::Success;
    });
}

// ---- Model ----

bool ModelView::TryGetAppearance(GameProjectModelAppearance& outAppearance) const noexcept {
    outAppearance = {};
    return ReadComponent<ModelComponent>(object_, [&](const ModelComponent& model) {
        const std::string& asset = model.GetAssetId();
        const std::string& material = model.GetMaterialSetAssetGuid().value;
        if (asset.size() >= sizeof(outAppearance.assetId) ||
            material.size() >= sizeof(outAppearance.materialSetAssetGuid)) {
            return false;
        }
        std::copy(asset.begin(), asset.end(), outAppearance.assetId);
        std::copy(material.begin(), material.end(), outAppearance.materialSetAssetGuid);
        return true;
    });
}

GameProjectWorldResult ModelView::ApplyAppearance(const GameProjectModelAppearance& appearance) const noexcept {
    return MutateComponent<ModelComponent>(object_, [&](ModelComponent& model) {
        const auto* assetEnd = std::find(std::begin(appearance.assetId), std::end(appearance.assetId), '\0');
        const auto* materialEnd =
            std::find(std::begin(appearance.materialSetAssetGuid), std::end(appearance.materialSetAssetGuid), '\0');
        if (appearance.structSize != sizeof(appearance) || appearance.version != 1 ||
            assetEnd == std::end(appearance.assetId) || assetEnd == appearance.assetId ||
            materialEnd == std::end(appearance.materialSetAssetGuid)) {
            return Code::InvalidRequest;
        }
        std::string asset(appearance.assetId, assetEnd);
        std::string material(appearance.materialSetAssetGuid, materialEnd);
        if (model.GetAssetId() != asset) {
            model.SetAssetId(std::move(asset));
        }
        if (model.GetMaterialSetAssetGuid().value != material) {
            model.SetMaterialSetAssetGuid(AssetGuid{std::move(material)});
        }
        return Code::Success;
    });
}

GameProjectWorldResult ModelView::SetMaterialColorOverride(uint32_t materialIndex, GameProjectFloat3 baseColor,
                                                           float opacity, GameProjectFloat3 emissiveColor,
                                                           float emissiveStrength) const noexcept {
    return MutateComponent<ModelComponent>(object_, [&](ModelComponent& model) {
        if (!Finite(baseColor) || !std::isfinite(opacity) || !Finite(emissiveColor) ||
            !std::isfinite(emissiveStrength)) {
            return Code::InvalidRequest;
        }
        Material* material = model.GetRuntimeMaterialOverride(materialIndex);
        if (material == nullptr) {
            return Code::Unsupported;
        }
        const uint64_t revisionBefore = material->GetRevision();
        const auto Color = [](float value) { return std::clamp(value, 0.0f, 64.0f); };
        material->SetBaseColor(
            {Color(baseColor.x), Color(baseColor.y), Color(baseColor.z), std::clamp(opacity, 0.0f, 1.0f)});
        material->SetEmissiveFactor({Color(emissiveColor.x), Color(emissiveColor.y), Color(emissiveColor.z)});
        material->SetEmissiveStrength(std::clamp(emissiveStrength, 0.0f, 64.0f));
        GameObject* owner = model.GetOwner();
        if (material->GetRevision() != revisionBefore && owner != nullptr && owner->GetWorld() != nullptr) {
            owner->GetWorld()->MarkRenderObjectDirty(owner);
        }
        return Code::Success;
    });
}

GameProjectWorldResult ModelView::SetNodeVisibility(std::string_view nodeName, bool visible,
                                                    GameProjectModelNodeVisibilityScope scope) const noexcept {
    return MutateComponent<ModelComponent>(object_, [&](ModelComponent& model) {
        if (!IsValidNodeName(nodeName) || scope >= GameProjectModelNodeVisibilityScope::Count) {
            return Code::InvalidRequest;
        }
        return model.SetRuntimeNodeVisibilityOverride(nodeName, visible,
                                                      scope == GameProjectModelNodeVisibilityScope::NodeAndDescendants)
                   ? Code::Success
                   : Code::MutationRejected;
    });
}

GameProjectWorldResult ModelView::ClearNodeVisibility(std::string_view nodeName) const noexcept {
    return MutateComponent<ModelComponent>(object_, [&](ModelComponent& model) {
        if (!IsValidNodeName(nodeName)) {
            return Code::InvalidRequest;
        }
        (void)model.ClearRuntimeNodeVisibilityOverride(nodeName);
        return Code::Success;
    });
}

GameProjectWorldResult ModelView::ClearAllNodeVisibility() const noexcept {
    return MutateComponent<ModelComponent>(object_, [](ModelComponent& model) {
        model.ClearRuntimeNodeVisibilityOverrides();
        return Code::Success;
    });
}

GameProjectWorldResult ModelView::SetPostGroupMask(uint32_t mask) const noexcept {
    return MutateComponent<ModelComponent>(object_, [mask](ModelComponent& model) {
        return model.SetRuntimePostGroupMask(mask) ? Code::Success : Code::InvalidRequest;
    });
}

GameProjectWorldResult ModelView::ClearPostGroupMask() const noexcept {
    return MutateComponent<ModelComponent>(object_, [](ModelComponent& model) {
        model.ClearRuntimePostGroupMask();
        return Code::Success;
    });
}

// ---- WorldSprite ----

bool WorldSpriteView::TryGetState(GameProjectWorldSpriteState& outState) const noexcept {
    outState = {};
    return ReadComponent<WorldSpriteComponent>(object_, [&](const WorldSpriteComponent& sprite) {
        const MATH::Vec2& size = sprite.GetSize();
        const MATH::Vec3& tint = sprite.GetTint();
        const MATH::Vec2 pivot = sprite.GetEffectivePivot();
        outState.stateRevision = sprite.GetStateRevision();
        outState.size = {size.x, size.y};
        outState.tint = {tint.x, tint.y, tint.z};
        outState.opacity = sprite.GetOpacity();
        outState.pivot = {pivot.x, pivot.y};
        outState.alphaCutoff = sprite.GetAlphaCutoff();
        outState.transparentSortPriority = sprite.GetTransparentSortPriority();
        outState.facing = static_cast<GameProjectWorldSpriteFacingMode>(sprite.GetFacingMode());
        outState.alphaMode = static_cast<GameProjectWorldSpriteAlphaMode>(sprite.GetAlphaMode());
        outState.visible = sprite.IsVisible() ? 1u : 0u;
        outState.preserveAspect = sprite.GetPreserveAspect() ? 1u : 0u;
        outState.flipX = sprite.GetFlipX() ? 1u : 0u;
        return true;
    });
}

GameProjectWorldResult WorldSpriteView::SetTexture(std::string_view textureAssetGuid) const noexcept {
    return MutateComponent<WorldSpriteComponent>(object_, [&](WorldSpriteComponent& sprite) {
        if (textureAssetGuid.size() > 128u || textureAssetGuid.find('\0') != std::string_view::npos) {
            return Code::InvalidRequest;
        }
        sprite.SetTextureAssetGuid(AssetGuid{std::string(textureAssetGuid)});
        return Code::Success;
    });
}

GameProjectWorldResult WorldSpriteView::ApplyPatch(const GameProjectWorldSpritePatch& patch) const noexcept {
    return MutateComponent<WorldSpriteComponent>(object_, [&](WorldSpriteComponent& sprite) {
        const uint32_t mask = patch.fieldMask;
        const auto Selected = [mask](GameProjectWorldSpritePatchField field) noexcept {
            return (mask & GameProjectWorldSpritePatchFields(field)) != 0u;
        };
        if (patch.structSize != kGameProjectWorldSpritePatchV1Size ||
            patch.version != kGameProjectWorldSpritePatchVersion || mask == 0u ||
            (mask & ~kGameProjectWorldSpritePatchKnownFields) != 0u || patch.reserved0 != 0u ||
            NUMERIC::AnyNonZero(patch.reserved1) ||
            (Selected(GameProjectWorldSpritePatchField::Visible) && patch.visible > 1u) ||
            (Selected(GameProjectWorldSpritePatchField::PreserveAspect) && patch.preserveAspect > 1u) ||
            (Selected(GameProjectWorldSpritePatchField::Facing) &&
             patch.facing >= GameProjectWorldSpriteFacingMode::Count) ||
            (Selected(GameProjectWorldSpritePatchField::AlphaMode) &&
             patch.alphaMode >= GameProjectWorldSpriteAlphaMode::Count) ||
            (Selected(GameProjectWorldSpritePatchField::FlipX) && patch.flipX > 1u) ||
            (Selected(GameProjectWorldSpritePatchField::Size) &&
             (!std::isfinite(patch.size.x) || !std::isfinite(patch.size.y))) ||
            (Selected(GameProjectWorldSpritePatchField::Tint) && !Finite(patch.tint)) ||
            (Selected(GameProjectWorldSpritePatchField::Opacity) && !std::isfinite(patch.opacity)) ||
            (Selected(GameProjectWorldSpritePatchField::Pivot) &&
             (!std::isfinite(patch.pivot.x) || !std::isfinite(patch.pivot.y))) ||
            (Selected(GameProjectWorldSpritePatchField::AlphaCutoff) && !std::isfinite(patch.alphaCutoff))) {
            return Code::InvalidRequest;
        }
        if (sprite.IsCanvasSourceOnly() && Selected(GameProjectWorldSpritePatchField::FlipX)) {
            return Code::Unsupported;
        }
        WorldSpriteComponent::RuntimeState next = sprite.GetRuntimeState();
        if (Selected(GameProjectWorldSpritePatchField::Visible)) {
            next.visible = patch.visible != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::FlipX)) {
            next.flipX = patch.flipX != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::Size)) {
            next.size = {patch.size.x, patch.size.y};
            next.sizeExplicit = true;
        }
        if (Selected(GameProjectWorldSpritePatchField::PreserveAspect)) {
            next.preserveAspect = patch.preserveAspect != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::Facing)) {
            next.facing = static_cast<WorldSpriteComponent::FacingMode>(patch.facing);
        }
        if (Selected(GameProjectWorldSpritePatchField::Tint)) {
            next.tint = {patch.tint.x, patch.tint.y, patch.tint.z};
        }
        if (Selected(GameProjectWorldSpritePatchField::Opacity)) {
            next.opacity = patch.opacity;
        }
        if (Selected(GameProjectWorldSpritePatchField::Pivot)) {
            next.pivot = {patch.pivot.x, patch.pivot.y};
            next.pivotExplicit = true;
        }
        if (Selected(GameProjectWorldSpritePatchField::AlphaMode)) {
            next.alphaMode = static_cast<WorldSpriteComponent::AlphaMode>(patch.alphaMode);
        }
        if (Selected(GameProjectWorldSpritePatchField::AlphaCutoff)) {
            next.alphaCutoff = patch.alphaCutoff;
        }
        if (Selected(GameProjectWorldSpritePatchField::TransparentSortPriority)) {
            next.transparentSortPriority = patch.transparentSortPriority;
        }
        (void)sprite.ApplyRuntimeState(next);
        return Code::Success;
    });
}

// ---- WorldText ----

bool WorldTextView::TryGetState(GameProjectWorldTextState& outState) const noexcept {
    outState = {};
    return ReadComponent<WorldTextComponent>(object_, [&](const WorldTextComponent& text) {
        const auto color = text.GetColor();
        const auto outline = text.GetOutlineColor();
        const auto size = text.GetLayoutSize();
        outState.color = {color.x, color.y, color.z};
        outState.outlineColor = {outline.x, outline.y, outline.z};
        outState.layoutSize = {size.x, size.y};
        outState.fontHeight = text.GetFontHeight();
        outState.outlineWidth = text.GetOutlineWidth();
        outState.opacity = text.GetOpacity();
        outState.revealAmount = text.GetRevealAmount();
        outState.visible = text.IsVisible() ? 1u : 0u;
        return true;
    });
}

GameProjectWorldResult WorldTextView::ApplyPatch(const GameProjectWorldTextPatch& patch) const noexcept {
    return MutateComponent<WorldTextComponent>(object_, [&](WorldTextComponent& text) {
        const auto& v = patch.values;
        using F = GameProjectWorldTextPatchField;
        const auto has = [&](F field) { return (patch.fieldMask & GameProjectWorldTextPatchFields(field)) != 0u; };
        if (patch.structSize != sizeof(patch) || patch.version != 1u || patch.reserved || v.structSize != sizeof(v) ||
            v.version != 1u || v.reserved || !patch.fieldMask ||
            (patch.fieldMask & ~kGameProjectWorldTextPatchKnownFields) || (has(F::Visible) && v.visible > 1u) ||
            (has(F::Color) && !Finite(v.color)) || (has(F::OutlineColor) && !Finite(v.outlineColor)) ||
            (has(F::Opacity) && !std::isfinite(v.opacity)) ||
            (has(F::RevealAmount) && !std::isfinite(v.revealAmount)) ||
            (has(F::OutlineWidth) && !std::isfinite(v.outlineWidth)) ||
            (has(F::FontHeight) && (!std::isfinite(v.fontHeight) || v.fontHeight <= 0.0f)) ||
            (has(F::LayoutSize) && (!std::isfinite(v.layoutSize.x) || !std::isfinite(v.layoutSize.y) ||
                                    v.layoutSize.x <= 0.0f || v.layoutSize.y <= 0.0f))) {
            return Code::InvalidRequest;
        }
        if (has(F::Visible)) {
            text.SetVisible(v.visible != 0u);
        }
        if (has(F::Color)) {
            text.SetColor({v.color.x, v.color.y, v.color.z});
        }
        if (has(F::OutlineColor)) {
            text.SetOutlineColor({v.outlineColor.x, v.outlineColor.y, v.outlineColor.z});
        }
        if (has(F::OutlineWidth)) {
            text.SetOutlineWidth(v.outlineWidth);
        }
        if (has(F::Opacity)) {
            text.SetOpacity(v.opacity);
        }
        if (has(F::RevealAmount)) {
            text.SetRevealAmount(v.revealAmount);
        }
        if (has(F::LayoutSize)) {
            text.SetLayoutSize({v.layoutSize.x, v.layoutSize.y});
        }
        if (has(F::FontHeight)) {
            text.SetFontHeight(v.fontHeight);
        }
        return Code::Success;
    });
}

GameProjectWorldResult WorldTextView::SetContent(std::string_view value,
                                                 std::string_view fontAssetPath) const noexcept {
    return MutateComponent<WorldTextComponent>(object_, [&](WorldTextComponent& text) {
        if (value.size() > kGameProjectWorldTextMaximumBytes || fontAssetPath.size() > 4096u ||
            value.find('\0') != std::string_view::npos || fontAssetPath.find('\0') != std::string_view::npos) {
            return Code::InvalidRequest;
        }
        if (text.GetText() == value && (fontAssetPath.empty() || text.GetFontAssetPath() == fontAssetPath)) {
            return Code::Success;
        }
        // 文字列確保が失敗しても一部だけ更新しない。
        std::string content(value);
        std::string font(fontAssetPath);
        text.SetText(std::move(content));
        if (!font.empty()) {
            text.SetFontAssetPath(std::move(font));
        }
        return Code::Success;
    });
}

} // namespace HIKARI
