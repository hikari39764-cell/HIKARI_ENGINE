#include "HIKARI_WorldSpriteComponent.h"

#include <algorithm>
#include <array>

#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

namespace HIKARI {
namespace {
MATH::Vec4 ResolveSpriteFrameUv(const SPRITE::SpriteFrame& frame, const SPRITE::SpritePage& page) noexcept {
    if (frame.empty || page.width == 0u || page.height == 0u) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return {static_cast<float>(frame.x) / static_cast<float>(page.width),
            static_cast<float>(frame.y) / static_cast<float>(page.height),
            static_cast<float>(frame.width) / static_cast<float>(page.width),
            static_cast<float>(frame.height) / static_cast<float>(page.height)};
}
} // namespace

void WorldSpriteComponent::Serialize(nlohmann::json& out) const {
    const std::string selectedClip(GetAnimationClip());
    out = {{"version", 5u},
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
           {WorldSpriteComponentPropertyIds::SpriteAssetGuid, spriteAssetGuid_.value},
           {WorldSpriteComponentPropertyIds::AnimationClip, selectedClip},
           {WorldSpriteComponentPropertyIds::AnimationSpeed, spritePlayer_.GetSpeed()},
           {WorldSpriteComponentPropertyIds::AnimationPaused, spritePlayer_.IsPaused()},
           {WorldSpriteComponentPropertyIds::Visible, visible_},
           {WorldSpriteComponentPropertyIds::FlipX, flipX_},
           {WorldSpriteComponentPropertyIds::Size, {size_.x, size_.y}},
           {WorldSpriteComponentPropertyIds::SizeExplicit, sizeExplicit_},
           {"sizeInitializedFromSprite", sizeInitializedFromSprite_},
           {WorldSpriteComponentPropertyIds::PreserveAspect, preserveAspect_},
           {WorldSpriteComponentPropertyIds::Facing, FacingName(facing_)},
           {WorldSpriteComponentPropertyIds::Tint, {tint_.x, tint_.y, tint_.z}},
           {WorldSpriteComponentPropertyIds::Opacity, opacity_},
           {WorldSpriteComponentPropertyIds::Pivot, {pivot_.x, pivot_.y}},
           {WorldSpriteComponentPropertyIds::PivotExplicit, pivotExplicit_},
           {WorldSpriteComponentPropertyIds::AlphaMode, AlphaModeName(alphaMode_)},
           {WorldSpriteComponentPropertyIds::AlphaCutoff, alphaCutoff_},
           {WorldSpriteComponentPropertyIds::TransparentSortPriority, transparentSortPriority_}};
    SerializeMaterialState(out["spriteMaterial"]);
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
    spritePlayer_.ClearSource();
    spriteAssetGuid_.value = JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::SpriteAssetGuid, {});
    animationClip_ = JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::AnimationClip, {});
    (void)spritePlayer_.SetSpeed(JSONREAD::NumberMemberOr(in, WorldSpriteComponentPropertyIds::AnimationSpeed, 1.0f));
    (void)spritePlayer_.SetPaused(
        JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::AnimationPaused, false));
    flipX_ = JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::FlipX, false);
    SanitizePresentation();
    textureAssetGuid_.value = JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::TextureAssetGuid, {});
    if (spriteAssetGuid_.IsValid()) {
        textureAssetGuid_ = {};
    }
    visible_ = JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::Visible, true);
    if (const nlohmann::json* value = JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Size)) {
        size_ = SanitizeSize(JSONREAD::Vec2Or(*value, size_));
    }
    sizeExplicit_ =
        JSONREAD::BooleanMemberOr(in, WorldSpriteComponentPropertyIds::SizeExplicit,
                                  JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Size) != nullptr);
    sizeInitializedFromSprite_ = JSONREAD::BooleanMemberOr(in, "sizeInitializedFromSprite", sizeExplicit_);
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
    pivotExplicit_ = JSONREAD::BooleanMemberOr(
        in, WorldSpriteComponentPropertyIds::PivotExplicit,
        !spriteAssetGuid_.IsValid() && JSONREAD::FindMember(in, WorldSpriteComponentPropertyIds::Pivot) != nullptr);
    alphaMode_ = ParseAlphaMode(JSONREAD::StringMemberOr(in, WorldSpriteComponentPropertyIds::AlphaMode, "Auto"));
    alphaCutoff_ =
        ClampFinite(JSONREAD::NumberMemberOr(in, WorldSpriteComponentPropertyIds::AlphaCutoff, 0.5f), 0.5f, 0.0f, 1.0f);
    transparentSortPriority_ = std::clamp(
        JSONREAD::IntegerMemberOr(in, WorldSpriteComponentPropertyIds::TransparentSortPriority, 0), -32768, 32767);
    const auto material = in.find("spriteMaterial");
    DeserializeMaterialState(material == in.end() ? nlohmann::json{} : *material);
    BumpMaterialStateRevision();
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
    changed = builder.FloatRange({"specular", "Specular"}, specular_, 0.0f, 1.0f) || changed;
    SanitizePresentation();
    std::string spriteAssetCandidate = spriteAssetGuid_.value;
    if (builder.AssetIdPicker(AuthoringFieldDescriptor{WorldSpriteComponentPropertyIds::SpriteAssetGuid,
                                                       "Sprite animation", "Hikari Sprite asset (.hsprite)."},
                              AssetType::Sprite, spriteAssetCandidate)) {
        if (spriteAssetCandidate.empty()) {
            spriteAssetGuid_ = {};
            animationClip_.clear();
            spritePlayer_.ClearSource();
            NotifyRenderStateDirty();
            changed = true;
        } else {
            const InspectorContext& context = builder.GetContext();
            const auto* store =
                context.worldServices != nullptr ? context.worldServices->Find<SPRITE::SpriteAssetStore>() : nullptr;
            std::string error{};
            const auto data = store != nullptr ? store->Load(AssetGuid{spriteAssetCandidate}, error) : nullptr;
            if (data != nullptr && BindSpriteAsset(AssetGuid{spriteAssetCandidate}, data)) {
                changed = true;
            } else {
                builder.Text(error.empty() ? "Sprite asset is unavailable in this World." : error);
            }
        }
    }

    if (spriteAssetGuid_.IsValid()) {
        // 一時 Inspector は World に attach されない。再生状態を書き換えず定義だけを参照する。
        std::shared_ptr<const SPRITE::SpriteAssetData> authoringAsset{};
        const SPRITE::SpriteAssetData* asset = spritePlayer_.GetAsset();
        if (asset == nullptr) {
            const auto* services = builder.GetContext().worldServices;
            const auto* store = services != nullptr ? services->Find<SPRITE::SpriteAssetStore>() : nullptr;
            std::string error{};
            authoringAsset = store != nullptr ? store->Load(spriteAssetGuid_, error) : nullptr;
            asset = authoringAsset.get();
            if (asset == nullptr) {
                builder.Text(error.empty() ? "Sprite animation metadata is unavailable." : error);
            }
        }
        if (asset != nullptr && !asset->clips.empty()) {
            std::vector<AuthoringChoice> clipChoices{};
            clipChoices.reserve(asset->clips.size());
            int selectedClip = 0;
            const std::string_view authoredClip = GetAnimationClip();
            const std::string_view currentClip =
                authoredClip.empty() ? std::string_view(asset->defaultClip) : authoredClip;
            for (size_t index = 0u; index < asset->clips.size(); ++index) {
                const std::string& clip = asset->clips[index].id;
                clipChoices.push_back({clip, clip});
                if (clip == currentClip) {
                    selectedClip = static_cast<int>(index);
                }
            }
            if (builder.Choice(AuthoringFieldDescriptor{WorldSpriteComponentPropertyIds::AnimationClip,
                                                        "Animation clip", "Named clip from the bound sprite asset."},
                               selectedClip, clipChoices) &&
                selectedClip >= 0 && static_cast<size_t>(selectedClip) < asset->clips.size()) {
                const std::string& clip = asset->clips[static_cast<size_t>(selectedClip)].id;
                if (spritePlayer_.HasSpriteAsset()) {
                    changed = PlayAnimation(clip, true) || changed;
                } else {
                    animationClip_ = clip;
                    changed = true;
                }
            }
        }
        bool paused = spritePlayer_.IsPaused();
        if (builder.Bool(AuthoringFieldDescriptor{WorldSpriteComponentPropertyIds::AnimationPaused, "Paused",
                                                  "Pause the current sprite clip."},
                         paused)) {
            changed = spritePlayer_.SetPaused(paused) || changed;
        }
        float speed = spritePlayer_.GetSpeed();
        if (builder.Float(AuthoringFieldDescriptor{WorldSpriteComponentPropertyIds::AnimationSpeed, "Playback speed",
                                                   "Positive finite playback multiplier."},
                          speed)) {
            if (spritePlayer_.SetSpeed(speed)) {
                changed = true;
            } else {
                builder.Text("Playback speed must be finite and greater than zero.");
            }
        }
    } else {
        changed = builder.Bool({"pointSampling", "Point sampling"}, pointSampling_) || changed;
        std::string textureCandidate = textureAssetGuid_.value;
        if (builder.AssetIdPicker(
                field(WorldSpriteComponentPropertyIds::TextureAssetGuid, textureLabel, textureHelp.c_str()),
                AssetType::Texture, textureCandidate)) {
            SetTextureAssetGuid(AssetGuid{textureCandidate});
            changed = true;
        }
        changed = builder.Int({"atlasColumns", "Atlas columns"}, atlasColumns_) || changed;
        changed = builder.Int({"atlasRows", "Atlas rows"}, atlasRows_) || changed;
        changed = builder.Int({"atlasFrames", "Atlas frames"}, atlasFrames_) || changed;
        changed =
            builder.FloatRange({"framesPerSecond", "Frames per second"}, framesPerSecond_, 0.0f, 60.0f) || changed;
    }
    changed =
        builder.Bool(field(WorldSpriteComponentPropertyIds::Visible, visibleLabel, visibleHelp.c_str()), visible_) ||
        changed;
    changed = builder.Bool(field(WorldSpriteComponentPropertyIds::FlipX, "Flip X",
                                 "Mirror sprite content horizontally around its pivot."),
                           flipX_) ||
              changed;
    if (builder.Vec2(field(WorldSpriteComponentPropertyIds::Size, sizeLabel, sizeHelp.c_str()), size_.x, size_.y)) {
        sizeExplicit_ = true;
        sizeInitializedFromSprite_ = true;
        changed = true;
    }
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
        MATH::Vec2 displayedPivot = GetEffectivePivot();
        if (builder.Vec2(field(WorldSpriteComponentPropertyIds::Pivot, pivotLabel, pivotHelp.c_str()), displayedPivot.x,
                         displayedPivot.y)) {
            pivot_ = SanitizePivot(displayedPivot);
            pivotExplicit_ = true;
            changed = true;
        }
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

    BuildMaterialInspector(builder);

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

void WorldSpriteComponent::SetTextureAssetGuid(AssetGuid guid) {
    if (textureAssetGuid_ == guid && !spriteAssetGuid_.IsValid() && !spritePlayer_.HasSpriteAsset() &&
        !spritePlayer_.HasGridAnimation() && atlasColumns_ == 1 && atlasRows_ == 1 && atlasFrames_ == 1 &&
        framesPerSecond_ == 0.0f) {
        return;
    }
    textureAssetGuid_ = std::move(guid);
    spriteAssetGuid_ = {};
    animationClip_.clear();
    spritePlayer_.ClearSource();
    atlasColumns_ = 1;
    atlasRows_ = 1;
    atlasFrames_ = 1;
    framesPerSecond_ = 0.0f;
    NotifyRenderStateDirty();
}

bool WorldSpriteComponent::BindSpriteAsset(const AssetGuid& guid, std::shared_ptr<const SPRITE::SpriteAssetData> asset,
                                           std::string_view initialClip) {
    if (!guid.IsValid() || !asset || !MATH::IsFinite(asset->pixelsPerUnit) || asset->pixelsPerUnit <= 0.0f) {
        return false;
    }
    WorldSpritePlayer candidatePlayer = spritePlayer_;
    if (!candidatePlayer.BindAsset(asset, initialClip)) {
        return false;
    }
    const SPRITE::SpriteFrame* sourceFrame = candidatePlayer.GetSpriteFrame();
    if (sourceFrame == nullptr || sourceFrame->sourceWidth == 0u || sourceFrame->sourceHeight == 0u) {
        return false;
    }

    spritePlayer_ = std::move(candidatePlayer);
    spriteAssetGuid_ = guid;
    textureAssetGuid_ = {};
    animationClip_.clear();
    atlasColumns_ = 1;
    atlasRows_ = 1;
    atlasFrames_ = 1;
    framesPerSecond_ = 0.0f;
    if (!sizeExplicit_ && !sizeInitializedFromSprite_) {
        size_ = {static_cast<float>(sourceFrame->sourceWidth) / asset->pixelsPerUnit,
                 static_cast<float>(sourceFrame->sourceHeight) / asset->pixelsPerUnit};
        size_ = SanitizeSize(size_);
    }
    sizeInitializedFromSprite_ = true;
    NotifyRenderStateDirty();
    return true;
}

bool WorldSpriteComponent::PlayAnimation(std::string_view clip, bool restart) {
    if (!spritePlayer_.HasSpriteAsset() || !spritePlayer_.Play(clip, restart)) {
        return false;
    }
    animationClip_.clear();
    NotifyRenderStateDirty();
    return true;
}

bool WorldSpriteComponent::SetPaused(bool paused) noexcept {
    if (!spritePlayer_.HasSpriteAsset()) {
        return false;
    }
    const bool changed = spritePlayer_.IsPaused() != paused;
    if (!spritePlayer_.SetPaused(paused)) {
        return false;
    }
    if (changed) {
        NotifyRenderStateDirty();
    }
    return true;
}

bool WorldSpriteComponent::SetPlaybackSpeed(float speed) noexcept {
    if (!spritePlayer_.HasSpriteAsset() || !spritePlayer_.SetSpeed(speed)) {
        return false;
    }
    NotifyRenderStateDirty();
    return true;
}

bool WorldSpriteComponent::RestartAnimation() noexcept {
    if (!spritePlayer_.HasSpriteAsset() || !spritePlayer_.Restart()) {
        return false;
    }
    NotifyRenderStateDirty();
    return true;
}

bool WorldSpriteComponent::SeekAnimation(double timeMs) noexcept {
    if (!spritePlayer_.HasSpriteAsset() || !spritePlayer_.Seek(timeMs)) {
        return false;
    }
    NotifyRenderStateDirty();
    return true;
}

WorldSpriteAnimationState WorldSpriteComponent::GetAnimationState() const noexcept {
    WorldSpriteAnimationState state{};
    state.timeMs = spritePlayer_.GetTimeMs();
    state.durationMs = spritePlayer_.GetDurationMs();
    state.speed = spritePlayer_.GetSpeed();
    state.frameIndex = spritePlayer_.GetFrameIndex();
    state.bound = spritePlayer_.HasSpriteAsset();
    state.hasClip = spritePlayer_.GetClip() != nullptr;
    state.loop = spritePlayer_.IsLooping();
    state.paused = spritePlayer_.IsPaused();
    state.complete = spritePlayer_.IsComplete();
    return state;
}

const SPRITE::SpriteClip* WorldSpriteComponent::FindAnimationClip(std::string_view clipId) const noexcept {
    const auto* asset = spritePlayer_.GetAsset();
    if (asset != nullptr) {
        for (const auto& clip : asset->clips) {
            if (clip.id == clipId) {
                return &clip;
            }
        }
    }
    return nullptr;
}

MATH::Vec2 WorldSpriteComponent::GetEffectivePivot() const noexcept {
    if (!pivotExplicit_ && spriteAssetGuid_.IsValid()) {
        const SPRITE::SpriteFrame* frame = spritePlayer_.GetSpriteFrame();
        if (frame != nullptr && frame->sourceWidth != 0u && frame->sourceHeight != 0u) {
            const float sourceWidth = static_cast<float>(frame->sourceWidth);
            const float sourceHeight = static_cast<float>(frame->sourceHeight);
            return {frame->pivotX / sourceWidth, 1.0f - frame->pivotY / sourceHeight};
        }
    }
    return pivot_;
}

void WorldSpriteComponent::Update(float dt) {
    if (!spriteAssetGuid_.IsValid()) {
        (void)spritePlayer_.ConfigureGrid(static_cast<uint32_t>(atlasFrames_), framesPerSecond_);
    }

    if (spritePlayer_.Update(dt)) {
        NotifyRenderStateDirty();
    }
}

MATH::Vec4 WorldSpriteComponent::GetUvRect() const noexcept {
    if (spritePlayer_.HasSpriteAsset()) {
        const SPRITE::SpriteFrame* frame = spritePlayer_.GetSpriteFrame();
        const SPRITE::SpritePage* page = spritePlayer_.GetSpritePage();
        if (frame == nullptr || page == nullptr) {
            return {0.0f, 0.0f, 0.0f, 0.0f};
        }
        return ResolveSpriteFrameUv(*frame, *page);
    }
    const uint32_t frame = spritePlayer_.GetGridFrameIndex();
    return {static_cast<float>(frame % static_cast<uint32_t>(atlasColumns_)) / static_cast<float>(atlasColumns_),
            static_cast<float>(frame / static_cast<uint32_t>(atlasColumns_)) / static_cast<float>(atlasRows_),
            1.0f / static_cast<float>(atlasColumns_), 1.0f / static_cast<float>(atlasRows_)};
}

bool WorldSpriteComponent::ResolveRenderProjection(const MATH::Vec3& worldScale,
                                                   WorldSpriteRenderProjection& outProjection) const {
    outProjection = {};
    if (spriteAssetGuid_.IsValid()) {
        const SPRITE::SpriteFrame* frame = spritePlayer_.GetSpriteFrame();
        const SPRITE::SpritePage* page = spritePlayer_.GetSpritePage();
        if (!spritePlayer_.HasSpriteAsset() || frame == nullptr || page == nullptr || page->width == 0u ||
            page->height == 0u || frame->sourceWidth == 0u || frame->sourceHeight == 0u) {
            return false;
        }
        outProjection.textureAssetId = AssetId{page->textureAssetId};
        if (!page->normalTextureAssetId.empty()) {
            outProjection.normalTextureAssetId = AssetId{page->normalTextureAssetId};
        }
        outProjection.usesSpriteAsset = true;
        outProjection.rotated = frame->rotated;
        outProjection.flipX = flipX_;
        outProjection.pointSampling = true;
        outProjection.preserveAspect = false;
        if (frame->empty) {
            outProjection.empty = true;
            return true;
        }

        const uint32_t trimWidth = frame->rotated ? frame->height : frame->width;
        const uint32_t trimHeight = frame->rotated ? frame->width : frame->height;
        if (trimWidth == 0u || trimHeight == 0u || frame->x > page->width || frame->y > page->height ||
            frame->width > page->width - frame->x || frame->height > page->height - frame->y) {
            return false;
        }
        const float sourceWidth = static_cast<float>(frame->sourceWidth);
        const float sourceHeight = static_cast<float>(frame->sourceHeight);
        const float worldScaleX = (std::max)(std::abs(worldScale.x), 0.001f);
        const float worldScaleY = (std::max)(std::abs(worldScale.y), 0.001f);
        MATH::Vec2 logicalSize = size_;
        if (preserveAspect_) {
            logicalSize.x = size_.y * worldScaleY / worldScaleX * sourceWidth / sourceHeight;
        }
        outProjection.size = {logicalSize.x * static_cast<float>(trimWidth) / sourceWidth,
                              logicalSize.y * static_cast<float>(trimHeight) / sourceHeight};

        const MATH::Vec2 effectivePivot = GetEffectivePivot();
        const float sourcePivotX = effectivePivot.x * sourceWidth;
        const float sourcePivotY = (1.0f - effectivePivot.y) * sourceHeight;
        outProjection.pivot = {(sourcePivotX - static_cast<float>(frame->trimX)) / static_cast<float>(trimWidth),
                               (static_cast<float>(frame->trimY) + static_cast<float>(trimHeight) - sourcePivotY) /
                                   static_cast<float>(trimHeight)};
        if (flipX_) {
            outProjection.pivot.x = 1.0f - outProjection.pivot.x;
        }
        outProjection.uvRect = ResolveSpriteFrameUv(*frame, *page);
        outProjection.sourceCanvasRect = {static_cast<float>(frame->trimX), static_cast<float>(frame->trimY),
                                          sourceWidth, sourceHeight};
        return true;
    }

    if (!textureAssetGuid_.IsValid()) {
        return false;
    }
    outProjection.textureAssetId = AssetId{textureAssetGuid_.value};
    outProjection.size = size_;
    outProjection.pivot = {flipX_ ? 1.0f - pivot_.x : pivot_.x, pivot_.y};
    outProjection.uvRect = GetUvRect();
    outProjection.flipX = flipX_;
    outProjection.preserveAspect = preserveAspect_;
    outProjection.pointSampling = pointSampling_;
    return true;
}
} // namespace HIKARI
