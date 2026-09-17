#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/HIKARI_AssetTypes.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Geometry/HIKARI_GeometryFitProvider.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    namespace WorldSpriteComponentPropertyIds {
        inline constexpr char TextureAssetGuid[] = "textureAssetGuid";
        inline constexpr char Visible[] = "visible";
        inline constexpr char Size[] = "size";
        inline constexpr char PreserveAspect[] = "preserveAspect";
        inline constexpr char Facing[] = "facing";
        inline constexpr char Tint[] = "tint";
        inline constexpr char Opacity[] = "opacity";
        inline constexpr char Pivot[] = "pivot";
        inline constexpr char AlphaMode[] = "alphaMode";
        inline constexpr char AlphaCutoff[] = "alphaCutoff";
        inline constexpr char TransparentSortPriority[] =
            "transparentSortPriority";
    }

    class WorldSpriteComponent final :
        public IComponent,
        public IGeometryFitProvider {
    public:
        using FacingMode =
            RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode;
        using AlphaMode =
            RENDER3D::WORLD_SPRITES::WorldSpriteAlphaMode;

        // Runtime-only mutations are composed against this complete value and
        // committed through ApplyRuntimeState. The component remains the
        // single owner of sanitization, change detection, and render dirtiness.
        struct RuntimeState {
            MATH::Vec2 size{ 1.0f, 1.0f };
            MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
            MATH::Vec2 pivot{ 0.5f, 0.5f };
            float opacity = 1.0f;
            float alphaCutoff = 0.5f;
            int transparentSortPriority = 0;
            FacingMode facing = FacingMode::CameraFacing;
            AlphaMode alphaMode = AlphaMode::Auto;
            bool visible = true;
            bool preserveAspect = true;
        };

        void Update(float dt) override;
        MATH::Vec4 GetUvRect() const noexcept;
        bool IsLit() const noexcept { return lit_; }
        bool CastsShadow() const noexcept { return castShadow_; }
        bool ReceivesShadow() const noexcept { return receiveShadow_; }
        float GetRoughness() const noexcept { return roughness_; }
        float GetSpecular() const noexcept { return specular_; }
        bool UsesPointSampling() const noexcept { return pointSampling_; }
        bool IsCanvasSourceOnly() const noexcept { return canvasSourceOnly_; }
        void SetCanvasSourceOnly(bool value) { if (canvasSourceOnly_ == value) return; canvasSourceOnly_ = value; NotifyRenderStateDirty(); }
        std::string_view GetTypeName() const override {
            return "WorldSpriteComponent";
        }

        const AssetGuid& GetTextureAssetGuid() const noexcept {
            return textureAssetGuid_;
        }

        void SetTextureAssetGuid(AssetGuid guid) {
            if (textureAssetGuid_ == guid) {
                return;
            }
            textureAssetGuid_ = std::move(guid);
            NotifyRenderStateDirty();
        }

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
            (void)ApplyRuntimeState(state);
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

        int GetTransparentSortPriority() const noexcept {
            return transparentSortPriority_;
        }

        uint64_t GetStateRevision() const noexcept {
            return stateRevision_;
        }

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
            return state;
        }

        // Returns true only when the sanitized effective state changed. All
        // fields are assigned before the single dirty/revision notification.
        bool ApplyRuntimeState(RuntimeState state) {
            state.size = SanitizeSize(state.size);
            state.tint = SanitizeColor(state.tint);
            state.pivot = SanitizePivot(state.pivot);
            state.opacity = ClampFinite(
                state.opacity, 1.0f, 0.0f, 1.0f);
            state.alphaCutoff = ClampFinite(
                state.alphaCutoff, 0.5f, 0.0f, 1.0f);
            state.transparentSortPriority = std::clamp(
                state.transparentSortPriority, -32768, 32767);
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
            NotifyRenderStateDirty();
            return true;
        }

        bool QueryGeometryFit(
            GeometryFitDesc& outFit) const noexcept override {
            const MATH::Vec2 size = SanitizeSize(size_);
            outFit = {};
            outFit.shape = GeometryFitShape::Box;
            outFit.center = {
                (0.5f - pivot_.x) * size.x,
                (0.5f - pivot_.y) * size.y,
                0.0f
            };
            outFit.size = { size.x, size.y, 0.02f };
            outFit.radius = 0.01f;
            outFit.height = size.y;
            return true;
        }

        void Serialize(nlohmann::json& out) const override;

        void Deserialize(const nlohmann::json& in) override;

        void BuildInspector(IInspectorBuilder& builder) override;

    private:
        static float ClampFinite(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            return std::clamp(
                std::isfinite(value) ? value : fallback,
                minimum,
                maximum);
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
            switch (mode) {
            case FacingMode::Fixed:
            case FacingMode::CameraFacing:
            case FacingMode::FixedYAxis:
                return mode;
            default:
                return FacingMode::CameraFacing;
            }
        }

        static AlphaMode SanitizeAlphaMode(AlphaMode mode) noexcept {
            switch (mode) {
            case AlphaMode::Auto:
            case AlphaMode::Opaque:
            case AlphaMode::Mask:
            case AlphaMode::Blend:
                return mode;
            default:
                return AlphaMode::Auto;
            }
        }

        bool HasSameRuntimeState(const RuntimeState& state) const noexcept {
            return size_.x == state.size.x && size_.y == state.size.y &&
                tint_.x == state.tint.x && tint_.y == state.tint.y &&
                tint_.z == state.tint.z && pivot_.x == state.pivot.x &&
                pivot_.y == state.pivot.y && opacity_ == state.opacity &&
                alphaCutoff_ == state.alphaCutoff &&
                transparentSortPriority_ ==
                    state.transparentSortPriority &&
                facing_ == state.facing && alphaMode_ == state.alphaMode &&
                visible_ == state.visible &&
                preserveAspect_ == state.preserveAspect;
        }

        static const char* FacingName(FacingMode mode) noexcept {
            switch (mode) {
            case FacingMode::Fixed: return "Fixed";
            case FacingMode::FixedYAxis: return "FixedYAxis";
            case FacingMode::CameraFacing:
            default: return "CameraFacing";
            }
        }

        static FacingMode ParseFacing(std::string_view value) noexcept {
            if (value == "Fixed") return FacingMode::Fixed;
            if (value == "FixedYAxis") return FacingMode::FixedYAxis;
            return FacingMode::CameraFacing;
        }

        static const char* AlphaModeName(AlphaMode mode) noexcept {
            switch (mode) {
            case AlphaMode::Opaque: return "Opaque";
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
            case AlphaMode::Auto:
            default: return "Auto";
            }
        }

        static AlphaMode ParseAlphaMode(std::string_view value) noexcept {
            if (value == "Opaque") return AlphaMode::Opaque;
            if (value == "Mask") return AlphaMode::Mask;
            if (value == "Blend") return AlphaMode::Blend;
            return AlphaMode::Auto;
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

        void SanitizePresentation() noexcept;
        int atlasColumns_ = 1, atlasRows_ = 1, atlasFrames_ = 1;
        float framesPerSecond_ = 0, roughness_ = 0.85f, specular_ = 0.25f;
        double animationSeconds_ = 0;
        bool lit_ = false, pointSampling_ = false;
        bool castShadow_ = true, receiveShadow_ = true;
        AssetGuid textureAssetGuid_{};
        MATH::Vec2 size_{ 1.0f, 1.0f };
        MATH::Vec3 tint_{ 1.0f, 1.0f, 1.0f };
        MATH::Vec2 pivot_{ 0.5f, 0.5f };
        float opacity_ = 1.0f;
        float alphaCutoff_ = 0.5f;
        int transparentSortPriority_ = 0;
        FacingMode facing_ = FacingMode::CameraFacing;
        AlphaMode alphaMode_ = AlphaMode::Auto;
        bool visible_ = true;
        bool preserveAspect_ = true;
        uint64_t stateRevision_ = 1u;
    private:
        bool canvasSourceOnly_ = false;
    };

} // namespace HIKARI
