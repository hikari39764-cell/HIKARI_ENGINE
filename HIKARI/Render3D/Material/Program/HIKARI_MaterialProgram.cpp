#include "Render3D/Material/Program/HIKARI_MaterialProgram.h"

#include <functional>

namespace HIKARI::RENDER3D::MATERIAL {
    namespace {
        constexpr bool HasMaterialCapability(
            uint64_t capabilities,
            MaterialProgramCapability capability) noexcept {
            return (capabilities & MaterialCapabilityBit(capability)) != 0u;
        }

        bool ParameterLayoutsMatch(
            const MaterialProgramParameterBinding& lhs,
            const MaterialProgramParameterBinding& rhs) noexcept {
            return lhs.parameterId == rhs.parameterId &&
                lhs.type == rhs.type &&
                lhs.byteOffset == rhs.byteOffset &&
                lhs.byteSize == rhs.byteSize;
        }

        bool ResourceLayoutsMatch(
            const MaterialProgramResourceBinding& lhs,
            const MaterialProgramResourceBinding& rhs) noexcept {
            return lhs.resourceId == rhs.resourceId &&
                lhs.type == rhs.type &&
                lhs.semantic == rhs.semantic &&
                lhs.resourceIndex == rhs.resourceIndex;
        }

        bool InstructionLayoutsMatch(
            const MaterialProgramInstruction& lhs,
            const MaterialProgramInstruction& rhs) noexcept {
            if (lhs.opcode != rhs.opcode ||
                lhs.valueType != rhs.valueType ||
                lhs.inputPins != rhs.inputPins ||
                lhs.inputRegisters != rhs.inputRegisters ||
                lhs.outputPins != rhs.outputPins ||
                lhs.outputRegisters != rhs.outputRegisters ||
                lhs.bindingIndex != rhs.bindingIndex ||
                lhs.immediate.type != rhs.immediate.type ||
                lhs.assetReference != rhs.assetReference ||
                lhs.sourceNodeId != rhs.sourceNodeId ||
                lhs.properties.size() != rhs.properties.size()) {
                return false;
            }
            for (size_t index = 0u; index < lhs.properties.size(); ++index) {
                if (lhs.properties[index].id != rhs.properties[index].id ||
                    lhs.properties[index].value.type !=
                        rhs.properties[index].value.type) {
                    return false;
                }
            }
            return true;
        }
    }

    bool MaterialProgramRuntimeCapabilities::Supports(
        MaterialProgramBackendCapability capability) const noexcept {
        return (backendMask & MaterialProgramCapabilityBit(capability)) != 0u;
    }

    bool MaterialProgramRuntimeCapabilities::Supports(
        MaterialProgramPassCapability capability) const noexcept {
        return (passMask & MaterialProgramCapabilityBit(capability)) != 0u;
    }

    bool MaterialProgramRuntimeCapabilities::Requires(
        MaterialProgramVertexSemantic semantic) const noexcept {
        return (requiredVertexSemanticMask &
            MaterialProgramCapabilityBit(semantic)) != 0u;
    }

    size_t MaterialProgramKeyHasher::operator()(
        const MaterialProgramKey& key) const noexcept {
        size_t seed = std::hash<uint64_t>{}(key.structureHash);
        seed ^= std::hash<uint64_t>{}(key.capabilityMask) +
            0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<uint32_t>{}(static_cast<uint32_t>(key.domain)) +
            0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }

    MaterialProgramRuntimeCapabilities BuildMaterialProgramRuntimeCapabilities(
        const MaterialProgramIR& program) noexcept {
        MaterialProgramRuntimeCapabilities result{};
        result.materialCapabilityMask = program.capabilities;
        result.backendMask =
            MaterialProgramCapabilityBit(MaterialProgramBackendCapability::StaticVsPs) |
            MaterialProgramCapabilityBit(MaterialProgramBackendCapability::SkinnedVsPs) |
            MaterialProgramCapabilityBit(MaterialProgramBackendCapability::GpuDrivenTraditional) |
            MaterialProgramCapabilityBit(MaterialProgramBackendCapability::MeshShader);
        result.passMask =
            MaterialProgramCapabilityBit(MaterialProgramPassCapability::Forward) |
            MaterialProgramCapabilityBit(MaterialProgramPassCapability::GeometryAux);
        if (!HasMaterialCapability(
                program.capabilities,
                MaterialProgramCapability::AlphaBlend)) {
            result.passMask |=
                MaterialProgramCapabilityBit(MaterialProgramPassCapability::DepthPrepass) |
                MaterialProgramCapabilityBit(MaterialProgramPassCapability::Shadow);
        }

        result.requiredVertexSemanticMask =
            MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Position) |
            MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Normal) |
            MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Uv0);
        if (HasMaterialCapability(
                program.capabilities,
                MaterialProgramCapability::AnisotropicHair)) {
            result.requiredVertexSemanticMask |=
                MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Tangent);
        }
        if (HasMaterialCapability(
                program.capabilities,
                MaterialProgramCapability::AuxiliaryUv)) {
            result.requiredVertexSemanticMask |=
                MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Uv1);
        }
        for (const MaterialProgramResourceBinding& resource : program.resources) {
            if (resource.semantic == MaterialResourceSemantic::Normal) {
                result.requiredVertexSemanticMask |=
                    MaterialProgramCapabilityBit(MaterialProgramVertexSemantic::Tangent);
                break;
            }
        }
        return result;
    }

    RuntimeMaterialProgram BuildRuntimeMaterialProgram(
        MaterialProgramId id,
        const MaterialProgramIR& program,
        bool standardPbrFallback) {
        RuntimeMaterialProgram result{};
        result.id = id;
        result.ir = program;
        result.capabilities = BuildMaterialProgramRuntimeCapabilities(program);
        result.standardPbrFallback = standardPbrFallback;
        return result;
    }

    bool IsMaterialProgramLayoutCompatible(
        const MaterialProgramIR& lhs,
        const MaterialProgramIR& rhs) noexcept {
        if (lhs.version != rhs.version ||
            !(lhs.key == rhs.key) ||
            lhs.domain != rhs.domain ||
            lhs.capabilities != rhs.capabilities ||
            lhs.parameters.size() != rhs.parameters.size() ||
            lhs.resources.size() != rhs.resources.size() ||
            lhs.instructions.size() != rhs.instructions.size() ||
            lhs.registerCount != rhs.registerCount ||
            lhs.surfaceOutputRegister != rhs.surfaceOutputRegister) {
            return false;
        }
        for (size_t index = 0u; index < lhs.parameters.size(); ++index) {
            if (!ParameterLayoutsMatch(
                    lhs.parameters[index],
                    rhs.parameters[index])) {
                return false;
            }
        }
        for (size_t index = 0u; index < lhs.resources.size(); ++index) {
            if (!ResourceLayoutsMatch(
                    lhs.resources[index],
                    rhs.resources[index])) {
                return false;
            }
        }
        for (size_t index = 0u; index < lhs.instructions.size(); ++index) {
            if (!InstructionLayoutsMatch(
                    lhs.instructions[index],
                    rhs.instructions[index])) {
                return false;
            }
        }
        return true;
    }

    std::string_view ToString(
        MaterialProgramBackendCapability capability) noexcept {
        switch (capability) {
        case MaterialProgramBackendCapability::StaticVsPs: return "StaticVSPS";
        case MaterialProgramBackendCapability::SkinnedVsPs: return "SkinnedVSPS";
        case MaterialProgramBackendCapability::GpuDrivenTraditional: return "GpuDrivenTraditional";
        case MaterialProgramBackendCapability::MeshShader: return "MeshShader";
        default: return "None";
        }
    }

    std::string_view ToString(
        MaterialProgramPassCapability capability) noexcept {
        switch (capability) {
        case MaterialProgramPassCapability::Forward: return "Forward";
        case MaterialProgramPassCapability::DepthPrepass: return "DepthPrepass";
        case MaterialProgramPassCapability::Shadow: return "Shadow";
        case MaterialProgramPassCapability::GeometryAux: return "GeometryAux";
        default: return "None";
        }
    }

} // namespace HIKARI::RENDER3D::MATERIAL
