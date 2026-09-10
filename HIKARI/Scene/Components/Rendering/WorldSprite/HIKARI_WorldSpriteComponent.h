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

        void Serialize(nlohmann::json& out) const override {
            out = {
                { "version", 1u },
                { WorldSpriteComponentPropertyIds::TextureAssetGuid,
                  textureAssetGuid_.value },
                { WorldSpriteComponentPropertyIds::Visible, visible_ },
                { WorldSpriteComponentPropertyIds::Size,
                  { size_.x, size_.y } },
                { WorldSpriteComponentPropertyIds::PreserveAspect,
                  preserveAspect_ },
                { WorldSpriteComponentPropertyIds::Facing,
                  FacingName(facing_) },
                { WorldSpriteComponentPropertyIds::Tint,
                  { tint_.x, tint_.y, tint_.z } },
                { WorldSpriteComponentPropertyIds::Opacity, opacity_ },
                { WorldSpriteComponentPropertyIds::Pivot,
                  { pivot_.x, pivot_.y } },
                { WorldSpriteComponentPropertyIds::AlphaMode,
                  AlphaModeName(alphaMode_) },
                { WorldSpriteComponentPropertyIds::AlphaCutoff,
                  alphaCutoff_ },
                { WorldSpriteComponentPropertyIds::TransparentSortPriority,
                  transparentSortPriority_ }
            };
        }

        void Deserialize(const nlohmann::json& in) override {
            textureAssetGuid_.value = JSONREAD::StringMemberOr(
                in,
                WorldSpriteComponentPropertyIds::TextureAssetGuid,
                {});
            visible_ = JSONREAD::BooleanMemberOr(
                in,
                WorldSpriteComponentPropertyIds::Visible,
                true);
            if (const nlohmann::json* value = JSONREAD::FindMember(
                    in, WorldSpriteComponentPropertyIds::Size)) {
                size_ = SanitizeSize(JSONREAD::Vec2Or(*value, size_));
            }
            preserveAspect_ = JSONREAD::BooleanMemberOr(
                in,
                WorldSpriteComponentPropertyIds::PreserveAspect,
                true);
            facing_ = ParseFacing(JSONREAD::StringMemberOr(
                in,
                WorldSpriteComponentPropertyIds::Facing,
                "CameraFacing"));
            if (const nlohmann::json* value = JSONREAD::FindMember(
                    in, WorldSpriteComponentPropertyIds::Tint)) {
                tint_ = SanitizeColor(JSONREAD::Vec3Or(*value, tint_));
            }
            opacity_ = ClampFinite(JSONREAD::NumberMemberOr(
                in,
                WorldSpriteComponentPropertyIds::Opacity,
                1.0f), 1.0f, 0.0f, 1.0f);
            if (const nlohmann::json* value = JSONREAD::FindMember(
                    in, WorldSpriteComponentPropertyIds::Pivot)) {
                pivot_ = SanitizePivot(JSONREAD::Vec2Or(*value, pivot_));
            }
            alphaMode_ = ParseAlphaMode(JSONREAD::StringMemberOr(
                in,
                WorldSpriteComponentPropertyIds::AlphaMode,
                "Auto"));
            alphaCutoff_ = ClampFinite(JSONREAD::NumberMemberOr(
                in,
                WorldSpriteComponentPropertyIds::AlphaCutoff,
                0.5f), 0.5f, 0.0f, 1.0f);
            transparentSortPriority_ = std::clamp(
                JSONREAD::IntegerMemberOr(
                    in,
                    WorldSpriteComponentPropertyIds::TransparentSortPriority,
                    0),
                -32768,
                32767);
            NotifyRenderStateDirty();
        }

        void BuildInspector(IInspectorBuilder& builder) override {
            const std::string textureLabel = TEXT::Localize(
                "ui.inspector.worldSprite.texture");
            const std::string visibleLabel = TEXT::Localize(
                "ui.inspector.worldSprite.visible");
            const std::string sizeLabel = TEXT::Localize(
                "ui.inspector.worldSprite.size");
            const std::string preserveAspectLabel = TEXT::Localize(
                "ui.inspector.worldSprite.preserveAspect");
            const std::string facingLabel = TEXT::Localize(
                "ui.inspector.worldSprite.facing");
            const std::string tintLabel = TEXT::Localize(
                "ui.inspector.worldSprite.tint");
            const std::string opacityLabel = TEXT::Localize(
                "ui.inspector.worldSprite.opacity");
            const std::string pivotLabel = TEXT::Localize(
                "ui.inspector.worldSprite.pivot");
            const std::string alphaModeLabel = TEXT::Localize(
                "ui.inspector.worldSprite.alphaMode");
            const std::string alphaCutoffLabel = TEXT::Localize(
                "ui.inspector.worldSprite.alphaCutoff");
            const std::string sortPriorityLabel = TEXT::Localize(
                "ui.inspector.worldSprite.sortPriority");
            const std::string textureHelp = TEXT::Localize(
                "ui.inspector.worldSprite.textureHelp");
            const std::string visibleHelp = TEXT::Localize(
                "ui.inspector.worldSprite.visibleHelp");
            const std::string sizeHelp = TEXT::Localize(
                "ui.inspector.worldSprite.sizeHelp");
            const std::string preserveAspectHelp = TEXT::Localize(
                "ui.inspector.worldSprite.preserveAspectHelp");
            const std::string facingHelp = TEXT::Localize(
                "ui.inspector.worldSprite.facingHelp");
            const std::string tintHelp = TEXT::Localize(
                "ui.inspector.worldSprite.tintHelp");
            const std::string opacityHelp = TEXT::Localize(
                "ui.inspector.worldSprite.opacityHelp");
            const std::string pivotHelp = TEXT::Localize(
                "ui.inspector.worldSprite.pivotHelp");
            const std::string alphaModeHelp = TEXT::Localize(
                "ui.inspector.worldSprite.alphaModeHelp");
            const std::string alphaCutoffHelp = TEXT::Localize(
                "ui.inspector.worldSprite.alphaCutoffHelp");
            const std::string sortPriorityHelp = TEXT::Localize(
                "ui.inspector.worldSprite.sortPriorityHelp");
            const auto field = [](const char* id, std::string_view label,
                                   const char* description) {
                return AuthoringFieldDescriptor{
                    id, label, description
                };
            };

            bool changed = false;
            changed = builder.AssetIdPicker(
                field(WorldSpriteComponentPropertyIds::TextureAssetGuid,
                    textureLabel,
                    textureHelp.c_str()),
                AssetType::Texture,
                textureAssetGuid_.value) || changed;
            changed = builder.Bool(
                field(WorldSpriteComponentPropertyIds::Visible,
                    visibleLabel,
                    visibleHelp.c_str()),
                visible_) || changed;
            changed = builder.Vec2(
                field(WorldSpriteComponentPropertyIds::Size,
                    sizeLabel,
                    sizeHelp.c_str()),
                size_.x,
                size_.y) || changed;
            changed = builder.Bool(
                field(WorldSpriteComponentPropertyIds::PreserveAspect,
                    preserveAspectLabel,
                    preserveAspectHelp.c_str()),
                preserveAspect_) || changed;

            const std::array facingLabels{
                TEXT::Localize("ui.inspector.worldSprite.fixed"),
                TEXT::Localize("ui.inspector.worldSprite.cameraFacing"),
                TEXT::Localize("ui.inspector.worldSprite.fixedYAxis"),
            };
            const std::array facingChoices{
                AuthoringChoice{ "Fixed", facingLabels[0] },
                AuthoringChoice{ "CameraFacing", facingLabels[1] },
                AuthoringChoice{ "FixedYAxis", facingLabels[2] },
            };
            int facingIndex = static_cast<int>(facing_);
            if (builder.Choice(
                    field(WorldSpriteComponentPropertyIds::Facing,
                        facingLabel,
                        facingHelp.c_str()),
                    facingIndex,
                    facingChoices)) {
                facing_ = static_cast<FacingMode>(std::clamp(
                    facingIndex, 0, 2));
                changed = true;
            }
            changed = builder.Color3(
                field(WorldSpriteComponentPropertyIds::Tint,
                    tintLabel,
                    tintHelp.c_str()),
                tint_.x,
                tint_.y,
                tint_.z) || changed;
            changed = builder.FloatRange(
                field(WorldSpriteComponentPropertyIds::Opacity,
                    opacityLabel,
                    opacityHelp.c_str()),
                opacity_,
                0.0f,
                1.0f,
                0.01f) || changed;

            if (builder.Section(
                    TEXT::Localize("ui.inspector.worldSprite.advanced"),
                    false)) {
                changed = builder.Vec2(
                    field(WorldSpriteComponentPropertyIds::Pivot,
                        pivotLabel,
                        pivotHelp.c_str()),
                    pivot_.x,
                    pivot_.y) || changed;
                const std::array alphaLabels{
                    TEXT::Localize("ui.inspector.worldSprite.alphaAuto"),
                    TEXT::Localize("ui.inspector.worldSprite.alphaOpaque"),
                    TEXT::Localize("ui.inspector.worldSprite.alphaMask"),
                    TEXT::Localize("ui.inspector.worldSprite.alphaBlend"),
                };
                const std::array alphaChoices{
                    AuthoringChoice{ "Auto", alphaLabels[0] },
                    AuthoringChoice{ "Opaque", alphaLabels[1] },
                    AuthoringChoice{ "Mask", alphaLabels[2] },
                    AuthoringChoice{ "Blend", alphaLabels[3] },
                };
                int alphaIndex = static_cast<int>(alphaMode_);
                if (builder.Choice(
                        field(WorldSpriteComponentPropertyIds::AlphaMode,
                            alphaModeLabel,
                            alphaModeHelp.c_str()),
                        alphaIndex,
                        alphaChoices)) {
                    alphaMode_ = static_cast<AlphaMode>(std::clamp(
                        alphaIndex, 0, 3));
                    changed = true;
                }
                if (alphaMode_ == AlphaMode::Mask) {
                    changed = builder.FloatRange(
                        field(WorldSpriteComponentPropertyIds::AlphaCutoff,
                            alphaCutoffLabel,
                            alphaCutoffHelp.c_str()),
                        alphaCutoff_,
                        0.0f,
                        1.0f,
                        0.01f) || changed;
                }
                changed = builder.Int(
                    field(
                        WorldSpriteComponentPropertyIds::
                            TransparentSortPriority,
                        sortPriorityLabel,
                        sortPriorityHelp.c_str()),
                    transparentSortPriority_) || changed;
            }

            size_ = SanitizeSize(size_);
            pivot_ = SanitizePivot(pivot_);
            tint_ = SanitizeColor(tint_);
            opacity_ = ClampFinite(opacity_, 1.0f, 0.0f, 1.0f);
            alphaCutoff_ = ClampFinite(
                alphaCutoff_, 0.5f, 0.0f, 1.0f);
            transparentSortPriority_ = std::clamp(
                transparentSortPriority_, -32768, 32767);
            if (changed) {
                NotifyRenderStateDirty();
            }
        }

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
