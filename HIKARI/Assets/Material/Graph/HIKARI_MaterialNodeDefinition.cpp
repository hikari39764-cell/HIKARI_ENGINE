#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"

#include <algorithm>
#include <array>

namespace HIKARI {
namespace {

using Pin = MaterialNodePinDefinition;
using PinType = MaterialNodePinTypeSource;
using NodeClass = MaterialNodeSemanticClass;
using OutputRoute = MaterialNodeOutputRoute;

constexpr Pin Fixed(std::string_view id, MaterialValueType type,
                    bool runtimeGenerated = false,
                    std::string_view runtimeResourceChannel = {}) noexcept {
    return {id, type, PinType::Fixed, false, runtimeGenerated,
            runtimeResourceChannel};
}

constexpr Pin Dynamic(std::string_view id, bool acceptsDynamicNumeric) noexcept {
    return {id, MaterialValueType::Float1, PinType::NodeValue,
            acceptsDynamicNumeric, false, {}};
}

constexpr std::array<Pin, 1u> kConstantOutputs{{
    {"Value", MaterialValueType::Float1, PinType::ImmediateValue, false, false, {}},
}};
constexpr std::array<Pin, 1u> kParameterOutputs{{
    {"Value", MaterialValueType::Float1, PinType::ReferencedParameter, false, false, {}},
}};
constexpr std::array kTextureSampleOutputs{
    Fixed("Color", MaterialValueType::Float4),
    Fixed("RGB", MaterialValueType::Float3),
    Fixed("R", MaterialValueType::Float1),
    Fixed("G", MaterialValueType::Float1),
    Fixed("B", MaterialValueType::Float1),
    Fixed("A", MaterialValueType::Float1),
    Fixed("Alpha", MaterialValueType::Float1),
};
constexpr std::array kBinaryInputs{Dynamic("A", true), Dynamic("B", true)};
constexpr std::array kDynamicResult{Dynamic("Result", false)};
constexpr std::array kLerpInputs{
    Dynamic("A", true), Dynamic("B", true),
    Fixed("Alpha", MaterialValueType::Float1),
};
constexpr std::array kClampInputs{
    Dynamic("Value", true), Fixed("Min", MaterialValueType::Float1),
    Fixed("Max", MaterialValueType::Float1),
};
constexpr std::array kUnaryInputs{Dynamic("Value", true)};
constexpr std::array kPowerInputs{
    Dynamic("Base", true), Fixed("Exponent", MaterialValueType::Float1),
};
constexpr std::array kDotInputs{
    Fixed("A", MaterialValueType::Float3),
    Fixed("B", MaterialValueType::Float3),
};
constexpr std::array kFloatResult{Fixed("Result", MaterialValueType::Float1)};
constexpr std::array kNormalUnpackInputs{
    Fixed("PackedNormal", MaterialValueType::Float3),
};
constexpr std::array kNormalUnpackOutputs{
    Fixed("Normal", MaterialValueType::Float3),
};
constexpr std::array kPbrSurfaceInputs{
    Fixed("BaseColor", MaterialValueType::Float4),
    Fixed("Normal", MaterialValueType::Float3),
    Fixed("Metallic", MaterialValueType::Float1),
    Fixed("Roughness", MaterialValueType::Float1),
    Fixed("Occlusion", MaterialValueType::Float1),
    Fixed("Emissive", MaterialValueType::Float3),
    Fixed("Specular", MaterialValueType::Float1),
    Fixed("SpecularColor", MaterialValueType::Float3),
    Fixed("DiffuseResponse", MaterialValueType::DiffuseResponse),
    Fixed("SpecularResponse", MaterialValueType::SpecularResponse),
    Fixed("Eye", MaterialValueType::Float3),
    Fixed("Additive", MaterialValueType::Float3),
    Fixed("Outline", MaterialValueType::OutlineResponse),
};
constexpr std::array kSurfaceOutput{Fixed("Surface", MaterialValueType::Surface)};
constexpr std::array kRampDiffuseInputs{
    Fixed("ShadowThreshold", MaterialValueType::Float1),
    Fixed("ShadowSoftness", MaterialValueType::Float1),
    Fixed("MidThreshold", MaterialValueType::Float1),
    Fixed("ShadowTint", MaterialValueType::Float3),
    Fixed("MidTint", MaterialValueType::Float3),
    Fixed("LitTint", MaterialValueType::Float3),
    Fixed("MaskInfluence", MaterialValueType::Float1),
    Fixed("Intensity", MaterialValueType::Float1),
    Fixed("DirectGain", MaterialValueType::Float1),
    Fixed("Mask", MaterialValueType::Float1, false, "R"),
};
constexpr std::array kMultiRampDiffuseInputs{
    Fixed("ShadowThreshold", MaterialValueType::Float1),
    Fixed("ShadowSoftness", MaterialValueType::Float1),
    Fixed("MidThreshold", MaterialValueType::Float1),
    Fixed("ShadowTint", MaterialValueType::Float3),
    Fixed("MidTint", MaterialValueType::Float3),
    Fixed("LitTint", MaterialValueType::Float3),
    Fixed("RampRow", MaterialValueType::Float1),
    Fixed("MaskInfluence", MaterialValueType::Float1),
    Fixed("RampBlend", MaterialValueType::Float1),
    Fixed("LayerWeights", MaterialValueType::Float4),
    Fixed("Intensity", MaterialValueType::Float1),
    Fixed("DirectGain", MaterialValueType::Float1),
    Fixed("Mask", MaterialValueType::Float1, false, "R"),
};
constexpr std::array kDiffuseResponse{
    Fixed("Response", MaterialValueType::DiffuseResponse),
};
constexpr std::array kSpecularBandInputs{
    Fixed("Width", MaterialValueType::Float1),
    Fixed("Softness", MaterialValueType::Float1),
    Fixed("Intensity", MaterialValueType::Float1),
    Fixed("Power", MaterialValueType::Float1),
    Fixed("Tint", MaterialValueType::Float3),
};
constexpr std::array kSpecularResponse{
    Fixed("Response", MaterialValueType::SpecularResponse),
};
constexpr std::array kFresnelInputs{
    Fixed("Power", MaterialValueType::Float1),
    Fixed("Width", MaterialValueType::Float1),
    Fixed("Color", MaterialValueType::Float3),
    Fixed("Intensity", MaterialValueType::Float1),
    Fixed("LightAlignment", MaterialValueType::Float1),
};
constexpr std::array kColorOutput{Fixed("Color", MaterialValueType::Float3)};
constexpr std::array kMatCapInputs{
    Fixed("Image", MaterialValueType::Float4, false, "Color"),
    Fixed("Normal", MaterialValueType::Float3, true),
    Fixed("ViewDirection", MaterialValueType::Float3, true),
    Fixed("Mask", MaterialValueType::Float1, false, "A"),
};
constexpr std::array kHairInputs{
    Fixed("Tangent", MaterialValueType::Float3, true),
    Fixed("Shift", MaterialValueType::Float1),
    Fixed("Width", MaterialValueType::Float1),
    Fixed("PrimaryPower", MaterialValueType::Float1),
    Fixed("SecondaryPower", MaterialValueType::Float1),
    Fixed("SecondaryIntensity", MaterialValueType::Float1),
    Fixed("FlowStrength", MaterialValueType::Float1),
    Fixed("Mask", MaterialValueType::Float1, false, "R"),
};
constexpr std::array kFaceSdfInputs{
    Fixed("LightDirection", MaterialValueType::Float3, true),
    Fixed("SDF", MaterialValueType::Float1, false, "R"),
    Fixed("Softness", MaterialValueType::Float1),
    Fixed("DirectGain", MaterialValueType::Float1),
};
constexpr std::array kEyeInputs{
    Fixed("Control", MaterialValueType::Float4, false, "Color"),
    Fixed("ViewDirection", MaterialValueType::Float3, true),
};
constexpr std::array kSkinInputs{
    Fixed("DiffuseResponse", MaterialValueType::DiffuseResponse),
    Fixed("Wrap", MaterialValueType::Float1),
    Fixed("Subsurface", MaterialValueType::Float1),
    Fixed("Tint", MaterialValueType::Float3),
    Fixed("Mask", MaterialValueType::Float1, false, "G"),
};
constexpr std::array kFabricInputs{
    Fixed("Sheen", MaterialValueType::Float1),
    Fixed("FiberRoughness", MaterialValueType::Float1),
    Fixed("Power", MaterialValueType::Float1),
    Fixed("Intensity", MaterialValueType::Float1),
    Fixed("Mask", MaterialValueType::Float1, false, "B"),
};
constexpr std::array kHybridInputs{
    Fixed("DiffuseResponse", MaterialValueType::DiffuseResponse),
    Fixed("DirectDiffuseBlend", MaterialValueType::Float1),
    Fixed("DirectSpecularBlend", MaterialValueType::Float1),
    Fixed("IndirectBlend", MaterialValueType::Float1),
};
constexpr std::array kOutlineInputs{
    Fixed("Color", MaterialValueType::Float3),
    Fixed("Thickness", MaterialValueType::Float1),
};
constexpr std::array kOutlineOutput{
    Fixed("Response", MaterialValueType::OutlineResponse),
};
constexpr std::array kHairShadowInputs{
    Fixed("DiffuseResponse", MaterialValueType::DiffuseResponse),
    Fixed("ShadowMask", MaterialValueType::Float1, false, "R"),
    Fixed("Strength", MaterialValueType::Float1),
};
constexpr std::array kFunctionInputs{Dynamic("Input", false)};
constexpr std::array kFunctionOutputs{Dynamic("Result", false)};

constexpr std::array kDefinitions{
    MaterialNodeDefinition{MaterialNodeKind::Constant, "Constant",
        MaterialValueType::Float1, NodeClass::Input, true, {}, kConstantOutputs},
    MaterialNodeDefinition{MaterialNodeKind::Parameter, "Parameter",
        MaterialValueType::Float1, NodeClass::Input, true, {}, kParameterOutputs},
    MaterialNodeDefinition{MaterialNodeKind::TextureSample, "TextureSample",
        MaterialValueType::Float4, NodeClass::Input, true, {}, kTextureSampleOutputs},
    MaterialNodeDefinition{MaterialNodeKind::Add, "Add",
        MaterialValueType::Float1, NodeClass::Math, true, kBinaryInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Multiply, "Multiply",
        MaterialValueType::Float1, NodeClass::Math, true, kBinaryInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Lerp, "Lerp",
        MaterialValueType::Float1, NodeClass::Math, true, kLerpInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Clamp, "Clamp",
        MaterialValueType::Float1, NodeClass::Math, true, kClampInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Saturate, "Saturate",
        MaterialValueType::Float1, NodeClass::Math, true, kUnaryInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::OneMinus, "OneMinus",
        MaterialValueType::Float1, NodeClass::Math, true, kUnaryInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Power, "Power",
        MaterialValueType::Float1, NodeClass::Math, true, kPowerInputs, kDynamicResult, true},
    MaterialNodeDefinition{MaterialNodeKind::Dot, "Dot",
        MaterialValueType::Float1, NodeClass::Math, true, kDotInputs, kFloatResult, true},
    MaterialNodeDefinition{MaterialNodeKind::NormalUnpack, "NormalUnpack",
        MaterialValueType::Float3, NodeClass::Math, true,
        kNormalUnpackInputs, kNormalUnpackOutputs},
    MaterialNodeDefinition{MaterialNodeKind::PbrSurface, "PBRSurface",
        MaterialValueType::Surface, NodeClass::Surface, true,
        kPbrSurfaceInputs, kSurfaceOutput},
    MaterialNodeDefinition{MaterialNodeKind::RampDiffuse, "RampDiffuse",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kRampDiffuseInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase},
    MaterialNodeDefinition{MaterialNodeKind::MultiRampDiffuse, "MultiRampDiffuse",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kMultiRampDiffuseInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase},
    MaterialNodeDefinition{MaterialNodeKind::SpecularBand, "SpecularBand",
        MaterialValueType::SpecularResponse, NodeClass::Shading, true,
        kSpecularBandInputs, kSpecularResponse, false, OutputRoute::Specular},
    MaterialNodeDefinition{MaterialNodeKind::FresnelRim, "FresnelRim",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kFresnelInputs, kColorOutput, false, OutputRoute::Additive},
    MaterialNodeDefinition{MaterialNodeKind::MatCap, "MatCap",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kMatCapInputs, kColorOutput, false, OutputRoute::Additive},
    MaterialNodeDefinition{MaterialNodeKind::AnisotropicHair, "AnisotropicHair",
        MaterialValueType::SpecularResponse, NodeClass::Shading, true,
        kHairInputs, kSpecularResponse, false, OutputRoute::Specular},
    MaterialNodeDefinition{MaterialNodeKind::FaceSdf, "FaceSDF",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kFaceSdfInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase},
    // v5 deliberately uses EyeLayer. EyeLighting belonged to the disconnected
    // v4 additive path and must not be accepted as the current semantics.
    MaterialNodeDefinition{MaterialNodeKind::EyeLighting, "EyeLayer",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kEyeInputs, kColorOutput, false, OutputRoute::Eye},
    MaterialNodeDefinition{MaterialNodeKind::SurfaceOutput, "SurfaceOutput",
        MaterialValueType::Surface, NodeClass::Surface, true, kSurfaceOutput, {}},
    MaterialNodeDefinition{MaterialNodeKind::FunctionCall, "FunctionCall",
        MaterialValueType::Float1, NodeClass::Function, false,
        kFunctionInputs, kFunctionOutputs},
    MaterialNodeDefinition{MaterialNodeKind::SkinLighting, "SkinLighting",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kSkinInputs, kDiffuseResponse, false, OutputRoute::DiffuseAfterSkin},
    MaterialNodeDefinition{MaterialNodeKind::FabricSheen, "FabricSheen",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kFabricInputs, kColorOutput, false, OutputRoute::Additive},
    MaterialNodeDefinition{MaterialNodeKind::HybridLighting, "HybridLighting",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kHybridInputs, kDiffuseResponse, false, OutputRoute::DiffuseFinal},
    MaterialNodeDefinition{MaterialNodeKind::Outline, "Outline",
        MaterialValueType::OutlineResponse, NodeClass::Shading, true,
        kOutlineInputs, kOutlineOutput, false, OutputRoute::Outline},
    MaterialNodeDefinition{MaterialNodeKind::HairShadow, "HairShadow",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kHairShadowInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase},
    MaterialNodeDefinition{MaterialNodeKind::CorneaHighlight, "CorneaHighlight",
        MaterialValueType::Float3, NodeClass::Shading, true, {}, kColorOutput,
        false, OutputRoute::Eye},
};

static_assert(kDefinitions.size() ==
    static_cast<size_t>(MaterialNodeKind::CorneaHighlight) + 1u);

template <class Range>
const MaterialNodePinDefinition* FindPin(
    const Range& pins,
    std::string_view id) noexcept {
    const auto found = std::find_if(
        pins.begin(), pins.end(),
        [id](const MaterialNodePinDefinition& pin) { return pin.id == id; });
    return found == pins.end() ? nullptr : &*found;
}

} // namespace

std::span<const MaterialNodeDefinition> GetMaterialNodeDefinitions() noexcept {
    return kDefinitions;
}

const MaterialNodeDefinition* FindMaterialNodeDefinition(
    MaterialNodeKind kind) noexcept {
    const size_t index = static_cast<size_t>(kind);
    if (index >= kDefinitions.size() || kDefinitions[index].kind != kind) {
        return nullptr;
    }
    return &kDefinitions[index];
}

const MaterialNodeDefinition* FindMaterialNodeDefinition(
    const MaterialNodeTypeId& typeId) noexcept {
    return FindMaterialNodeDefinition(
        std::string_view{ typeId.value });
}

const MaterialNodeDefinition* FindMaterialNodeDefinition(
    std::string_view stableName) noexcept {
    const auto found = std::find_if(
        kDefinitions.begin(), kDefinitions.end(),
        [stableName](const MaterialNodeDefinition& definition) {
            return definition.stableName == stableName;
        });
    return found == kDefinitions.end() ? nullptr : &*found;
}

const MaterialNodePinDefinition* FindMaterialNodeInputDefinition(
    MaterialNodeKind kind,
    std::string_view pin) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(kind);
    return definition == nullptr ? nullptr : FindPin(definition->inputs, pin);
}

const MaterialNodePinDefinition* FindMaterialNodeInputDefinition(
    const MaterialNodeTypeId& typeId,
    std::string_view pin) noexcept {
    const MaterialNodeDefinition* definition =
        FindMaterialNodeDefinition(typeId);
    return definition == nullptr ? nullptr : FindPin(definition->inputs, pin);
}

const MaterialNodePinDefinition* FindMaterialNodeOutputDefinition(
    MaterialNodeKind kind,
    std::string_view pin) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(kind);
    return definition == nullptr ? nullptr : FindPin(definition->outputs, pin);
}

const MaterialNodePinDefinition* FindMaterialNodeOutputDefinition(
    const MaterialNodeTypeId& typeId,
    std::string_view pin) noexcept {
    const MaterialNodeDefinition* definition =
        FindMaterialNodeDefinition(typeId);
    return definition == nullptr ? nullptr : FindPin(definition->outputs, pin);
}

MaterialValueType ResolveMaterialNodePinType(
    const MaterialNodePinDefinition& pin,
    const MaterialGraphNodeData& node,
    std::optional<MaterialValueType> referencedParameterType) noexcept {
    switch (pin.typeSource) {
    case MaterialNodePinTypeSource::NodeValue:
        return node.valueType;
    case MaterialNodePinTypeSource::ImmediateValue:
        return node.value.type;
    case MaterialNodePinTypeSource::ReferencedParameter:
        return referencedParameterType.value_or(node.valueType);
    case MaterialNodePinTypeSource::Fixed:
    default:
        return pin.fixedType;
    }
}

} // namespace HIKARI
