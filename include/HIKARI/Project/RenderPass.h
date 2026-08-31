#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/RenderFrame.h"
#include "HIKARI/Project/RenderShader.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectRenderPassVersion = 1u;
    inline constexpr uint32_t kGameProjectRenderPassIdCapacity = 48u;
    inline constexpr uint32_t kGameProjectRenderPassMaximumResourceUseCount =
        8u;
    inline constexpr uint32_t kGameProjectRenderMaximumPassCount = 8u;
    inline constexpr uint64_t kGameProjectPrimaryRenderViewId = 1u;
    inline constexpr uint32_t kGameProjectRenderPassDescV1Size = 224u;
    inline constexpr uint32_t kGameProjectRenderCommandEncoderV1Size = 56u;
    inline constexpr uint32_t kGameProjectRenderPassContextV1Size = 88u;
    inline constexpr uint32_t kGameProjectRenderCommandEncoderVersion = 2u;

    enum class GameProjectRenderPassAnchor : uint32_t {
        PostOpaqueBeforeObserver = 0u,
        Count = 1u,
    };

    enum class GameProjectRenderPassTarget : uint32_t {
        None = 0u,
        WorkingSceneColor = 1u,
        Count = 2u,
    };

    enum class GameProjectRenderPassResourceRequirement : uint8_t {
        Required = 0u,
        Optional = 1u,
        Count = 2u,
    };

    enum class GameProjectRenderCommandCapability : uint32_t {
        None = 0u,
        ClearWorkingColor = 1u << 0u,
        ShaderCommands = 1u << 1u,
    };

    namespace GameProjectRenderPassFlag {
        inline constexpr uint32_t ShaderCommands = 1u << 0u;
        inline constexpr uint32_t All = ShaderCommands;
    }

    constexpr uint32_t GameProjectRenderViewPurposeBit(
        GameProjectRenderViewPurpose purpose) noexcept {
        return purpose < GameProjectRenderViewPurpose::Count
            ? 1u << static_cast<uint32_t>(purpose)
            : 0u;
    }

    inline constexpr uint32_t kGameProjectRenderPassV1PurposeMask =
        GameProjectRenderViewPurposeBit(
            GameProjectRenderViewPurpose::Game) |
        GameProjectRenderViewPurposeBit(
            GameProjectRenderViewPurpose::EditorScene);

    struct GameProjectRenderPassResourceUse {
        GameProjectRenderSurfaceSemantic semantic =
            GameProjectRenderSurfaceSemantic::None;
        GameProjectRenderSurfacePhase phase =
            GameProjectRenderSurfacePhase::None;
        GameProjectRenderPassResourceRequirement requirement =
            GameProjectRenderPassResourceRequirement::Required;
        GameProjectRenderSurfaceAccess access =
            GameProjectRenderSurfaceAccess::None;
        uint32_t reserved[2] = { 0u, 0u };
    };

    // Registration copies this complete descriptor. v1 deliberately admits
    // one forward-only anchor, so project dependency cycles are impossible by
    // construction. Active System order defines the pass order at the anchor.
    struct GameProjectRenderPassDesc {
        uint32_t structSize = kGameProjectRenderPassDescV1Size;
        uint32_t version = kGameProjectRenderPassVersion;
        char stableId[kGameProjectRenderPassIdCapacity]{};
        GameProjectRenderPassAnchor anchor =
            GameProjectRenderPassAnchor::PostOpaqueBeforeObserver;
        GameProjectRenderPassTarget target =
            GameProjectRenderPassTarget::None;
        uint32_t viewPurposeMask = kGameProjectRenderPassV1PurposeMask;
        uint32_t resourceUseCount = 0u;
        uint32_t flags = 0u;
        GameProjectRenderPassResourceUse resources[
            kGameProjectRenderPassMaximumResourceUseCount]{};
        uint32_t reserved[5] = { 0u, 0u, 0u, 0u, 0u };

        bool SetStableId(const char* value) noexcept {
            if (value == nullptr || value[0] == '\0') {
                return false;
            }
            uint32_t length = 0u;
            while (length < kGameProjectRenderPassIdCapacity &&
                value[length] != '\0') {
                ++length;
            }
            if (length == 0u ||
                length >= kGameProjectRenderPassIdCapacity) {
                return false;
            }
            for (uint32_t index = 0u;
                index < kGameProjectRenderPassIdCapacity;
                ++index) {
                stableId[index] = index < length ? value[index] : '\0';
            }
            return true;
        }

        bool IsValid() const noexcept {
            constexpr uint32_t readAccessMask =
                static_cast<uint32_t>(
                    GameProjectRenderSurfaceAccess::Sampled) |
                static_cast<uint32_t>(
                    GameProjectRenderSurfaceAccess::ReadDepth) |
                static_cast<uint32_t>(
                    GameProjectRenderSurfaceAccess::ReadUav) |
                static_cast<uint32_t>(
                    GameProjectRenderSurfaceAccess::CopySource);
            constexpr char prefix[] = "Project.";
            uint32_t idLength = 0u;
            while (idLength < kGameProjectRenderPassIdCapacity &&
                stableId[idLength] != '\0') {
                const char value = stableId[idLength];
                const bool alphaNumeric =
                    (value >= 'a' && value <= 'z') ||
                    (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9');
                if (!alphaNumeric && value != '.' && value != '_' &&
                    value != '-') {
                    return false;
                }
                ++idLength;
            }
            if (idLength <= sizeof(prefix) - 1u ||
                idLength >= kGameProjectRenderPassIdCapacity) {
                return false;
            }
            for (uint32_t index = 0u;
                index < sizeof(prefix) - 1u;
                ++index) {
                if (stableId[index] != prefix[index]) {
                    return false;
                }
            }
            if (structSize != kGameProjectRenderPassDescV1Size ||
                version != kGameProjectRenderPassVersion ||
                anchor !=
                    GameProjectRenderPassAnchor::PostOpaqueBeforeObserver ||
                target !=
                    GameProjectRenderPassTarget::WorkingSceneColor ||
                viewPurposeMask == 0u ||
                (viewPurposeMask &
                    ~kGameProjectRenderPassV1PurposeMask) != 0u ||
                resourceUseCount >
                    kGameProjectRenderPassMaximumResourceUseCount ||
                (flags & ~GameProjectRenderPassFlag::All) != 0u) {
                return false;
            }
            for (const uint32_t value : reserved) {
                if (value != 0u) {
                    return false;
                }
            }
            bool hasSampledInput = false;
            for (uint32_t index = 0u;
                index < kGameProjectRenderPassMaximumResourceUseCount;
                ++index) {
                const GameProjectRenderPassResourceUse& use =
                    resources[index];
                bool useReservedClear = true;
                for (const uint32_t value : use.reserved) {
                    useReservedClear = useReservedClear && value == 0u;
                }
                if (index >= resourceUseCount) {
                    if (use.semantic !=
                            GameProjectRenderSurfaceSemantic::None ||
                        use.phase != GameProjectRenderSurfacePhase::None ||
                        use.requirement !=
                            GameProjectRenderPassResourceRequirement::
                                Required ||
                        use.access != GameProjectRenderSurfaceAccess::None ||
                        !useReservedClear) {
                        return false;
                    }
                    continue;
                }
                const uint32_t access =
                    static_cast<uint32_t>(use.access);
                if (use.semantic <=
                        GameProjectRenderSurfaceSemantic::None ||
                    use.semantic >=
                        GameProjectRenderSurfaceSemantic::Count ||
                    use.phase <= GameProjectRenderSurfacePhase::None ||
                    use.phase >= GameProjectRenderSurfacePhase::Count ||
                    use.requirement >=
                        GameProjectRenderPassResourceRequirement::Count ||
                    access == 0u || (access & (access - 1u)) != 0u ||
                    (access & ~readAccessMask) != 0u ||
                    !useReservedClear) {
                    return false;
                }
                hasSampledInput = hasSampledInput ||
                    (use.access == GameProjectRenderSurfaceAccess::Sampled &&
                     IsGameProjectFullscreenShaderInput(
                        use.semantic,
                        use.phase));
                for (uint32_t previous = 0u;
                    previous < index;
                    ++previous) {
                    if (resources[previous].semantic == use.semantic &&
                        resources[previous].phase == use.phase) {
                        return false;
                    }
                }
            }
            if ((flags & GameProjectRenderPassFlag::ShaderCommands) != 0u &&
                !hasSampledInput) {
                return false;
            }
            return true;
        }
    };

    using ClearGameProjectWorkingColorFn = bool (*)(
        void* context,
        uint64_t commandGeneration,
        float red,
        float green,
        float blue,
        float alpha) noexcept;

    // Engine-owned and callback-borrowed. Copying this value does not extend
    // its lifetime; every command validates the matching frame/pass scope.
    struct GameProjectRenderCommandEncoder {
        uint32_t structSize = kGameProjectRenderCommandEncoderV1Size;
        uint32_t version = kGameProjectRenderPassVersion;
        void* context = nullptr;
        uint64_t commandGeneration = 0u;
        ClearGameProjectWorkingColorFn clearWorkingColor = nullptr;
        uint32_t capabilityMask = 0u;
        uint32_t reservedV1 = 0u;
        DrawGameProjectFullscreenShaderFn drawFullscreen = nullptr;
        uint32_t reserved[2] = { 0u, 0u };

        bool ClearWorkingColor(
            float red,
            float green,
            float blue,
            float alpha = 1.0f) const noexcept {
            const uint32_t capability = static_cast<uint32_t>(
                GameProjectRenderCommandCapability::ClearWorkingColor);
            return structSize == kGameProjectRenderCommandEncoderV1Size &&
                (version == kGameProjectRenderPassVersion ||
                 version == kGameProjectRenderCommandEncoderVersion) &&
                context != nullptr && commandGeneration != 0u &&
                clearWorkingColor != nullptr &&
                (capabilityMask & capability) != 0u &&
                clearWorkingColor(
                    context,
                    commandGeneration,
                    red,
                    green,
                    blue,
                    alpha);
        }

        bool DrawFullscreen(
            const GameProjectFullscreenDrawDesc& draw) const noexcept {
            const uint32_t capability = static_cast<uint32_t>(
                GameProjectRenderCommandCapability::ShaderCommands);
            if (structSize != kGameProjectRenderCommandEncoderV1Size ||
                version != kGameProjectRenderCommandEncoderVersion ||
                context == nullptr || commandGeneration == 0u ||
                drawFullscreen == nullptr || reservedV1 != 0u ||
                reserved[0] != 0u || reserved[1] != 0u ||
                (capabilityMask & capability) == 0u) {
                return false;
            }
            return drawFullscreen(context, commandGeneration, &draw);
        }
    };

    // All pointers and the encoder are valid only during ExecuteRenderPass.
    // frame.surfaces contains only this pass's declared and available inputs.
    struct GameProjectRenderPassContext {
        uint32_t structSize = kGameProjectRenderPassContextV1Size;
        uint32_t version = kGameProjectRenderPassVersion;
        const GameProjectRenderFrameView* frame = nullptr;
        GameProjectRenderCommandEncoder encoder{};
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    using ExecuteGameProjectRenderPassFn = bool (*)(
        const IGameSystem* system,
        const GameProjectRenderPassContext* context) noexcept;

    static_assert(std::is_standard_layout_v<
        GameProjectRenderPassResourceUse>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectRenderPassResourceUse>);
    static_assert(sizeof(GameProjectRenderPassResourceUse) == 16u);
    static_assert(std::is_standard_layout_v<GameProjectRenderPassDesc>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderPassDesc>);
    static_assert(sizeof(GameProjectRenderPassDesc) == 224u);
    static_assert(offsetof(GameProjectRenderPassDesc, stableId) == 8u);
    static_assert(offsetof(GameProjectRenderPassDesc, resources) == 76u);
    static_assert(std::is_standard_layout_v<
        GameProjectRenderCommandEncoder>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectRenderCommandEncoder>);
    static_assert(sizeof(GameProjectRenderCommandEncoder) == 56u);
    static_assert(offsetof(
        GameProjectRenderCommandEncoder, context) == 8u);
    static_assert(offsetof(
        GameProjectRenderCommandEncoder, commandGeneration) == 16u);
    static_assert(offsetof(
        GameProjectRenderCommandEncoder, drawFullscreen) == 40u);
    static_assert(std::is_standard_layout_v<GameProjectRenderPassContext>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectRenderPassContext>);
    static_assert(sizeof(GameProjectRenderPassContext) == 88u);
    static_assert(offsetof(GameProjectRenderPassContext, frame) == 8u);
    static_assert(offsetof(GameProjectRenderPassContext, encoder) == 16u);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectRenderPassAnchor>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectRenderPassTarget>, uint32_t>);

} // namespace HIKARI
