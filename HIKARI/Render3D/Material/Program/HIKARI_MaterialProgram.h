#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramTypes.h"

namespace HIKARI::RENDER3D::MATERIAL {

    enum class MaterialProgramBackendCapability : uint32_t {
        None = 0u,
        StaticVsPs = 1u << 0u,
        SkinnedVsPs = 1u << 1u,
        GpuDrivenTraditional = 1u << 2u,
        MeshShader = 1u << 3u,
    };

    enum class MaterialProgramPassCapability : uint32_t {
        None = 0u,
        Forward = 1u << 0u,
        DepthPrepass = 1u << 1u,
        Shadow = 1u << 2u,
        GeometryAux = 1u << 3u,
    };

    enum class MaterialProgramVertexSemantic : uint32_t {
        None = 0u,
        Position = 1u << 0u,
        Normal = 1u << 1u,
        Tangent = 1u << 2u,
        Uv0 = 1u << 3u,
        Uv1 = 1u << 4u,
    };

    constexpr uint32_t MaterialProgramCapabilityBit(
        MaterialProgramBackendCapability capability) noexcept {
        return static_cast<uint32_t>(capability);
    }

    constexpr uint32_t MaterialProgramCapabilityBit(
        MaterialProgramPassCapability capability) noexcept {
        return static_cast<uint32_t>(capability);
    }

    constexpr uint32_t MaterialProgramCapabilityBit(
        MaterialProgramVertexSemantic semantic) noexcept {
        return static_cast<uint32_t>(semantic);
    }

    struct MaterialProgramRuntimeCapabilities {
        uint64_t materialCapabilityMask = 0u;
        uint32_t backendMask = 0u;
        uint32_t passMask = 0u;
        uint32_t requiredVertexSemanticMask = 0u;

        bool Supports(MaterialProgramBackendCapability capability) const noexcept;
        bool Supports(MaterialProgramPassCapability capability) const noexcept;
        bool Requires(MaterialProgramVertexSemantic semantic) const noexcept;
    };

    struct RuntimeMaterialProgram {
        MaterialProgramId id{};
        MaterialProgramIR ir{};
        MaterialProgramRuntimeCapabilities capabilities{};
        bool standardPbrFallback = false;
    };

    struct MaterialProgramKeyHasher {
        size_t operator()(const MaterialProgramKey& key) const noexcept;
    };

    MaterialProgramRuntimeCapabilities BuildMaterialProgramRuntimeCapabilities(
        const MaterialProgramIR& program) noexcept;

    RuntimeMaterialProgram BuildRuntimeMaterialProgram(
        MaterialProgramId id,
        const MaterialProgramIR& program,
        bool standardPbrFallback = false);

    // Compares only shader/runtime layout. Default parameter values, resource
    // asset GUIDs, feature requests and source labels are instance/source data
    // and must not create another program or PSO.
    bool IsMaterialProgramLayoutCompatible(
        const MaterialProgramIR& lhs,
        const MaterialProgramIR& rhs) noexcept;

    std::string_view ToString(MaterialProgramBackendCapability capability) noexcept;
    std::string_view ToString(MaterialProgramPassCapability capability) noexcept;

} // namespace HIKARI::RENDER3D::MATERIAL
