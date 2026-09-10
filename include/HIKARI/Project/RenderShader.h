#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/RenderFrame.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectRenderShaderVersion = 1u;
    inline constexpr uint32_t kGameProjectShaderAssetGuidCapacity = 33u;
    inline constexpr uint32_t kGameProjectShaderEntryIdCapacity = 48u;
    inline constexpr uint32_t kGameProjectShaderProgramRefV1Size = 96u;
    inline constexpr uint32_t kGameProjectFullscreenDrawDescV1Size = 132u;

    // Canonical HSHADERC admission contract for the first project fullscreen
    // pipeline. The project manifest copies these exact values; the engine
    // validates them again together with DXIL reflection before creating PSO.
    inline constexpr const char*
        kGameProjectFullscreenShaderEngineInterfaceId =
            "HIKARI.Project.Fullscreen";
    inline constexpr uint32_t
        kGameProjectFullscreenShaderEngineInterfaceVersion = 2u;
    inline constexpr const char*
        kGameProjectFullscreenShaderEngineInterfaceHash =
            "0da689c9adb81cb84badc2064d9e3068746b1599d8071828641b5a7ddffd5b22";
    inline constexpr const char*
        kGameProjectFullscreenShaderRootLayoutId =
            "HIKARI.Project.Fullscreen.Root";
    inline constexpr uint32_t
        kGameProjectFullscreenShaderRootLayoutVersion = 2u;
    inline constexpr const char*
        kGameProjectFullscreenShaderRootLayoutHash =
            "7eea12a765e2c8df37ef16230ee0af4c4d662c90c7a0a2975ed074290a18c6a5";

    namespace DETAIL {

        inline bool CopyRenderShaderIdentifier(
            char* destination,
            uint32_t capacity,
            const char* value,
            bool exactAssetGuid) noexcept {
            if (destination == nullptr || capacity == 0u || value == nullptr) {
                return false;
            }
            uint32_t length = 0u;
            while (length < capacity && value[length] != '\0') {
                const char character = value[length];
                const bool digit = character >= '0' && character <= '9';
                const bool lower = character >= 'a' && character <= 'f';
                const bool upper = character >= 'A' && character <= 'F';
                const bool alpha =
                    (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z');
                const bool stablePunctuation =
                    character == '_' || character == '.' || character == '-';
                if (exactAssetGuid) {
                    if (!digit && !lower && !upper) {
                        return false;
                    }
                } else if ((!alpha && !digit && !stablePunctuation) ||
                           (length == 0u && digit)) {
                    return false;
                }
                ++length;
            }
            const uint32_t requiredLength = exactAssetGuid ? 32u : 1u;
            if (length < requiredLength || length >= capacity ||
                (exactAssetGuid && length != 32u)) {
                return false;
            }
            for (uint32_t index = 0u; index < capacity; ++index) {
                destination[index] = index < length ? value[index] : '\0';
            }
            return true;
        }

        inline bool IsRenderShaderIdentifierValid(
            const char* value,
            uint32_t capacity,
            bool exactAssetGuid) noexcept {
            if (value == nullptr || capacity == 0u ||
                capacity > kGameProjectShaderEntryIdCapacity) {
                return false;
            }
            uint32_t length = 0u;
            while (length < capacity && value[length] != '\0') {
                const char character = value[length];
                const bool digit = character >= '0' && character <= '9';
                const bool lower = character >= 'a' && character <= 'f';
                const bool upper = character >= 'A' && character <= 'F';
                const bool alpha =
                    (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z');
                const bool stablePunctuation =
                    character == '_' || character == '.' ||
                    character == '-';
                if ((exactAssetGuid && !digit && !lower && !upper) ||
                    (!exactAssetGuid &&
                     ((!alpha && !digit && !stablePunctuation) ||
                      (length == 0u && digit)))) {
                    return false;
                }
                ++length;
            }
            if (length >= capacity || length == 0u ||
                (exactAssetGuid && length != 32u)) {
                return false;
            }
            for (uint32_t index = length + 1u;
                index < capacity;
                ++index) {
                if (value[index] != '\0') {
                    return false;
                }
            }
            return true;
        }

    } // namespace DETAIL

    constexpr bool IsGameProjectFullscreenShaderInput(
        GameProjectRenderSurfaceSemantic semantic,
        GameProjectRenderSurfacePhase phase) noexcept {
        return
            (semantic ==
                 GameProjectRenderSurfaceSemantic::SceneColorOpaque &&
             phase == GameProjectRenderSurfacePhase::Opaque) ||
            (semantic ==
                 GameProjectRenderSurfaceSemantic::SceneColorFinal &&
             phase == GameProjectRenderSurfacePhase::Final);
    }

    // Stable source identity only. GPU programs, DXIL and PSOs remain owned by
    // the engine. Both arrays must be NUL-terminated within their capacity.
    struct GameProjectShaderProgramRef {
        uint32_t structSize = kGameProjectShaderProgramRefV1Size;
        uint32_t version = kGameProjectRenderShaderVersion;
        char assetGuid[kGameProjectShaderAssetGuidCapacity]{};
        char pixelEntryId[kGameProjectShaderEntryIdCapacity]{};
        uint8_t reserved[7]{};

        bool SetAssetGuid(const char* value) noexcept {
            return DETAIL::CopyRenderShaderIdentifier(
                assetGuid,
                kGameProjectShaderAssetGuidCapacity,
                value,
                true);
        }

        bool SetPixelEntryId(const char* value) noexcept {
            return DETAIL::CopyRenderShaderIdentifier(
                pixelEntryId,
                kGameProjectShaderEntryIdCapacity,
                value,
                false);
        }

        bool IsValid() const noexcept {
            if (structSize != kGameProjectShaderProgramRefV1Size ||
                version != kGameProjectRenderShaderVersion ||
                !DETAIL::IsRenderShaderIdentifierValid(
                    assetGuid,
                    kGameProjectShaderAssetGuidCapacity,
                    true) ||
                !DETAIL::IsRenderShaderIdentifierValid(
                    pixelEntryId,
                    kGameProjectShaderEntryIdCapacity,
                    false)) {
                return false;
            }
            for (uint8_t value : reserved) {
                if (value != 0u) {
                    return false;
                }
            }
            return true;
        }
    };

    // The input must also be declared as Sampled by the containing pass. Use
    // SceneColorOpaque for an early opaque-only effect or SceneColorFinal for
    // a late effect that must preserve transparent and material screen
    // features. The output is the implicit engine-owned WorkingSceneColor.
    struct GameProjectFullscreenDrawDesc {
        uint32_t structSize = kGameProjectFullscreenDrawDescV1Size;
        uint32_t version = kGameProjectRenderShaderVersion;
        GameProjectShaderProgramRef shader{};
        GameProjectRenderSurfaceSemantic inputSemantic =
            GameProjectRenderSurfaceSemantic::None;
        GameProjectRenderSurfacePhase inputPhase =
            GameProjectRenderSurfacePhase::None;
        uint32_t reserved[6]{};

        bool IsValid() const noexcept {
            if (structSize != kGameProjectFullscreenDrawDescV1Size ||
                version != kGameProjectRenderShaderVersion ||
                !shader.IsValid() ||
                !IsGameProjectFullscreenShaderInput(
                    inputSemantic,
                    inputPhase)) {
                return false;
            }
            for (uint32_t value : reserved) {
                if (value != 0u) {
                    return false;
                }
            }
            return true;
        }
    };

    using DrawGameProjectFullscreenShaderFn = bool (*)(
        void* context,
        uint64_t commandGeneration,
        const GameProjectFullscreenDrawDesc* draw) noexcept;

    static_assert(std::is_standard_layout_v<GameProjectShaderProgramRef>);
    static_assert(std::is_trivially_copyable_v<GameProjectShaderProgramRef>);
    static_assert(sizeof(GameProjectShaderProgramRef) == 96u);
    static_assert(offsetof(GameProjectShaderProgramRef, assetGuid) == 8u);
    static_assert(offsetof(GameProjectShaderProgramRef, pixelEntryId) == 41u);
    static_assert(std::is_standard_layout_v<GameProjectFullscreenDrawDesc>);
    static_assert(std::is_trivially_copyable_v<GameProjectFullscreenDrawDesc>);
    static_assert(sizeof(GameProjectFullscreenDrawDesc) == 132u);
    static_assert(offsetof(GameProjectFullscreenDrawDesc, shader) == 8u);
    static_assert(offsetof(
        GameProjectFullscreenDrawDesc, inputSemantic) == 104u);
    static_assert(offsetof(
        GameProjectFullscreenDrawDesc, inputPhase) == 106u);
} // namespace HIKARI
