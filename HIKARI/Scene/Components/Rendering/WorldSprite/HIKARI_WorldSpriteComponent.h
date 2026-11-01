#pragma once

#include <algorithm>
#include "Core/Math/HIKARI_ScalarMath.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Assets/Sprite/HIKARI_SpriteAsset.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpritePlayer.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"
#include "Core/Numeric/HIKARI_NonZero.h"

namespace HIKARI {

struct WorldSpriteRenderProjection {
    AssetGuid textureAssetId{};
    AssetGuid normalTextureAssetId{};
    MATH::Vec2 size{1.0f, 1.0f};
    MATH::Vec2 pivot{0.5f, 0.5f};
    MATH::Vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    MATH::Vec4 sourceCanvasRect{};
    bool preserveAspect = true;
    bool usesSpriteAsset = false;
    bool rotated = false;
    bool flipX = false;
    bool pointSampling = false;
    bool empty = false;
};

// シーンに保存する名前。
inline constexpr TEXT::EnumName<RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode> kWorldSpriteFacingNames[] = {
    {RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::Fixed, "Fixed"},
    {RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::CameraFacing, "CameraFacing"},
    {RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::FixedYAxis, "FixedYAxis"},
};
inline constexpr TEXT::EnumName<RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode> kWorldSpriteAlphaModeNames[] = {
    {RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Auto, "Auto"},
    {RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Opaque, "Opaque"},
    {RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Mask, "Mask"},
    {RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Blend, "Blend"},
};

struct AuthoringFieldDescriptor;

// 作者値（シーン文書の properties と同じ形）のうち、項目表（kWorldSpriteFields）で宣言できる項目。スプライト材質
// （spriteMaterial）は材質アセットの束縛と上書きを持つので、部件が読み書きする。再生の状態（クリップ・速さ・一時停止）は
// WorldSpritePlayer が持つので、保存と検査器はそこから写す（animationClip / animationSpeed / animationPaused の値は
// 読み込み時に渡す初期値）。
struct WorldSpriteSettings {
    AssetGuid spriteAssetGuid{};
    std::string animationClip{};
    bool animationPaused = false;
    float animationSpeed = 1.0f;
    AssetGuid textureAssetGuid{};
    bool pointSampling = false;
    int atlasColumns = 1;
    int atlasRows = 1;
    int atlasFrames = 1;
    float framesPerSecond = 0.0f;
    bool visible = true;
    bool flipX = false;
    MATH::Vec2 size{1.0f, 1.0f};
    // 作者が指定した大きさか。指定が無い間は、スプライトを結び付けた時にその大きさへ合わせる。
    bool sizeExplicit = false;
    bool sizeInitializedFromSprite = false;
    bool preserveAspect = true;
    RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode facing =
        RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::CameraFacing;
    MATH::Vec3 tint{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    MATH::Vec2 pivot{0.5f, 0.5f};
    // 作者が指定した軸心か。指定が無い間は、スプライトのフレームが持つ軸心を使う。
    bool pivotExplicit = false;
    RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode alphaMode = RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Auto;
    float alphaCutoff = 0.5f;
    int transparentSortPriority = 0;
    bool lit = false;
    bool castShadow = true;
    bool receiveShadow = true;
    float roughness = 0.85f;
    float specular = 0.25f;

    bool operator==(const WorldSpriteSettings&) const = default;
};

// animationClip の選択欄（kWorldSpriteFields の Draw）。結び付けたスプライト資産が持つクリップから選ぶ。
bool DrawWorldSpriteClip(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, std::string& value,
                         const WorldSpriteSettings& owner);

inline constexpr auto kWorldSpriteFields = [](auto&& field) {
    using S = WorldSpriteSettings;
    using namespace JSONFIELDS;
    constexpr auto animated = [](const S& s) { return s.spriteAssetGuid.IsValid(); };
    constexpr auto plain = [](const S& s) { return !s.spriteAssetGuid.IsValid(); };

    field("spriteAssetGuid", &S::spriteAssetGuid, Label{"Sprite animation"}, Help{"Hikari Sprite asset (.hsprite)."},
          AssetRef{AssetType::Sprite}, Common{});
    field("animationClip", &S::animationClip, Label{"Animation clip"}, Help{"Named clip from the bound sprite asset."},
          Draw{&DrawWorldSpriteClip}, Show{animated}, Common{});
    field("animationPaused", &S::animationPaused, Label{"Paused"}, Help{"Pause the current sprite clip."},
          Show{animated}, Common{});
    field("animationSpeed", &S::animationSpeed, Label{"Playback speed"}, Help{"Positive finite playback multiplier."},
          Show{animated}, Common{});
    field("textureAssetGuid", &S::textureAssetGuid, Label{"Texture"}, Help{"Texture displayed directly in 3D space."},
          AssetRef{AssetType::Texture}, Show{plain}, Common{});
    field("visible", &S::visible, Label{"Visible"}, Help{"Controls whether this World Sprite is rendered."}, Common{});
    field("size", &S::size, Range{0.001, 100000.0}, Label{"Size"}, Help{"Width and height in world units."}, Common{});
    field("tint", &S::tint, Range{0.0, 16.0}, Label{"Tint"}, Color{}, Help{"Linear color multiplier for the texture."},
          Common{});
    field("opacity", &S::opacity, Range{0.0, 1.0}, Label{"Opacity"}, Help{"Final sprite opacity."}, Common{});

    field("flipX", &S::flipX, Label{"Flip X"}, Help{"Mirror sprite content horizontally around its pivot."});
    field("preserveAspect", &S::preserveAspect, Label{"Preserve Aspect"},
          Help{"Uses height and derives width from the texture aspect ratio."});
    field("facing", &S::facing, kWorldSpriteFacingNames, Label{"Facing"},
          Help{"Controls how the plane faces each render view."});
    field("pointSampling", &S::pointSampling, Label{"Point sampling"}, Show{plain});
    field("atlasColumns", &S::atlasColumns, Range{1.0, 64.0}, Label{"Atlas columns"}, Show{plain});
    field("atlasRows", &S::atlasRows, Range{1.0, 64.0}, Label{"Atlas rows"}, Show{plain});
    field("atlasFrames", &S::atlasFrames, Range{1.0, 4096.0}, Label{"Atlas frames"}, Show{plain});
    field("framesPerSecond", &S::framesPerSecond, Range{0.0, 60.0}, Label{"Frames per second"}, Show{plain});

    constexpr const char* kShading = "Shading";
    field("lit", &S::lit, Label{"Lit surface"}, Help{"Receives scene lights, shadows and sky lighting."},
          Group{kShading});
    field("castShadow", &S::castShadow, Label{"Cast Shadow"},
          Help{"Opaque and cutout pixels cast directional and local shadows; transparent fringes do not."},
          Group{kShading});
    field("receiveShadow", &S::receiveShadow, Label{"Receive Shadow"},
          Help{"Apply scene shadow visibility to direct lighting."}, Group{kShading});
    field("roughness", &S::roughness, Range{0.05, 1.0}, Label{"Roughness"}, Help{"Width of the reflected highlight."},
          Group{kShading});
    field("specular", &S::specular, Range{0.0, 1.0}, Label{"Specular"}, Group{kShading});

    field("pivot", &S::pivot, Range{0.0, 1.0}, Label{"Pivot"},
          Help{"Placement pivot in normalized texture coordinates."}, Advanced{});
    field("alphaMode", &S::alphaMode, kWorldSpriteAlphaModeNames, Label{"Alpha Mode"},
          Help{"Auto uses imported alpha diagnostics and anchors opaque coverage in depth while retaining soft "
               "edges."},
          Advanced{});
    field("alphaCutoff", &S::alphaCutoff, Range{0.0, 1.0}, Label{"Alpha Cutoff"},
          Help{"Pixels below this alpha value are discarded."},
          Show{[](const S& s) { return s.alphaMode == RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode::Mask; }},
          Advanced{});
    field("transparentSortPriority", &S::transparentSortPriority, Range{-32768.0, 32767.0},
          Label{"Transparent Sort Priority"}, Help{"Higher transparent priorities are drawn later."}, Advanced{});
    field("sizeExplicit", &S::sizeExplicit, Label{"Size Explicit"}, Hidden{});
    field("sizeInitializedFromSprite", &S::sizeInitializedFromSprite, Label{"Size Initialized From Sprite"}, Hidden{});
    field("pivotExplicit", &S::pivotExplicit, Label{"Pivot Explicit"}, Hidden{});
};

// 範囲へ丸め、結び付けたスプライトがあれば貼り付けるテクスチャを外す。atlasFrames は格子の升の数まで。
WorldSpriteSettings NormalizeWorldSpriteSettings(WorldSpriteSettings settings);

class WorldSpriteComponent final : public IComponent, public IGeometryFitProvider {
public:
    using FacingMode = RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode;
    using AlphaMode = RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode;

    // Runtime-only mutations are composed against this complete value and
    // committed through ApplyRuntimeState. The component remains the
    // single owner of sanitization, change detection, and render dirtiness.
    struct RuntimeState {
        MATH::Vec2 size{1.0f, 1.0f};
        MATH::Vec3 tint{1.0f, 1.0f, 1.0f};
        MATH::Vec2 pivot{0.5f, 0.5f};
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        int transparentSortPriority = 0;
        FacingMode facing = FacingMode::CameraFacing;
        AlphaMode alphaMode = AlphaMode::Auto;
        bool visible = true;
        bool preserveAspect = true;
        bool sizeExplicit = false;
        bool pivotExplicit = false;
        bool flipX = false;
    };

    void Update(float dt) override;
    MATH::Vec4 GetUvRect() const noexcept;
    bool ResolveRenderProjection(const MATH::Vec3& worldScale, WorldSpriteRenderProjection& outProjection) const;
    bool BindSpriteAsset(const AssetGuid& guid, std::shared_ptr<const SPRITE::SpriteAssetData> asset,
                         std::string_view initialClip = {});
    const AssetGuid& GetMaterialAssetGuid() const noexcept { return materialAssetGuid_; }
    const std::shared_ptr<const SPRITE::SpriteMaterialBinding>& GetSpriteMaterialBinding() const noexcept {
        return materialBinding_;
    }
    const ASSETS::SHADER::ShaderParameterSlots& GetEffectiveMaterialParameterSlots() const noexcept {
        return effectiveMaterialParameterSlots_;
    }
    bool BindMaterialAsset(const AssetGuid& guid, std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding);
    bool RefreshMaterialBinding(std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding);
    void ClearMaterialAsset();
    bool IsMaterialEnabled() const noexcept { return materialEnabled_; }
    bool SetMaterialEnabled(bool enabled) noexcept;
    size_t GetMaterialParameterCount() const noexcept;
    bool TryGetMaterialParameterInfo(size_t index, ASSETS::SHADER::ShaderParameterDefinition& outInfo) const;
    bool HasMaterialParameterOverride(std::string_view key) const noexcept;
    bool SetMaterialParameter(std::string_view key, const BINDING::Value& value);
    bool TryGetMaterialParameter(std::string_view key, BINDING::Value& outValue) const noexcept;
    bool ResetMaterialParameter(std::string_view key);
    bool ResetMaterialParameters();
    uint64_t GetMaterialStateRevision() const noexcept { return materialStateRevision_; }
    void SetMaterialDiagnostic(std::string diagnostic) { materialDiagnostic_ = std::move(diagnostic); }
    const std::string& GetMaterialDiagnostic() const noexcept { return materialDiagnostic_; }
    bool PlayAnimation(std::string_view clip, bool restart = true);
    bool SetPaused(bool paused) noexcept;
    bool SetPlaybackSpeed(float speed) noexcept;
    bool RestartAnimation() noexcept;
    bool SeekAnimation(double timeMs) noexcept;
    const AssetGuid& GetSpriteAssetGuid() const noexcept { return settings_.spriteAssetGuid; }
    bool HasBoundSpriteAsset() const noexcept { return spritePlayer_.HasSpriteAsset(); }
    WorldSpriteAnimationState GetAnimationState() const noexcept;
    // Borrowed until the bound asset changes; no player mutation or asset reload.
    const SPRITE::SpriteClip* FindAnimationClip(std::string_view clipId) const noexcept;
    MATH::Vec2 GetEffectivePivot() const noexcept;
    bool GetFlipX() const noexcept { return settings_.flipX; }
    void SetFlipX(bool flipX) noexcept {
        RuntimeState state = GetRuntimeState();
        state.flipX = flipX;
        (void)ApplyRuntimeState(state);
    }
    std::string_view GetAnimationClip() const noexcept {
        return spritePlayer_.HasSpriteAsset() ? std::string_view(spritePlayer_.GetClipName())
                                              : std::string_view(settings_.animationClip);
    }
    bool IsLit() const noexcept { return settings_.lit; }
    bool CastsShadow() const noexcept { return settings_.castShadow; }
    bool ReceivesShadow() const noexcept { return settings_.receiveShadow; }
    float GetRoughness() const noexcept { return settings_.roughness; }
    float GetSpecular() const noexcept { return settings_.specular; }
    bool UsesPointSampling() const noexcept { return settings_.spriteAssetGuid.IsValid() || settings_.pointSampling; }
    bool IsCanvasSourceOnly() const noexcept { return canvasSourceOnly_; }
    void SetCanvasSourceOnly(bool value) {
        if (canvasSourceOnly_ == value)
            return;
        canvasSourceOnly_ = value;
        NotifyRenderStateDirty();
    }
    std::string_view GetTypeName() const override { return "WorldSpriteComponent"; }

    const AssetGuid& GetTextureAssetGuid() const noexcept { return settings_.textureAssetGuid; }

    void SetTextureAssetGuid(AssetGuid guid);

    bool IsVisible() const noexcept { return settings_.visible; }
    void SetVisible(bool visible) {
        RuntimeState state = GetRuntimeState();
        state.visible = visible;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec2& GetSize() const noexcept { return settings_.size; }
    void SetSize(MATH::Vec2 size) {
        RuntimeState state = GetRuntimeState();
        state.size = size;
        state.sizeExplicit = true;
        (void)ApplyRuntimeState(state);
        settings_.sizeInitializedFromSprite = true;
    }

    bool GetPreserveAspect() const noexcept { return settings_.preserveAspect; }
    void SetPreserveAspect(bool preserve) {
        RuntimeState state = GetRuntimeState();
        state.preserveAspect = preserve;
        (void)ApplyRuntimeState(state);
    }

    FacingMode GetFacingMode() const noexcept { return settings_.facing; }
    void SetFacingMode(FacingMode facing) {
        RuntimeState state = GetRuntimeState();
        state.facing = facing;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec3& GetTint() const noexcept { return settings_.tint; }
    void SetTint(MATH::Vec3 tint) {
        RuntimeState state = GetRuntimeState();
        state.tint = tint;
        (void)ApplyRuntimeState(state);
    }

    float GetOpacity() const noexcept { return settings_.opacity; }
    void SetOpacity(float opacity) {
        RuntimeState state = GetRuntimeState();
        state.opacity = opacity;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec2& GetPivot() const noexcept { return settings_.pivot; }
    void SetPivot(MATH::Vec2 pivot) {
        RuntimeState state = GetRuntimeState();
        state.pivot = pivot;
        state.pivotExplicit = true;
        (void)ApplyRuntimeState(state);
    }

    AlphaMode GetAlphaMode() const noexcept { return settings_.alphaMode; }
    void SetAlphaMode(AlphaMode mode) {
        RuntimeState state = GetRuntimeState();
        state.alphaMode = mode;
        (void)ApplyRuntimeState(state);
    }

    float GetAlphaCutoff() const noexcept { return settings_.alphaCutoff; }
    void SetAlphaCutoff(float cutoff) {
        RuntimeState state = GetRuntimeState();
        state.alphaCutoff = cutoff;
        (void)ApplyRuntimeState(state);
    }

    int GetTransparentSortPriority() const noexcept { return settings_.transparentSortPriority; }

    uint64_t GetStateRevision() const noexcept { return stateRevision_; }

    void SetTransparentSortPriority(int priority) {
        RuntimeState state = GetRuntimeState();
        state.transparentSortPriority = priority;
        (void)ApplyRuntimeState(state);
    }

    RuntimeState GetRuntimeState() const noexcept {
        RuntimeState state{};
        state.size = settings_.size;
        state.tint = settings_.tint;
        state.pivot = settings_.pivot;
        state.opacity = settings_.opacity;
        state.alphaCutoff = settings_.alphaCutoff;
        state.transparentSortPriority = settings_.transparentSortPriority;
        state.facing = settings_.facing;
        state.alphaMode = settings_.alphaMode;
        state.visible = settings_.visible;
        state.preserveAspect = settings_.preserveAspect;
        state.sizeExplicit = settings_.sizeExplicit;
        state.pivotExplicit = settings_.pivotExplicit;
        state.flipX = settings_.flipX;
        return state;
    }

    // Returns true only when the sanitized effective state changed. All
    // fields are assigned before the single dirty/revision notification.
    bool ApplyRuntimeState(const RuntimeState& state);

    bool QueryGeometryFit(GeometryFitDesc& outFit) const noexcept override {
        try {
            WorldSpriteRenderProjection projection{};
            if (!ResolveRenderProjection({1.0f, 1.0f, 1.0f}, projection) || projection.empty) {
                outFit = {};
                return false;
            }
            const MATH::Vec2 size = projection.size;
            outFit = {};
            outFit.shape = GeometryFitShape::Box;
            outFit.center = {(0.5f - projection.pivot.x) * size.x, (0.5f - projection.pivot.y) * size.y, 0.0f};
            outFit.size = {size.x, size.y, 0.02f};
            outFit.radius = 0.01f;
            outFit.height = size.y;
            return true;
        } catch (...) {
            outFit = {};
            return false;
        }
    }

    void Serialize(nlohmann::json& out) const override;

    void Deserialize(const nlohmann::json& in) override;

#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

private:
    void NotifyRenderStateDirty() {
        NUMERIC::AdvanceNonZero(stateRevision_);
        IComponent::NotifyRenderStateDirty();
    }

    void NotifyMaterialStateDirty() noexcept;
    void BumpMaterialStateRevision() noexcept;
    void SerializeMaterialState(nlohmann::json& out) const;
    void DeserializeMaterialState(const nlohmann::json& node);
    void BuildMaterialInspector(IInspectorBuilder& builder);

    // 保存と検査器に出す値。再生の状態は player が持つので、そこから写す。
    WorldSpriteSettings Snapshot() const;
    void ApplyInspectorEdit(IInspectorBuilder& builder, const WorldSpriteSettings& before, WorldSpriteSettings next);

    WorldSpriteSettings settings_{};
    WorldSpritePlayer spritePlayer_{};
    uint64_t stateRevision_ = 1u;
    AssetGuid materialAssetGuid_{};
    std::shared_ptr<const SPRITE::SpriteMaterialBinding> materialBinding_{};
    std::vector<ASSETS::SHADER::ShaderParameterOverride> materialOverrides_{};
    ASSETS::SHADER::ShaderParameterSlots effectiveMaterialParameterSlots_{};
    bool materialEnabled_ = false;
    uint64_t materialStateRevision_ = 1u;
    std::string materialDiagnostic_{};
    bool canvasSourceOnly_ = false;
};

} // namespace HIKARI
