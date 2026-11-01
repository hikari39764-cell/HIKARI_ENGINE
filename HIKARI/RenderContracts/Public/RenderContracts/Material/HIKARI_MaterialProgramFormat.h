#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "RenderContracts/Material/HIKARI_MaterialShellSettings.h"

namespace HIKARI {

inline constexpr uint32_t kMaterialProgramIrVersion = 11u;

enum class MaterialAssetKind : uint8_t {
    Material,
    Function,
    Instance,
    Profile,
    Ramp,
};

enum class MaterialDomain : uint8_t {
    Surface,
    Particle,
};

enum class MaterialTemplateId : uint8_t {
    StandardPbr,
    GenericToon,
    Skin,
    Face,
    Hair,
    Eye,
    Cloth,
    HybridMetal,
    Custom,
    ParticleUnlit,
    ParticleUnlitTextured,
    PainterlyShell,
};

enum class MaterialAlphaMode : uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class MaterialAlphaRepresentation : uint8_t {
    Straight,
    Premultiplied,
};

enum class MaterialValueType : uint8_t {
    Bool,
    Float1,
    Float2,
    Float3,
    Float4,
    Surface,
    DiffuseResponse,
    SpecularResponse,
    OutlineResponse,
    ShellSettings,
};

enum class MaterialResourceType : uint8_t {
    Texture2D,
    TextureCube,
    Ramp1D,
    Ramp2D,
};

enum class MaterialResourceSemantic : uint8_t {
    Custom,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Specular,
    SpecularColor,
    Mask,
    Ramp,
    MatCap,
    FaceSdf,
    HairShadow,
    HairControl,
    EyeControl,
    Thickness,
    Fabric,
    // VFX の歪み/溶解が参照する汎用ノイズ。応答ノード用の空きスロットへ
    // 割り当て、標準 PBR の投影先とは混ぜない。
    Noise,
    // 2 つ目のノイズ場。大まかな流れと細かい質感を別の貼図で与えたい
    // ときに使う。Noise と同じく応答ノードの空きスロットを 1 つ使う。
    DetailNoise,
    FlowMap,
    // 筆触の殻（Shell ノード）が所有する筆触マスクと筆触法線。
    StrokeMask,
    StrokeNormal,
};

enum class MaterialNodeKind : uint8_t {
    Constant,
    Parameter,
    TextureSample,
    Add,
    Multiply,
    Lerp,
    Clamp,
    Saturate,
    OneMinus,
    Power,
    Dot,
    NormalUnpack,
    PbrSurface,
    RampDiffuse,
    MultiRampDiffuse,
    SpecularBand,
    FresnelRim,
    MatCap,
    AnisotropicHair,
    FaceSdf,
    EyeLighting,
    SurfaceOutput,
    FunctionCall,
    SkinLighting,
    FabricSheen,
    HybridLighting,
    Outline,
    HairShadow,
    CorneaHighlight,
    Subtract,
    Divide,
    Minimum,
    Maximum,
    Absolute,
    Negate,
    Floor,
    Ceil,
    Fraction,
    Modulo,
    SquareRoot,
    Sine,
    Cosine,
    Step,
    SmoothStep,
    Remap,
    Cross,
    Length,
    Normalize,
    Distance,
    Reflect,
    MakeFloat2,
    MakeFloat3,
    MakeFloat4,
    Split,
    Select,
    TextureCoordinate,
    Time,
    WorldPosition,
    WorldNormal,
    WorldTangent,
    ViewDirection,
    CameraPosition,
    ScreenPosition,
    DerivativeX,
    DerivativeY,
    FilterWidth,
    // Append-only: new native/source nodes MUST be added at the end.
    // Variable is source-only and lowers to Parameter. ParticleOutput is
    // a terminal opcode and is never executed by the expression runtime.
    Atan2,
    Exp,
    Log2,
    Sign,
    Round,
    Trunc,
    UvTransform,
    ValueNoise,
    Voronoi,
    Fresnel,
    Variable,
    ParticleOutput,
    ParticleState,
    ParticleColor,
    ParticleUv,
    Shell,
    ShellLayer,
    NormalBlend,
    NormalUvTransform,
};

enum class MaterialFeatureRequest : uint8_t {
    GeometryOutline,
    ScreenOutline,
    VisibilityOverlay,
    FaceHairShadow,
};

struct MaterialValueData {
    MaterialValueType type = MaterialValueType::Float1;
    MATH::Vec4 numeric{0.0f, 0.0f, 0.0f, 0.0f};
    bool boolean = false;
};

inline MaterialValueData MakeMaterialValue(MaterialValueType type, float x, float y = 0.0f, float z = 0.0f,
                                           float w = 0.0f) {
    MaterialValueData value{};
    value.type = type;
    value.numeric = {x, y, z, w};
    return value;
}

struct MaterialNodePropertyData {
    std::string id{};
    MaterialValueData value{};
};

struct MaterialSurfaceData {
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    MaterialAlphaRepresentation alphaRepresentation = MaterialAlphaRepresentation::Straight;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false;
};

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
    ParticleExpression = 1ull << 24u,
    Shell = 1ull << 25u,
};

constexpr uint64_t MaterialCapabilityBit(MaterialProgramCapability capability) noexcept {
    return static_cast<uint64_t>(capability);
}

struct MaterialProgramKey {
    uint64_t structureHash = 0u;
    MaterialDomain domain = MaterialDomain::Surface;
    uint64_t capabilityMask = 0u;

    bool operator==(const MaterialProgramKey& rhs) const noexcept {
        return structureHash == rhs.structureHash && domain == rhs.domain && capabilityMask == rhs.capabilityMask;
    }
};

struct MaterialProgramParameterBinding {
    std::string parameterId{};
    MaterialValueType type = MaterialValueType::Float1;
    uint32_t byteOffset = 0u;
    uint32_t byteSize = 16u;
    MaterialValueData defaultValue{};
    // Empty for ordinary authored parameters. Runtime-variable source
    // nodes retain their project binding ID here while lowering to the
    // existing Parameter opcode.
    std::string externalBindingId{};
};

inline std::string MakeMaterialVariableParameterId(std::string_view sourceNodeId) {
    return "runtime.variable:" + std::string(sourceNodeId);
}

struct MaterialProgramResourceBinding {
    std::string resourceId{};
    MaterialResourceType type = MaterialResourceType::Texture2D;
    MaterialResourceSemantic semantic = MaterialResourceSemantic::Custom;
    AssetGuid assetGuid{};
    uint32_t resourceIndex = 0u;
    uint32_t texCoord = 0u;
    MATH::Vec2 uvScale{1.0f, 1.0f};
    MATH::Vec2 uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;
};

inline constexpr uint32_t kInvalidMaterialProgramRegister = (std::numeric_limits<uint32_t>::max)();

// Native built-in compatibility IDs are serialized directly as cooked
// opcodes. Source graphs use MaterialNodeTypeId; Function calls are
// expanded before lowering, so Runtime IR remains compact and closed.
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
    MaterialShellSettings shell{};
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
    MaterialCompileDiagnosticSeverity severity = MaterialCompileDiagnosticSeverity::Error;
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

bool IsMaterialNumericType(MaterialValueType type) noexcept;
uint32_t GetMaterialValueComponentCount(MaterialValueType type) noexcept;

uint32_t FindMaterialProgramInputRegister(const MaterialProgramInstruction& instruction, std::string_view pin) noexcept;

// Constant/Parameter のみを uniform 設定へ投影する。ピクセル式や任意 WPO は実行しない。
bool ResolveMaterialProgramShellSettings(
    const MaterialProgramIR& program, MaterialShellSettings& outSettings, std::string& outError,
    const std::function<const MaterialValueData*(uint32_t)>& parameterOverride = {});

} // namespace HIKARI
