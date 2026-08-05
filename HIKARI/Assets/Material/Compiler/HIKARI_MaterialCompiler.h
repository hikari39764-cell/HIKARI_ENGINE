#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

    enum class MaterialProgramCapability : uint64_t {
        None = 0u,
        StandardPbr = 1ull << 0u,
        Unlit = 1ull << 1u,
        AlphaMask = 1ull << 2u,
        AlphaBlend = 1ull << 3u,
        DoubleSided = 1ull << 4u,
        RampDiffuse = 1ull << 5u,
        MultiRampDiffuse = 1ull << 6u,
        SpecularBand = 1ull << 7u,
        FresnelRim = 1ull << 8u,
        MatCap = 1ull << 9u,
        AnisotropicHair = 1ull << 10u,
        FaceSdf = 1ull << 11u,
        EyeLighting = 1ull << 12u,
        FunctionCalls = 1ull << 13u,
        AuxiliaryUv = 1ull << 14u,
        MaterialInstance = 1ull << 15u,
        MaterialProfile = 1ull << 16u,
        RampAsset = 1ull << 17u,
        SkinLighting = 1ull << 18u,
        FabricSheen = 1ull << 19u,
        HybridLighting = 1ull << 20u,
        Outline = 1ull << 21u,
        HairShadow = 1ull << 22u,
        CorneaHighlight = 1ull << 23u,
    };

    constexpr uint64_t MaterialCapabilityBit(
        MaterialProgramCapability capability) noexcept {
        return static_cast<uint64_t>(capability);
    }

    struct MaterialProgramKey {
        uint64_t structureHash = 0u;
        MaterialDomain domain = MaterialDomain::Surface;
        uint64_t capabilityMask = 0u;

        bool operator==(const MaterialProgramKey& rhs) const noexcept {
            return structureHash == rhs.structureHash &&
                domain == rhs.domain &&
                capabilityMask == rhs.capabilityMask;
        }
    };

    struct MaterialProgramParameterBinding {
        std::string parameterId{};
        MaterialValueType type = MaterialValueType::Float1;
        uint32_t byteOffset = 0u;
        uint32_t byteSize = 16u;
        MaterialValueData defaultValue{};
    };

    struct MaterialProgramResourceBinding {
        std::string resourceId{};
        MaterialResourceType type = MaterialResourceType::Texture2D;
        MaterialResourceSemantic semantic = MaterialResourceSemantic::Custom;
        AssetGuid assetGuid{};
        uint32_t resourceIndex = 0u;
        uint32_t texCoord = 0u;
        MATH::Vec2 uvScale{ 1.0f, 1.0f };
        MATH::Vec2 uvOffset{ 0.0f, 0.0f };
        float uvRotation = 0.0f;
    };

    inline constexpr uint32_t kInvalidMaterialProgramRegister =
        (std::numeric_limits<uint32_t>::max)();

    // Native built-in compatibility IDs are serialized directly as cooked
    // opcodes. Source graphs use MaterialNodeTypeId; Function calls are
    // expanded before lowering, so Runtime IR v3 remains compact and closed.
    using MaterialProgramOpcode = MaterialNodeKind;

    // Deterministic SSA-like instruction. The pin arrays preserve the bounded
    // node catalogue order so shader generation never needs the Editor graph.
    struct MaterialProgramInstruction {
        MaterialProgramOpcode opcode = MaterialProgramOpcode::Constant;
        MaterialValueType valueType = MaterialValueType::Float1;
        std::vector<std::string> inputPins{};
        std::vector<uint32_t> inputRegisters{};
        std::vector<std::string> outputPins{};
        std::vector<uint32_t> outputRegisters{};
        uint32_t bindingIndex = kInvalidMaterialProgramRegister;
        MaterialValueData immediate{};
        std::vector<MaterialNodePropertyData> properties{};
        AssetGuid assetReference{};
        std::string sourceNodeId{};
    };

    struct MaterialProgramIR {
        uint32_t version = kMaterialProgramIrVersion;
        MaterialProgramKey key{};
        MaterialAssetKind sourceKind = MaterialAssetKind::Material;
        MaterialDomain domain = MaterialDomain::Surface;
        MaterialTemplateId templateId = MaterialTemplateId::StandardPbr;
        MaterialSurfaceData surface{};
        uint64_t capabilities = 0u;
        std::vector<MaterialFeatureRequest> featureRequests{};
        std::vector<MaterialProgramParameterBinding> parameters{};
        std::vector<MaterialProgramResourceBinding> resources{};
        std::vector<MaterialProgramInstruction> instructions{};
        uint32_t registerCount = 0u;
        uint32_t surfaceOutputRegister = kInvalidMaterialProgramRegister;
        std::vector<AssetGuid> dependencyGuids{};
        AssetGuid parentMaterialGuid{};
        AssetGuid profileAssetGuid{};
        std::string programId{};
    };

    enum class MaterialCompileDiagnosticSeverity : uint8_t {
        Info,
        Warning,
        Error,
    };

    struct MaterialCompileDiagnostic {
        MaterialCompileDiagnosticSeverity severity =
            MaterialCompileDiagnosticSeverity::Error;
        std::string code{};
        std::string message{};
        std::string subjectId{};
    };

    struct MaterialCompileResult {
        bool success = false;
        bool usedFallback = false;
        MaterialProgramIR program{};
        std::vector<MaterialCompileDiagnostic> diagnostics{};
    };

    using MaterialCompileAssetResolver = std::function<bool(
        const AssetGuid& assetGuid,
        MaterialAssetData& outSource,
        std::string& outError)>;

    struct MaterialCompileOptions {
        MaterialCompileAssetResolver resolveAsset{};
        uint32_t maximumFunctionDepth = 16u;
    };

    MaterialProgramIR BuildStandardPbrMaterialProgramFallback();

    MaterialCompileResult CompileMaterialAsset(
        const MaterialAssetData& source);
    MaterialCompileResult CompileMaterialAsset(
        const MaterialAssetData& source,
        const MaterialCompileOptions& options);

} // namespace HIKARI
