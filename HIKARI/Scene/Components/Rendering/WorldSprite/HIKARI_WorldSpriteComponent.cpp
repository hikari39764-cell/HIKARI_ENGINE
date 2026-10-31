#include "HIKARI_WorldSpriteComponent.h"
#include "Core/Math/HIKARI_ScalarMath.h"

#include <algorithm>
#include <array>

#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

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

WorldSpriteSettings NormalizeWorldSpriteSettings(WorldSpriteSettings settings) {
    settings = JSONFIELDS::Sanitize(settings, kWorldSpriteFields);
    settings.atlasFrames = std::clamp(settings.atlasFrames, 1, settings.atlasColumns * settings.atlasRows);
    // 実行時の API は列挙値を数値のまま渡せるので、名前の表に無い値は既定へ戻す。
    if (!TEXT::IsNamedEnum(kWorldSpriteFacingNames, settings.facing)) {
        settings.facing = RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::CameraFacing;
    }
    if (!TEXT::IsNamedEnum(kWorldSpriteAlphaModeNames, settings.alphaMode)) {
        settings.alphaMode = RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Auto;
    }
    if (settings.spriteAssetGuid.IsValid()) {
        settings.textureAssetGuid = {};
    }
    return settings;
}

bool DrawWorldSpriteClip(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, std::string& value,
                         const WorldSpriteSettings& owner) {
    // 一時の検査器は World に結び付かない。再生状態を書き換えず、資産の定義だけを参照する。
    const auto* services = builder.GetContext().worldServices;
    const auto* store = services != nullptr ? services->Find<SPRITE::SpriteAssetStore>() : nullptr;
    std::string error{};
    const std::shared_ptr<const SPRITE::SpriteAssetData> asset =
        store != nullptr ? store->Load(owner.spriteAssetGuid, error) : nullptr;
    if (asset == nullptr) {
        builder.Text(error.empty() ? "Sprite animation metadata is unavailable." : error);
        return false;
    }
    if (asset->clips.empty()) {
        return false;
    }
    std::vector<AuthoringChoice> choices{};
    choices.reserve(asset->clips.size());
    int selected = 0;
    const std::string_view current = value.empty() ? std::string_view(asset->defaultClip) : std::string_view(value);
    for (size_t index = 0u; index < asset->clips.size(); ++index) {
        const std::string& clip = asset->clips[index].id;
        choices.push_back({clip, clip});
        if (clip == current) {
            selected = static_cast<int>(index);
        }
    }
    if (!builder.Choice(field, selected, choices) || selected < 0 ||
        static_cast<size_t>(selected) >= asset->clips.size()) {
        return false;
    }
    value = asset->clips[static_cast<size_t>(selected)].id;
    return true;
}

WorldSpriteSettings WorldSpriteComponent::Snapshot() const {
    WorldSpriteSettings snapshot = settings_;
    snapshot.animationClip = std::string(GetAnimationClip());
    snapshot.animationSpeed = spritePlayer_.GetSpeed();
    snapshot.animationPaused = spritePlayer_.IsPaused();
    return snapshot;
}

void WorldSpriteComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, Snapshot(), kWorldSpriteFields);
    SerializeMaterialState(out["spriteMaterial"]);
}

void WorldSpriteComponent::Deserialize(const nlohmann::json& in) {
    WorldSpriteSettings decoded{};
    JSONFIELDS::Read(in, decoded, kWorldSpriteFields);
    settings_ = NormalizeWorldSpriteSettings(std::move(decoded));
    spritePlayer_.ClearSource();
    (void)spritePlayer_.SetSpeed(settings_.animationSpeed);
    (void)spritePlayer_.SetPaused(settings_.animationPaused);
    const nlohmann::json* material = JSONREAD::FindMember(in, "spriteMaterial");
    DeserializeMaterialState(material != nullptr ? *material : nlohmann::json{});
    BumpMaterialStateRevision();
    NotifyRenderStateDirty();
}

bool WorldSpriteComponent::ApplyRuntimeState(const RuntimeState& state) {
    WorldSpriteSettings next = settings_;
    next.size = state.size;
    next.tint = state.tint;
    next.pivot = state.pivot;
    next.opacity = state.opacity;
    next.alphaCutoff = state.alphaCutoff;
    next.transparentSortPriority = state.transparentSortPriority;
    next.facing = state.facing;
    next.alphaMode = state.alphaMode;
    next.visible = state.visible;
    next.preserveAspect = state.preserveAspect;
    next.sizeExplicit = state.sizeExplicit;
    next.pivotExplicit = state.pivotExplicit;
    next.flipX = state.flipX;
    next = NormalizeWorldSpriteSettings(std::move(next));
    if (next == settings_) {
        return false;
    }
    next.sizeInitializedFromSprite = next.sizeInitializedFromSprite || state.sizeExplicit;
    settings_ = std::move(next);
    NotifyRenderStateDirty();
    return true;
}

#if defined(HIKARI_WITH_EDITOR)
void WorldSpriteComponent::BuildInspector(IInspectorBuilder& builder) {
    WorldSpriteSettings before = Snapshot();
    // 軸心は、作者が指定していなければスプライトのフレームが持つ値を見せる。
    before.pivot = GetEffectivePivot();
    WorldSpriteSettings next = before;
    if (InspectFields(builder, next, kWorldSpriteFields)) {
        ApplyInspectorEdit(builder, before, std::move(next));
    }
    BuildMaterialInspector(builder);
}

void WorldSpriteComponent::ApplyInspectorEdit(IInspectorBuilder& builder, const WorldSpriteSettings& before,
                                              WorldSpriteSettings next) {
    // 参照する資産の付け替えは、読み込みと再生状態の作り直しを伴うので、それだけを行う。
    if (next.spriteAssetGuid != before.spriteAssetGuid) {
        if (next.spriteAssetGuid.value.empty()) {
            settings_.spriteAssetGuid = {};
            settings_.animationClip.clear();
            spritePlayer_.ClearSource();
            NotifyRenderStateDirty();
            return;
        }
        const auto* services = builder.GetContext().worldServices;
        const auto* store = services != nullptr ? services->Find<SPRITE::SpriteAssetStore>() : nullptr;
        std::string error{};
        const auto data = store != nullptr ? store->Load(next.spriteAssetGuid, error) : nullptr;
        if (data == nullptr || !BindSpriteAsset(next.spriteAssetGuid, data)) {
            builder.Text(error.empty() ? "Sprite asset is unavailable in this World." : error);
        }
        return;
    }
    if (next.textureAssetGuid != before.textureAssetGuid) {
        SetTextureAssetGuid(next.textureAssetGuid);
        return;
    }

    // 再生の状態は player が持つ。
    if (next.animationClip != before.animationClip) {
        if (spritePlayer_.HasSpriteAsset()) {
            (void)PlayAnimation(next.animationClip, true);
        } else {
            settings_.animationClip = next.animationClip;
        }
    }
    if (next.animationPaused != before.animationPaused) {
        (void)spritePlayer_.SetPaused(next.animationPaused);
    }
    if (next.animationSpeed != before.animationSpeed && !spritePlayer_.SetSpeed(next.animationSpeed)) {
        builder.Text("Playback speed must be finite and greater than zero.");
    }
    next.animationClip = settings_.animationClip;
    next.animationPaused = settings_.animationPaused;
    next.animationSpeed = settings_.animationSpeed;

    if (next.size != before.size) {
        next.sizeExplicit = true;
        next.sizeInitializedFromSprite = true;
    }
    if (next.pivot != before.pivot) {
        next.pivotExplicit = true;
    } else {
        next.pivot = settings_.pivot;
    }
    settings_ = NormalizeWorldSpriteSettings(std::move(next));
    NotifyRenderStateDirty();
}
#endif

void WorldSpriteComponent::SetTextureAssetGuid(AssetGuid guid) {
    if (settings_.textureAssetGuid == guid && !settings_.spriteAssetGuid.IsValid() && !spritePlayer_.HasSpriteAsset() &&
        !spritePlayer_.HasGridAnimation() && settings_.atlasColumns == 1 && settings_.atlasRows == 1 &&
        settings_.atlasFrames == 1 && settings_.framesPerSecond == 0.0f) {
        return;
    }
    settings_.textureAssetGuid = std::move(guid);
    settings_.spriteAssetGuid = {};
    settings_.animationClip.clear();
    spritePlayer_.ClearSource();
    settings_.atlasColumns = 1;
    settings_.atlasRows = 1;
    settings_.atlasFrames = 1;
    settings_.framesPerSecond = 0.0f;
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
    settings_.spriteAssetGuid = guid;
    settings_.textureAssetGuid = {};
    settings_.animationClip.clear();
    settings_.atlasColumns = 1;
    settings_.atlasRows = 1;
    settings_.atlasFrames = 1;
    settings_.framesPerSecond = 0.0f;
    if (!settings_.sizeExplicit && !settings_.sizeInitializedFromSprite) {
        settings_.size = {static_cast<float>(sourceFrame->sourceWidth) / asset->pixelsPerUnit,
                          static_cast<float>(sourceFrame->sourceHeight) / asset->pixelsPerUnit};
        settings_ = NormalizeWorldSpriteSettings(std::move(settings_));
    }
    settings_.sizeInitializedFromSprite = true;
    NotifyRenderStateDirty();
    return true;
}

bool WorldSpriteComponent::PlayAnimation(std::string_view clip, bool restart) {
    if (!spritePlayer_.HasSpriteAsset() || !spritePlayer_.Play(clip, restart)) {
        return false;
    }
    settings_.animationClip.clear();
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
    if (!settings_.pivotExplicit && settings_.spriteAssetGuid.IsValid()) {
        const SPRITE::SpriteFrame* frame = spritePlayer_.GetSpriteFrame();
        if (frame != nullptr && frame->sourceWidth != 0u && frame->sourceHeight != 0u) {
            const float sourceWidth = static_cast<float>(frame->sourceWidth);
            const float sourceHeight = static_cast<float>(frame->sourceHeight);
            return {frame->pivotX / sourceWidth, 1.0f - frame->pivotY / sourceHeight};
        }
    }
    return settings_.pivot;
}

void WorldSpriteComponent::Update(float dt) {
    if (!settings_.spriteAssetGuid.IsValid()) {
        (void)spritePlayer_.ConfigureGrid(static_cast<uint32_t>(settings_.atlasFrames), settings_.framesPerSecond);
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
    return {static_cast<float>(frame % static_cast<uint32_t>(settings_.atlasColumns)) /
                static_cast<float>(settings_.atlasColumns),
            static_cast<float>(frame / static_cast<uint32_t>(settings_.atlasColumns)) /
                static_cast<float>(settings_.atlasRows),
            1.0f / static_cast<float>(settings_.atlasColumns), 1.0f / static_cast<float>(settings_.atlasRows)};
}

bool WorldSpriteComponent::ResolveRenderProjection(const MATH::Vec3& worldScale,
                                                   WorldSpriteRenderProjection& outProjection) const {
    outProjection = {};
    if (settings_.spriteAssetGuid.IsValid()) {
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
        outProjection.flipX = settings_.flipX;
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
        MATH::Vec2 logicalSize = settings_.size;
        if (settings_.preserveAspect) {
            logicalSize.x = settings_.size.y * worldScaleY / worldScaleX * sourceWidth / sourceHeight;
        }
        outProjection.size = {logicalSize.x * static_cast<float>(trimWidth) / sourceWidth,
                              logicalSize.y * static_cast<float>(trimHeight) / sourceHeight};

        const MATH::Vec2 effectivePivot = GetEffectivePivot();
        const float sourcePivotX = effectivePivot.x * sourceWidth;
        const float sourcePivotY = (1.0f - effectivePivot.y) * sourceHeight;
        outProjection.pivot = {(sourcePivotX - static_cast<float>(frame->trimX)) / static_cast<float>(trimWidth),
                               (static_cast<float>(frame->trimY) + static_cast<float>(trimHeight) - sourcePivotY) /
                                   static_cast<float>(trimHeight)};
        if (settings_.flipX) {
            outProjection.pivot.x = 1.0f - outProjection.pivot.x;
        }
        outProjection.uvRect = ResolveSpriteFrameUv(*frame, *page);
        outProjection.sourceCanvasRect = {static_cast<float>(frame->trimX), static_cast<float>(frame->trimY),
                                          sourceWidth, sourceHeight};
        return true;
    }

    if (!settings_.textureAssetGuid.IsValid()) {
        return false;
    }
    outProjection.textureAssetId = AssetId{settings_.textureAssetGuid.value};
    outProjection.size = settings_.size;
    outProjection.pivot = {settings_.flipX ? 1.0f - settings_.pivot.x : settings_.pivot.x, settings_.pivot.y};
    outProjection.uvRect = GetUvRect();
    outProjection.flipX = settings_.flipX;
    outProjection.preserveAspect = settings_.preserveAspect;
    outProjection.pointSampling = settings_.pointSampling;
    return true;
}
} // namespace HIKARI
