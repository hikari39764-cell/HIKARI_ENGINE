#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Sprite/HIKARI_SpriteAsset.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpritePlayer.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

namespace WorldSpriteComponentPropertyIds {
inline constexpr char TextureAssetGuid[] = "textureAssetGuid";
inline constexpr char SpriteAssetGuid[] = "spriteAssetGuid";
inline constexpr char AnimationClip[] = "animationClip";
inline constexpr char AnimationSpeed[] = "animationSpeed";
inline constexpr char AnimationPaused[] = "animationPaused";
inline constexpr char SizeExplicit[] = "sizeExplicit";
inline constexpr char PivotExplicit[] = "pivotExplicit";
inline constexpr char Visible[] = "visible";
inline constexpr char FlipX[] = "flipX";
inline constexpr char Size[] = "size";
inline constexpr char PreserveAspect[] = "preserveAspect";
inline constexpr char Facing[] = "facing";
inline constexpr char Tint[] = "tint";
inline constexpr char Opacity[] = "opacity";
inline constexpr char Pivot[] = "pivot";
inline constexpr char AlphaMode[] = "alphaMode";
inline constexpr char AlphaCutoff[] = "alphaCutoff";
inline constexpr char TransparentSortPriority[] = "transparentSortPriority";
} // namespace WorldSpriteComponentPropertyIds

struct WorldSpriteRenderProjection {
    AssetId textureAssetId{};
    AssetId normalTextureAssetId{};
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

class WorldSpriteComponent final : public IComponent, public IGeometryFitProvider {
public:
    using FacingMode = RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode;
    using AlphaMode = RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode;
    // シーンに保存する名前。
    static constexpr TEXT::EnumName<FacingMode> kFacingNames[] = {
        {FacingMode::Fixed, "Fixed"},
        {FacingMode::CameraFacing, "CameraFacing"},
        {FacingMode::FixedYAxis, "FixedYAxis"},
    };
    static constexpr TEXT::EnumName<AlphaMode> kAlphaModeNames[] = {
        {AlphaMode::Auto, "Auto"},
        {AlphaMode::Opaque, "Opaque"},
        {AlphaMode::Mask, "Mask"},
        {AlphaMode::Blend, "Blend"},
    };

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
    const AssetGuid& GetSpriteAssetGuid() const noexcept { return spriteAssetGuid_; }
    bool HasBoundSpriteAsset() const noexcept { return spritePlayer_.HasSpriteAsset(); }
    WorldSpriteAnimationState GetAnimationState() const noexcept;
    // Borrowed until the bound asset changes; no player mutation or asset reload.
    const SPRITE::SpriteClip* FindAnimationClip(std::string_view clipId) const noexcept;
    MATH::Vec2 GetEffectivePivot() const noexcept;
    bool GetFlipX() const noexcept { return flipX_; }
    void SetFlipX(bool flipX) noexcept {
        RuntimeState state = GetRuntimeState();
        state.flipX = flipX;
        (void)ApplyRuntimeState(state);
    }
    std::string_view GetAnimationClip() const noexcept {
        return spritePlayer_.HasSpriteAsset() ? std::string_view(spritePlayer_.GetClipName())
                                              : std::string_view(animationClip_);
    }
    bool IsLit() const noexcept { return lit_; }
    bool CastsShadow() const noexcept { return castShadow_; }
    bool ReceivesShadow() const noexcept { return receiveShadow_; }
    float GetRoughness() const noexcept { return roughness_; }
    float GetSpecular() const noexcept { return specular_; }
    bool UsesPointSampling() const noexcept { return spriteAssetGuid_.IsValid() || pointSampling_; }
    bool IsCanvasSourceOnly() const noexcept { return canvasSourceOnly_; }
    void SetCanvasSourceOnly(bool value) {
        if (canvasSourceOnly_ == value)
            return;
        canvasSourceOnly_ = value;
        NotifyRenderStateDirty();
    }
    std::string_view GetTypeName() const override { return "WorldSpriteComponent"; }

    const AssetGuid& GetTextureAssetGuid() const noexcept { return textureAssetGuid_; }

    void SetTextureAssetGuid(AssetGuid guid);

    bool IsVisible() const noexcept { return visible_; }
    void SetVisible(bool visible) {
        RuntimeState state = GetRuntimeState();
        state.visible = visible;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec2& GetSize() const noexcept { return size_; }
    void SetSize(MATH::Vec2 size) {
        RuntimeState state = GetRuntimeState();
        state.size = size;
        state.sizeExplicit = true;
        (void)ApplyRuntimeState(state);
        sizeInitializedFromSprite_ = true;
    }

    bool GetPreserveAspect() const noexcept { return preserveAspect_; }
    void SetPreserveAspect(bool preserve) {
        RuntimeState state = GetRuntimeState();
        state.preserveAspect = preserve;
        (void)ApplyRuntimeState(state);
    }

    FacingMode GetFacingMode() const noexcept { return facing_; }
    void SetFacingMode(FacingMode facing) {
        RuntimeState state = GetRuntimeState();
        state.facing = facing;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec3& GetTint() const noexcept { return tint_; }
    void SetTint(MATH::Vec3 tint) {
        RuntimeState state = GetRuntimeState();
        state.tint = tint;
        (void)ApplyRuntimeState(state);
    }

    float GetOpacity() const noexcept { return opacity_; }
    void SetOpacity(float opacity) {
        RuntimeState state = GetRuntimeState();
        state.opacity = opacity;
        (void)ApplyRuntimeState(state);
    }

    const MATH::Vec2& GetPivot() const noexcept { return pivot_; }
    void SetPivot(MATH::Vec2 pivot) {
        RuntimeState state = GetRuntimeState();
        state.pivot = pivot;
        state.pivotExplicit = true;
        (void)ApplyRuntimeState(state);
    }

    AlphaMode GetAlphaMode() const noexcept { return alphaMode_; }
    void SetAlphaMode(AlphaMode mode) {
        RuntimeState state = GetRuntimeState();
        state.alphaMode = mode;
        (void)ApplyRuntimeState(state);
    }

    float GetAlphaCutoff() const noexcept { return alphaCutoff_; }
    void SetAlphaCutoff(float cutoff) {
        RuntimeState state = GetRuntimeState();
        state.alphaCutoff = cutoff;
        (void)ApplyRuntimeState(state);
    }

    int GetTransparentSortPriority() const noexcept { return transparentSortPriority_; }

    uint64_t GetStateRevision() const noexcept { return stateRevision_; }

    void SetTransparentSortPriority(int priority) {
        RuntimeState state = GetRuntimeState();
        state.transparentSortPriority = priority;
        (void)ApplyRuntimeState(state);
    }

    RuntimeState GetRuntimeState() const noexcept {
        RuntimeState state{};
        state.size = size_;
        state.tint = tint_;
        state.pivot = pivot_;
        state.opacity = opacity_;
        state.alphaCutoff = alphaCutoff_;
        state.transparentSortPriority = transparentSortPriority_;
        state.facing = facing_;
        state.alphaMode = alphaMode_;
        state.visible = visible_;
        state.preserveAspect = preserveAspect_;
        state.sizeExplicit = sizeExplicit_;
        state.pivotExplicit = pivotExplicit_;
        state.flipX = flipX_;
        return state;
    }

    // Returns true only when the sanitized effective state changed. All
    // fields are assigned before the single dirty/revision notification.
    bool ApplyRuntimeState(RuntimeState state) {
        state.size = SanitizeSize(state.size);
        state.tint = SanitizeColor(state.tint);
        state.pivot = SanitizePivot(state.pivot);
        state.opacity = ClampFinite(state.opacity, 1.0f, 0.0f, 1.0f);
        state.alphaCutoff = ClampFinite(state.alphaCutoff, 0.5f, 0.0f, 1.0f);
        state.transparentSortPriority = std::clamp(state.transparentSortPriority, -32768, 32767);
        state.facing = SanitizeFacing(state.facing);
        state.alphaMode = SanitizeAlphaMode(state.alphaMode);
        if (HasSameRuntimeState(state)) {
            return false;
        }

        size_ = state.size;
        tint_ = state.tint;
        pivot_ = state.pivot;
        opacity_ = state.opacity;
        alphaCutoff_ = state.alphaCutoff;
        transparentSortPriority_ = state.transparentSortPriority;
        facing_ = state.facing;
        alphaMode_ = state.alphaMode;
        visible_ = state.visible;
        preserveAspect_ = state.preserveAspect;
        sizeExplicit_ = state.sizeExplicit;
        pivotExplicit_ = state.pivotExplicit;
        flipX_ = state.flipX;
        sizeInitializedFromSprite_ = sizeInitializedFromSprite_ || state.sizeExplicit;
        NotifyRenderStateDirty();
        return true;
    }

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

    void BuildInspector(IInspectorBuilder& builder) override;

private:
    static float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept {
        return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
    }

    static MATH::Vec2 SanitizeSize(MATH::Vec2 value) noexcept {
        value.x = ClampFinite(value.x, 1.0f, 0.001f, 100000.0f);
        value.y = ClampFinite(value.y, 1.0f, 0.001f, 100000.0f);
        return value;
    }

    static MATH::Vec2 SanitizePivot(MATH::Vec2 value) noexcept {
        value.x = ClampFinite(value.x, 0.5f, 0.0f, 1.0f);
        value.y = ClampFinite(value.y, 0.5f, 0.0f, 1.0f);
        return value;
    }

    static MATH::Vec3 SanitizeColor(MATH::Vec3 value) noexcept {
        value.x = ClampFinite(value.x, 1.0f, 0.0f, 16.0f);
        value.y = ClampFinite(value.y, 1.0f, 0.0f, 16.0f);
        value.z = ClampFinite(value.z, 1.0f, 0.0f, 16.0f);
        return value;
    }

    static FacingMode SanitizeFacing(FacingMode mode) noexcept {
        return TEXT::IsNamedEnum(kFacingNames, mode) ? mode : FacingMode::CameraFacing;
    }

    static AlphaMode SanitizeAlphaMode(AlphaMode mode) noexcept {
        return TEXT::IsNamedEnum(kAlphaModeNames, mode) ? mode : AlphaMode::Auto;
    }

    bool HasSameRuntimeState(const RuntimeState& state) const noexcept {
        return size_.x == state.size.x && size_.y == state.size.y && tint_.x == state.tint.x &&
               tint_.y == state.tint.y && tint_.z == state.tint.z && pivot_.x == state.pivot.x &&
               pivot_.y == state.pivot.y && opacity_ == state.opacity && alphaCutoff_ == state.alphaCutoff &&
               transparentSortPriority_ == state.transparentSortPriority && facing_ == state.facing &&
               alphaMode_ == state.alphaMode && visible_ == state.visible && preserveAspect_ == state.preserveAspect &&
               sizeExplicit_ == state.sizeExplicit && pivotExplicit_ == state.pivotExplicit && flipX_ == state.flipX;
    }

    void NotifyRenderStateDirty() {
        ++stateRevision_;
        if (stateRevision_ == 0u) {
            stateRevision_ = 1u;
        }
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void NotifyMaterialStateDirty() noexcept;
    void BumpMaterialStateRevision() noexcept;
    void SerializeMaterialState(nlohmann::json& out) const;
    void DeserializeMaterialState(const nlohmann::json& node);
    void BuildMaterialInspector(IInspectorBuilder& builder);

    void SanitizePresentation() noexcept;
    int atlasColumns_ = 1, atlasRows_ = 1, atlasFrames_ = 1;
    float framesPerSecond_ = 0, roughness_ = 0.85f, specular_ = 0.25f;
    bool lit_ = false, pointSampling_ = false;
    bool castShadow_ = true, receiveShadow_ = true;
    AssetGuid textureAssetGuid_{};
    AssetGuid spriteAssetGuid_{};
    std::string animationClip_{};
    WorldSpritePlayer spritePlayer_{};
    MATH::Vec2 size_{1.0f, 1.0f};
    MATH::Vec3 tint_{1.0f, 1.0f, 1.0f};
    MATH::Vec2 pivot_{0.5f, 0.5f};
    float opacity_ = 1.0f;
    float alphaCutoff_ = 0.5f;
    int transparentSortPriority_ = 0;
    FacingMode facing_ = FacingMode::CameraFacing;
    AlphaMode alphaMode_ = AlphaMode::Auto;
    bool visible_ = true;
    bool preserveAspect_ = true;
    bool sizeExplicit_ = false;
    bool pivotExplicit_ = false;
    bool sizeInitializedFromSprite_ = false;
    bool flipX_ = false;
    uint64_t stateRevision_ = 1u;
    AssetGuid materialAssetGuid_{};
    std::shared_ptr<const SPRITE::SpriteMaterialBinding> materialBinding_{};
    std::vector<ASSETS::SHADER::ShaderParameterOverride> materialOverrides_{};
    ASSETS::SHADER::ShaderParameterSlots effectiveMaterialParameterSlots_{};
    bool materialEnabled_ = false;
    uint64_t materialStateRevision_ = 1u;
    std::string materialDiagnostic_{};

private:
    bool canvasSourceOnly_ = false;
};

} // namespace HIKARI
