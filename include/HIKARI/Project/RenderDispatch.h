#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/RenderShader.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectRenderDispatchVersion = 1u;
    inline constexpr uint32_t kGameProjectComputeShaderProgramRefV1Size = 96u;
    inline constexpr uint32_t kGameProjectComputeDispatchDescV1Size = 128u;
    inline constexpr const char* kGameProjectDispatchShaderEngineInterfaceId =
        "HIKARI.Project.Dispatch";
    inline constexpr uint32_t
        kGameProjectDispatchShaderEngineInterfaceVersion = 1u;
    inline constexpr const char*
        kGameProjectDispatchShaderEngineInterfaceHash =
            "dfa3e57c0f1fa2cac90fdbbf09ea18781db05ef97246733004f7090e1cf38b8a";
    inline constexpr const char* kGameProjectDispatchShaderRootLayoutId =
        "HIKARI.Project.Dispatch.Root";
    inline constexpr uint32_t
        kGameProjectDispatchShaderRootLayoutVersion = 1u;
    inline constexpr const char* kGameProjectDispatchShaderRootLayoutHash =
        "3f66c10f350edb70882caace2efb99544e2fccaa14efd010d10f46917f44c953";

    // Stable source identity only. Project HLSL is admitted through its Asset
    // GUID and HSHADERC entry; compiled bytecode and pipelines remain engine
    // owned. Both arrays must be NUL-terminated within their capacity.
    struct GameProjectComputeShaderProgramRef {
        uint32_t structSize = kGameProjectComputeShaderProgramRefV1Size;
        uint32_t version = kGameProjectRenderDispatchVersion;
        char assetGuid[kGameProjectShaderAssetGuidCapacity]{};
        char computeEntryId[kGameProjectShaderEntryIdCapacity]{};
        uint8_t reserved[7]{};

        bool SetAssetGuid(const char* value) noexcept {
            return DETAIL::CopyRenderShaderIdentifier(
                assetGuid,
                kGameProjectShaderAssetGuidCapacity,
                value,
                true);
        }

        bool SetComputeEntryId(const char* value) noexcept {
            return DETAIL::CopyRenderShaderIdentifier(
                computeEntryId,
                kGameProjectShaderEntryIdCapacity,
                value,
                false);
        }

        bool IsValid() const noexcept {
            if (structSize != kGameProjectComputeShaderProgramRefV1Size ||
                version != kGameProjectRenderDispatchVersion ||
                !DETAIL::IsRenderShaderIdentifierValid(
                    assetGuid,
                    kGameProjectShaderAssetGuidCapacity,
                    true) ||
                !DETAIL::IsRenderShaderIdentifierValid(
                    computeEntryId,
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

    // v1 is deliberately narrow: one SceneColorOpaque input and one
    // ProjectDispatchOutput. The engine derives ceil(width/8), ceil(height/8)
    // dispatch dimensions and owns b0/t0/u0 binding, transitions and output
    // publication. Project callbacks never receive descriptors or resources.
    struct GameProjectComputeDispatchDesc {
        uint32_t structSize = kGameProjectComputeDispatchDescV1Size;
        uint32_t version = kGameProjectRenderDispatchVersion;
        GameProjectComputeShaderProgramRef shader{};
        GameProjectRenderSurfaceSemantic inputSemantic =
            GameProjectRenderSurfaceSemantic::SceneColorOpaque;
        GameProjectRenderSurfacePhase inputPhase =
            GameProjectRenderSurfacePhase::Opaque;
        uint8_t reserved0 = 0u;
        GameProjectRenderSurfaceSemantic outputSemantic =
            GameProjectRenderSurfaceSemantic::ProjectDispatchOutput;
        GameProjectRenderSurfacePhase outputPhase =
            GameProjectRenderSurfacePhase::PostOpaque;
        uint8_t reserved1 = 0u;
        uint32_t flags = 0u;
        uint32_t reserved[3]{};

        bool IsValid() const noexcept {
            if (structSize != kGameProjectComputeDispatchDescV1Size ||
                version != kGameProjectRenderDispatchVersion ||
                !shader.IsValid() ||
                inputSemantic !=
                    GameProjectRenderSurfaceSemantic::SceneColorOpaque ||
                inputPhase != GameProjectRenderSurfacePhase::Opaque ||
                outputSemantic !=
                    GameProjectRenderSurfaceSemantic::ProjectDispatchOutput ||
                outputPhase != GameProjectRenderSurfacePhase::PostOpaque ||
                reserved0 != 0u || reserved1 != 0u || flags != 0u) {
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

    using DispatchGameProjectComputeShaderFn = bool (*)(
        void* context,
        uint64_t commandGeneration,
        const GameProjectComputeDispatchDesc* dispatch) noexcept;

    static_assert(std::is_standard_layout_v<
        GameProjectComputeShaderProgramRef>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectComputeShaderProgramRef>);
    static_assert(sizeof(GameProjectComputeShaderProgramRef) == 96u);
    static_assert(offsetof(
        GameProjectComputeShaderProgramRef, assetGuid) == 8u);
    static_assert(offsetof(
        GameProjectComputeShaderProgramRef, computeEntryId) == 41u);
    static_assert(std::is_standard_layout_v<GameProjectComputeDispatchDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectComputeDispatchDesc>);
    static_assert(sizeof(GameProjectComputeDispatchDesc) == 128u);
    static_assert(offsetof(GameProjectComputeDispatchDesc, shader) == 8u);
    static_assert(offsetof(
        GameProjectComputeDispatchDesc, inputSemantic) == 104u);
    static_assert(offsetof(
        GameProjectComputeDispatchDesc, outputSemantic) == 108u);

} // namespace HIKARI
