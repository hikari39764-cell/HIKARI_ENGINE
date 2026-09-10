#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectSceneCaptureVersion = 1u;
    inline constexpr uint32_t kGameProjectSceneCaptureDescV1Size = 48u;
    inline constexpr uint32_t
        kGameProjectSceneCaptureMaterialBindingDescV1Size = 152u;
    inline constexpr uint32_t
        kGameProjectSceneCaptureMaterialRegionBindingVersion = 1u;
    inline constexpr uint32_t
        kGameProjectSceneCaptureMaterialRegionBindingDescV1Size = 176u;
    inline constexpr uint32_t kGameProjectSceneCaptureSnapshotV1Size = 64u;
    inline constexpr uint32_t
        kGameProjectSceneCaptureMaterialSlotNameCapacity = 96u;
    inline constexpr uint32_t kGameProjectSceneCaptureMinimumExtent = 64u;
    inline constexpr uint32_t kGameProjectSceneCaptureMaximumExtent = 2048u;
    // A World may still own a small number of genuinely independent views.
    // Surfaces that share a camera should prefer region bindings on one
    // capture instead of consuming additional targets.
    inline constexpr uint32_t kGameProjectSceneCaptureMaximumActiveCount = 4u;
    inline constexpr uint32_t kGameProjectInvalidMaterialIndex = 0xffffffffu;

    enum class GameProjectSceneCaptureMaterialBindingFlag : uint32_t {
        None = 0u,
        FlipTextureX = 1u << 0u,
        FlipTextureY = 1u << 1u,
        ProtectReadability = 1u << 2u,
    };

    constexpr uint32_t GameProjectSceneCaptureMaterialBindingFlags(
        GameProjectSceneCaptureMaterialBindingFlag flags) noexcept {
        return static_cast<uint32_t>(flags);
    }

    inline constexpr uint32_t
        kGameProjectSceneCaptureMaterialBindingKnownFlags =
            GameProjectSceneCaptureMaterialBindingFlags(
                GameProjectSceneCaptureMaterialBindingFlag::FlipTextureX) |
            GameProjectSceneCaptureMaterialBindingFlags(
                GameProjectSceneCaptureMaterialBindingFlag::FlipTextureY) |
            GameProjectSceneCaptureMaterialBindingFlags(
                GameProjectSceneCaptureMaterialBindingFlag::ProtectReadability);

    struct GameProjectSceneCaptureHandle {
        uint64_t value = 0u;
        uint64_t epoch = 0u;

        bool IsValid() const noexcept {
            return value != 0u && epoch != 0u;
        }

        bool operator==(
            const GameProjectSceneCaptureHandle&) const noexcept = default;
    };

    enum class GameProjectSceneCaptureFlag : uint32_t {
        None = 0u,
        Enabled = 1u << 0u,
        RenderSky = 1u << 1u,
        RenderTransparent = 1u << 2u,
        RenderVfx = 1u << 3u,
    };

    constexpr GameProjectSceneCaptureFlag operator|(
        GameProjectSceneCaptureFlag lhs,
        GameProjectSceneCaptureFlag rhs) noexcept {
        return static_cast<GameProjectSceneCaptureFlag>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs));
    }

    constexpr uint32_t GameProjectSceneCaptureFlags(
        GameProjectSceneCaptureFlag flags) noexcept {
        return static_cast<uint32_t>(flags);
    }

    enum class GameProjectSceneCaptureStatus : uint32_t {
        Disabled = 0u,
        Pending,
        Ready,
        Failed,
    };

    enum class GameProjectSceneCaptureFailure : uint32_t {
        None = 0u,
        InvalidRequest,
        InvalidCamera,
        RenderTargetUnavailable,
        RenderFailed,
        MaterialBindingFailed,
        WorldClosing,
    };

    // A World owns a small bounded set of secondary captures. Camera handles
    // are resolved every frame and never extend object lifetime. The renderer
    // owns every target, descriptor, and GPU synchronization resource.
    struct GameProjectSceneCaptureDesc {
        uint32_t structSize = kGameProjectSceneCaptureDescV1Size;
        uint32_t version = kGameProjectSceneCaptureVersion;
        GameProjectObjectHandle cameraObject{};
        uint32_t width = 960u;
        uint32_t height = 640u;
        uint32_t flags = GameProjectSceneCaptureFlags(
            GameProjectSceneCaptureFlag::Enabled |
            GameProjectSceneCaptureFlag::RenderSky |
            GameProjectSceneCaptureFlag::RenderTransparent |
            GameProjectSceneCaptureFlag::RenderVfx);
        uint32_t reserved[3] = { 0u, 0u, 0u };
    };

    // The binding resolves an imported material by exact source material
    // name. fallbackMaterialIndex is used only when the name is empty. Flags
    // correct the imported display surface's UV orientation without mutating
    // model data. The capture texture remains engine-owned and is never
    // exposed to the GameProject as a descriptor or native resource.
    struct GameProjectSceneCaptureMaterialBindingDesc {
        uint32_t structSize =
            kGameProjectSceneCaptureMaterialBindingDescV1Size;
        uint32_t version = kGameProjectSceneCaptureVersion;
        GameProjectSceneCaptureHandle capture{};
        GameProjectObjectHandle modelObject{};
        char materialSlotName[
            kGameProjectSceneCaptureMaterialSlotNameCapacity]{};
        uint32_t fallbackMaterialIndex = kGameProjectInvalidMaterialIndex;
        uint32_t flags = GameProjectSceneCaptureMaterialBindingFlags(
            GameProjectSceneCaptureMaterialBindingFlag::None);
        uint32_t reserved[2] = { 0u, 0u };
    };

    struct GameProjectSceneCaptureSourceRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };

    // Binds a normalized region of one capture to a display material. This
    // allows several cabinet surfaces to share one rendered scene feed. The
    // optional quarter-turn rotation is applied after FlipTextureX/Y.
    struct GameProjectSceneCaptureMaterialRegionBindingDesc {
        uint32_t structSize =
            kGameProjectSceneCaptureMaterialRegionBindingDescV1Size;
        uint32_t version =
            kGameProjectSceneCaptureMaterialRegionBindingVersion;
        GameProjectSceneCaptureHandle capture{};
        GameProjectObjectHandle modelObject{};
        char materialSlotName[
            kGameProjectSceneCaptureMaterialSlotNameCapacity]{};
        uint32_t fallbackMaterialIndex = kGameProjectInvalidMaterialIndex;
        uint32_t flags = GameProjectSceneCaptureMaterialBindingFlags(
            GameProjectSceneCaptureMaterialBindingFlag::None);
        GameProjectSceneCaptureSourceRect sourceRect{};
        uint32_t quarterTurns = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };
    };

    struct GameProjectSceneCaptureSnapshot {
        uint32_t structSize = kGameProjectSceneCaptureSnapshotV1Size;
        uint32_t version = kGameProjectSceneCaptureVersion;
        GameProjectSceneCaptureHandle capture{};
        GameProjectSceneCaptureStatus status =
            GameProjectSceneCaptureStatus::Disabled;
        GameProjectSceneCaptureFailure failure =
            GameProjectSceneCaptureFailure::None;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint64_t outputRevision = 0u;
        uint64_t renderedFrameIndex = 0u;
        uint32_t reserved[2] = { 0u, 0u };
    };

    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureHandle>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureHandle>);
    static_assert(sizeof(GameProjectSceneCaptureHandle) == 16u);
    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureDesc>);
    static_assert(sizeof(GameProjectSceneCaptureDesc) ==
        kGameProjectSceneCaptureDescV1Size);
    static_assert(offsetof(
        GameProjectSceneCaptureDesc, cameraObject) == 8u);
    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureMaterialBindingDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureMaterialBindingDesc>);
    static_assert(sizeof(GameProjectSceneCaptureMaterialBindingDesc) ==
        kGameProjectSceneCaptureMaterialBindingDescV1Size);
    static_assert(offsetof(
        GameProjectSceneCaptureMaterialBindingDesc,
        materialSlotName) == 40u);
    static_assert(offsetof(
        GameProjectSceneCaptureMaterialBindingDesc,
        flags) == 140u);
    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureSourceRect>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureSourceRect>);
    static_assert(sizeof(GameProjectSceneCaptureSourceRect) == 16u);
    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureMaterialRegionBindingDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureMaterialRegionBindingDesc>);
    static_assert(sizeof(
        GameProjectSceneCaptureMaterialRegionBindingDesc) ==
        kGameProjectSceneCaptureMaterialRegionBindingDescV1Size);
    static_assert(offsetof(
        GameProjectSceneCaptureMaterialRegionBindingDesc,
        sourceRect) == 144u);
    static_assert(std::is_standard_layout_v<
        GameProjectSceneCaptureSnapshot>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectSceneCaptureSnapshot>);
    static_assert(sizeof(GameProjectSceneCaptureSnapshot) ==
        kGameProjectSceneCaptureSnapshotV1Size);

} // namespace HIKARI
