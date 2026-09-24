#include "HIKARI_WorldSpriteComponent.h"

namespace HIKARI {
void WorldSpriteComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 3u},
           {"lit", lit_},
           {"roughness", roughness_},
           {"specular", specular_},
           {"castShadow", castShadow_},
           {"receiveShadow", receiveShadow_},
           {"pointSampling", pointSampling_},
           {"atlasColumns", atlasColumns_},
           {"atlasRows", atlasRows_},
           {"atlasFrames", atlasFrames_},
           {"framesPerSecond", framesPerSecond_},
           {WorldSpriteComponentPropertyIds::TextureAssetGuid, textureAssetGuid_.value},
           {WorldSpriteComponentPropertyIds::Visible, visible_},
           {WorldSpriteComponentPropertyIds::Size, {size_.x, size_.y}},
           {WorldSpriteComponentPropertyIds::PreserveAspect, preserveAspect_},
           {WorldSpriteComponentPropertyIds::Facing, FacingName(facing_)},
           {WorldSpriteComponentPropertyIds::Tint, {tint_.x, tint_.y, tint_.z}},
           {WorldSpriteComponentPropertyIds::Opacity, opacity_},
           {WorldSpriteComponentPropertyIds::Pivot, {pivot_.x, pivot_.y}},
           {WorldSpriteComponentPropertyIds::AlphaMode, AlphaModeName(alphaMode_)},
           {WorldSpriteComponentPropertyIds::AlphaCutoff, alphaCutoff_},
           {WorldSpriteComponentPropertyIds::TransparentSortPriority, transparentSortPriority_}};
}

void WorldSpriteComponent::Deserialize(const nlohmann::json& in) {
    lit_ = JSONREAD::BooleanMemberOr(in, "lit", false);
    castShadow_ = JSONREAD::BooleanMemberOr(in, "castShadow", true);
    receiveShadow_ = JSONREAD::BooleanMemberOr(in, "receiveShadow", true);
    pointSampling_ = JSONREAD::BooleanMemberOr(in, "pointSampling", false);
    roughness_ = JSONREAD::NumberMemberOr(in, "roughness", 0.85f);
    specular_ = JSONREAD::NumberMemberOr(in, "specular", 0.25f);
    atlasColumns_ = JSONREAD::IntegerMemberOr(in, "atlasColumns", 1);
    atlasRows_ = JSONREAD::IntegerMemberOr(in, "atlasRows", 1);
    atlasFrames_ = JSONREAD::IntegerMemberOr(in, "atlasFrames", 1);
    framesPerSecond_ = JSONREAD::NumberMemberOr(in, "framesPerSecond", 0.0f);
    animationSeconds_ = 0;
    SanitizePresentation();
    textureAssetGuid_.value = JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::TextureAssetGuid, {});
    visible_ = JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::Visible, true);
    if (const nlohmann::json* value = JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Size)) {
        size_ = SanitizeSize(JSONREAD::Vec2Or(*value, size_));
    }
    preserveAspect_ = JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::PreserveAspect, true);
    facing_ = ParseFacing(JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::Facing, "CameraFacing"));
    if (const nlohmann::json* value = JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Tint)) {
        tint_ = SanitizeColor(JSONREAD::Vec3Or(*value, tint_));
    }
    opacity_ =
        ClampFinite(JSONREAD::NumberMemberOr(in, WorldSpriteComponentPropertyIds::Opacity, 1.0f), 1.0f, 0.0f, 1.0f);
    if (const nlohmann::json* value = JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Pivot)) {
        pivot_ = SanitizePivot(JSONREAD::Vec2Or(*value, pivot_));
    }
    alphaMode_ = ParseAlphaMode(JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::AlphaMode, "Auto"));
    alphaCutoff_ =
        ClampFinite(JSONREAD::NumberMemberOr(in, WorldSpriteComponentPropertyIds::AlphaCutoff, 0.5f), 0.5f, 0.0f, 1.0f);
    transparentSortPriority_ = std::clamp(
        JSONREAD::IntegerMemberOr(in, WorldSpriteComponentPropertyIds::TransparentSortPriority, 0), -32768, 32767);
    NotifyRenderStateDirty();
}

void WorldSpriteComponent::BuildInspector(IInspectorBuilder& builder) {
    const std::string textureLabel = TEXT::Localize("ui.inspector.worldSprite.texture");
    const std::string visibleLabel = TEXT::Localize("ui.inspector.worldSprite.visible");
    const std::string sizeLabel = TEXT::Localize("ui.inspector.worldSprite.size");
    const std::string preserveAspectLabel = TEXT::Localize("ui.inspector.worldSprite.preserveAspect");
    const std::string facingLabel = TEXT::Localize("ui.inspector.worldSprite.facing");
    const std::string tintLabel = TEXT::Localize("ui.inspector.worldSprite.tint");
    const std::string opacityLabel = TEXT::Localize("ui.inspector.worldSprite.opacity");
    const std::string pivotLabel = TEXT::Localize("ui.inspector.worldSprite.pivot");
    const std::string alphaModeLabel = TEXT::Localize("ui.inspector.worldSprite.alphaMode");
    const std::string alphaCutoffLabel = TEXT::Localize("ui.inspector.worldSprite.alphaCutoff");
    const std::string sortPriorityLabel = TEXT::Localize("ui.inspector.worldSprite.sortPriority");
    const std::string textureHelp = TEXT::Localize("ui.inspector.worldSprite.textureHelp");
    const std::string visibleHelp = TEXT::Localize("ui.inspector.worldSprite.visibleHelp");
    const std::string sizeHelp = TEXT::Localize("ui.inspector.worldSprite.sizeHelp");
    const std::string preserveAspectHelp = TEXT::Localize("ui.inspector.worldSprite.preserveAspectHelp");
    const std::string facingHelp = TEXT::Localize("ui.inspector.worldSprite.facingHelp");
    const std::string tintHelp = TEXT::Localize("ui.inspector.worldSprite.tintHelp");
    const std::string opacityHelp = TEXT::Localize("ui.inspector.worldSprite.opacityHelp");
    const std::string pivotHelp = TEXT::Localize("ui.inspector.worldSprite.pivotHelp");
    const std::string alphaModeHelp = TEXT::Localize("ui.inspector.worldSprite.alphaModeHelp");
    const std::string alphaCutoffHelp = TEXT::Localize("ui.inspector.worldSprite.alphaCutoffHelp");
    const std::string sortPriorityHelp = TEXT::Localize("ui.inspector.worldSprite.sortPriorityHelp");
    const auto field = [](const char* id, std::string_view label, const char* description) {
        return AuthoringFieldDescriptor{id, label, description};
    };

    bool changed = false;
    changed = builder.Bool({"lit", "Lit surface", "Receives scene lights, shadows and sky lighting."}, lit_) || changed;
    changed = builder.Bool({"castShadow", "Cast shadow",
                            "Opaque and cutout pixels cast directional and local shadows; transparent fringes do not."},
                           castShadow_) ||
              changed;
    changed = builder.Bool({"receiveShadow", "Receive shadow", "Apply scene shadow visibility to direct lighting."},
                           receiveShadow_) ||
              changed;
    changed =
        builder.FloatRange({"roughness", "Roughness", "Width of the reflected highlight."}, roughness_, 0.05f, 1.0f) ||
        changed;
    changed = builder.FloatRange("Specular", specular_, 0.0f, 1.0f) || changed;
    changed = builder.Bool("Point sampling", pointSampling_) || changed;
    changed = builder.Int("Atlas columns", atlasColumns_) || changed;
    changed = builder.Int("Atlas rows", atlasRows_) || changed;
    changed = builder.Int("Atlas frames", atlasFrames_) || changed;
    changed = builder.FloatRange("Frames per second", framesPerSecond_, 0.0f, 60.0f) || changed;
    SanitizePresentation();
    changed = builder.AssetIdPicker(
                  field(WorldSpriteComponentPropertyIds::TextureAssetGuid, textureLabel, textureHelp.c_str()),
                  AssetType::Texture, textureAssetGuid_.value) ||
              changed;
    changed =
        builder.Bool(field(WorldSpriteComponentPropertyIds::Visible, visibleLabel, visibleHelp.c_str()), visible_) ||
        changed;
    changed =
        builder.Vec2(field(WorldSpriteComponentPropertyIds::Size, sizeLabel, sizeHelp.c_str()), size_.x, size_.y) ||
        changed;
    changed = builder.Bool(field(WorldSpriteComponentPropertyIds::PreserveAspect, preserveAspectLabel,
                                 preserveAspectHelp.c_str()),
                           preserveAspect_) ||
              changed;

    const std::array facingLabels{
        TEXT::Localize("ui.inspector.worldSprite.fixed"),
        TEXT::Localize("ui.inspector.worldSprite.cameraFacing"),
        TEXT::Localize("ui.inspector.worldSprite.fixedYAxis"),
    };
    const std::array facingChoices{
        AuthoringChoice{"Fixed", facingLabels[0]},
        AuthoringChoice{"CameraFacing", facingLabels[1]},
        AuthoringChoice{"FixedYAxis", facingLabels[2]},
    };
    int facingIndex = static_cast<int>(facing_);
    if (builder.Choice(field(WorldSpriteComponentPropertyIds::Facing, facingLabel, facingHelp.c_str()), facingIndex,
                       facingChoices)) {
        facing_ = static_cast<FacingMode>(std::clamp(facingIndex, 0, 2));
        changed = true;
    }
    changed = builder.Color3(field(WorldSpriteComponentPropertyIds::Tint, tintLabel, tintHelp.c_str()), tint_.x,
                             tint_.y, tint_.z) ||
              changed;
    changed = builder.FloatRange(field(WorldSpriteComponentPropertyIds::Opacity, opacityLabel, opacityHelp.c_str()),
                                 opacity_, 0.0f, 1.0f, 0.01f) ||
              changed;

    if (builder.Section(TEXT::Localize("ui.inspector.worldSprite.advanced"), false)) {
        changed = builder.Vec2(field(WorldSpriteComponentPropertyIds::Pivot, pivotLabel, pivotHelp.c_str()), pivot_.x,
                               pivot_.y) ||
                  changed;
        const std::array alphaLabels{
            TEXT::Localize("ui.inspector.worldSprite.alphaAuto"),
            TEXT::Localize("ui.inspector.worldSprite.alphaOpaque"),
            TEXT::Localize("ui.inspector.worldSprite.alphaMask"),
            TEXT::Localize("ui.inspector.worldSprite.alphaBlend"),
        };
        const std::array alphaChoices{
            AuthoringChoice{"Auto", alphaLabels[0]},
            AuthoringChoice{"Opaque", alphaLabels[1]},
            AuthoringChoice{"Mask", alphaLabels[2]},
            AuthoringChoice{"Blend", alphaLabels[3]},
        };
        int alphaIndex = static_cast<int>(alphaMode_);
        if (builder.Choice(field(WorldSpriteComponentPropertyIds::AlphaMode, alphaModeLabel, alphaModeHelp.c_str()),
                           alphaIndex, alphaChoices)) {
            alphaMode_ = static_cast<AlphaMode>(std::clamp(alphaIndex, 0, 3));
            changed = true;
        }
        if (alphaMode_ == AlphaMode::Mask) {
            changed = builder.FloatRange(field(WorldSpriteComponentPropertyIds::AlphaCutoff, alphaCutoffLabel,
                                               alphaCutoffHelp.c_str()),
                                         alphaCutoff_, 0.0f, 1.0f, 0.01f) ||
                      changed;
        }
        changed = builder.Int(field(WorldSpriteComponentPropertyIds::TransparentSortPriority, sortPriorityLabel,
                                    sortPriorityHelp.c_str()),
                              transparentSortPriority_) ||
                  changed;
    }

    SanitizePresentation();
    size_ = SanitizeSize(size_);
    pivot_ = SanitizePivot(pivot_);
    tint_ = SanitizeColor(tint_);
    opacity_ = ClampFinite(opacity_, 1.0f, 0.0f, 1.0f);
    alphaCutoff_ = ClampFinite(alphaCutoff_, 0.5f, 0.0f, 1.0f);
    transparentSortPriority_ = std::clamp(transparentSortPriority_, -32768, 32767);
    if (changed) {
        NotifyRenderStateDirty();
    }
}

void WorldSpriteComponent::SanitizePresentation() noexcept {
    atlasColumns_ = std::clamp(atlasColumns_, 1, 64);
    atlasRows_ = std::clamp(atlasRows_, 1, 64);
    atlasFrames_ = std::clamp(atlasFrames_, 1, atlasColumns_ * atlasRows_);
    framesPerSecond_ = ClampFinite(framesPerSecond_, 0, 0, 60);
    roughness_ = ClampFinite(roughness_, 0.85f, 0.05f, 1);
    specular_ = ClampFinite(specular_, 0.25f, 0, 1);
}
void WorldSpriteComponent::Update(float dt) {
    if (framesPerSecond_ <= 0 || atlasFrames_ <= 1 || !std::isfinite(dt) || dt <= 0)
        return;
    const int before = static_cast<int>(animationSeconds_ * framesPerSecond_) % atlasFrames_;
    animationSeconds_ = std::fmod(animationSeconds_ + dt, double(atlasFrames_) / framesPerSecond_);
    if (before != static_cast<int>(animationSeconds_ * framesPerSecond_) % atlasFrames_)
        NotifyRenderStateDirty();
}
MATH::Vec4 WorldSpriteComponent::GetUvRect() const noexcept {
    const int frame = static_cast<int>(animationSeconds_ * framesPerSecond_) % atlasFrames_;
    return {float(frame % atlasColumns_) / atlasColumns_, float(frame / atlasColumns_) / atlasRows_,
            1.0f / atlasColumns_, 1.0f / atlasRows_};
}
} // namespace HIKARI
