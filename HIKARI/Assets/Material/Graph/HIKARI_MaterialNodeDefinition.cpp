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
constexpr std::array kTextureSampleInputs{
    Fixed("UV", MaterialValueType::Float2),
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
    Fixed("Opacity", MaterialValueType::Float1),
    Fixed("Normal", MaterialValueType::Float3),
    Fixed("Metallic", MaterialValueType::Float1),
    Fixed("Roughness", MaterialValueType::Float1),
    Fixed("Occlusion", MaterialValueType::Float1),
    Fixed("Emissive", MaterialValueType::Float3),
    Fixed("Specular", MaterialValueType::Float1),
    Fixed("SpecularColor", MaterialValueType::Float3),
    Fixed("AlphaClipThreshold", MaterialValueType::Float1),
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
constexpr std::array kGenericFresnelInputs{
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
constexpr std::array kStepInputs{
    Dynamic("Edge", true), Dynamic("Value", true),
};
constexpr std::array kSmoothStepInputs{
    Dynamic("Edge0", true), Dynamic("Edge1", true),
    Dynamic("Value", true),
};
constexpr std::array kRemapInputs{
    Dynamic("Value", true), Dynamic("InMin", true),
    Dynamic("InMax", true), Dynamic("OutMin", true),
    Dynamic("OutMax", true),
};
constexpr std::array kCrossInputs{
    Fixed("A", MaterialValueType::Float3),
    Fixed("B", MaterialValueType::Float3),
};
constexpr std::array kLengthInputs{Dynamic("Value", true)};
constexpr std::array kDistanceInputs{
    Dynamic("A", true), Dynamic("B", true),
};
constexpr std::array kReflectInputs{
    Fixed("Incident", MaterialValueType::Float3),
    Fixed("Normal", MaterialValueType::Float3),
};
constexpr std::array kMakeFloat2Inputs{
    Fixed("X", MaterialValueType::Float1),
    Fixed("Y", MaterialValueType::Float1),
};
constexpr std::array kMakeFloat3Inputs{
    Fixed("X", MaterialValueType::Float1),
    Fixed("Y", MaterialValueType::Float1),
    Fixed("Z", MaterialValueType::Float1),
};
constexpr std::array kMakeFloat4Inputs{
    Fixed("X", MaterialValueType::Float1),
    Fixed("Y", MaterialValueType::Float1),
    Fixed("Z", MaterialValueType::Float1),
    Fixed("W", MaterialValueType::Float1),
};
constexpr std::array kSplitInputs{Dynamic("Value", true)};
constexpr std::array kSplitOutputs{
    Fixed("X", MaterialValueType::Float1),
    Fixed("Y", MaterialValueType::Float1),
    Fixed("Z", MaterialValueType::Float1),
    Fixed("W", MaterialValueType::Float1),
};
constexpr std::array kSelectInputs{
    Dynamic("False", true), Dynamic("True", true),
    Fixed("Condition", MaterialValueType::Float1),
};
constexpr std::array kTextureCoordinateOutputs{
    Fixed("UV0", MaterialValueType::Float2),
    Fixed("UV1", MaterialValueType::Float2),
};
constexpr std::array kTimeOutputs{
    Fixed("Time", MaterialValueType::Float1),
    Fixed("DeltaTime", MaterialValueType::Float1),
    Fixed("GameDeltaTime", MaterialValueType::Float1),
    Fixed("FrameIndex", MaterialValueType::Float1),
};
constexpr std::array kFloat2Output{
    Fixed("Value", MaterialValueType::Float2),
};
constexpr std::array kFloat3Output{
    Fixed("Value", MaterialValueType::Float3),
};
constexpr std::array kFloat4Output{
    Fixed("Value", MaterialValueType::Float4),
};
constexpr std::array kUvTransformInputs{
    Dynamic("UV", true),
    Fixed("Pivot", MaterialValueType::Float2),
    Fixed("Scale", MaterialValueType::Float2),
    Fixed("Offset", MaterialValueType::Float2),
    Fixed("Rotation", MaterialValueType::Float1),
};
constexpr std::array kNoiseInputs{
    Dynamic("UV", true),
    Fixed("Scale", MaterialValueType::Float1),
};
constexpr std::array kFresnelInputs{
    Fixed("Normal", MaterialValueType::Float3),
    Fixed("View", MaterialValueType::Float3),
    Fixed("F0", MaterialValueType::Float1),
    Fixed("Power", MaterialValueType::Float1),
};

constexpr std::array kDefinitions{
    MaterialNodeDefinition{MaterialNodeKind::Constant, "Constant",
        MaterialValueType::Float1, NodeClass::Input, true, {}, kConstantOutputs, false, OutputRoute::Unrestricted, "Editable scalar/vector constant value."},
    MaterialNodeDefinition{MaterialNodeKind::Parameter, "Parameter",
        MaterialValueType::Float1, NodeClass::Input, true, {}, kParameterOutputs, false, OutputRoute::Unrestricted, "Exposed material parameter editable by name."},
    MaterialNodeDefinition{MaterialNodeKind::TextureSample, "TextureSample",
        MaterialValueType::Float4, NodeClass::Input, true,
        kTextureSampleInputs, kTextureSampleOutputs, false, OutputRoute::Unrestricted, "Samples a bound texture at UV; exposes Color/RGB/R/G/B/A."},
    MaterialNodeDefinition{MaterialNodeKind::Add, "Add",
        MaterialValueType::Float1, NodeClass::Math, true, kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Adds A and B element-wise."},
    MaterialNodeDefinition{MaterialNodeKind::Multiply, "Multiply",
        MaterialValueType::Float1, NodeClass::Math, true, kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Multiplies A and B element-wise."},
    MaterialNodeDefinition{MaterialNodeKind::Lerp, "Lerp",
        MaterialValueType::Float1, NodeClass::Math, true, kLerpInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Blends A toward B by Alpha (0..1)."},
    MaterialNodeDefinition{MaterialNodeKind::Clamp, "Clamp",
        MaterialValueType::Float1, NodeClass::Math, true, kClampInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Clamps Value between Min and Max."},
    MaterialNodeDefinition{MaterialNodeKind::Saturate, "Saturate",
        MaterialValueType::Float1, NodeClass::Math, true, kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Clamps Value to 0..1."},
    MaterialNodeDefinition{MaterialNodeKind::OneMinus, "OneMinus",
        MaterialValueType::Float1, NodeClass::Math, true, kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Returns 1 - Value."},
    MaterialNodeDefinition{MaterialNodeKind::Power, "Power",
        MaterialValueType::Float1, NodeClass::Math, true, kPowerInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Returns Base raised to Exponent."},
    MaterialNodeDefinition{MaterialNodeKind::Dot, "Dot",
        MaterialValueType::Float1, NodeClass::Math, true, kDotInputs, kFloatResult, true, OutputRoute::Unrestricted, "Dot product of two Float3 vectors."},
    MaterialNodeDefinition{MaterialNodeKind::NormalUnpack, "NormalUnpack",
        MaterialValueType::Float3, NodeClass::Math, true,
        kNormalUnpackInputs, kNormalUnpackOutputs, false, OutputRoute::Unrestricted, "Unpacks a packed tangent-space normal."},
    MaterialNodeDefinition{MaterialNodeKind::PbrSurface, "PBRSurface",
        MaterialValueType::Surface, NodeClass::Surface, true,
        kPbrSurfaceInputs, kSurfaceOutput, false, OutputRoute::Unrestricted, "Aggregates PBR surface inputs into a Surface value."},
    MaterialNodeDefinition{MaterialNodeKind::RampDiffuse, "RampDiffuse",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kRampDiffuseInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase, "Stylized diffuse response with shadow/mid/lit ramp controls."},
    MaterialNodeDefinition{MaterialNodeKind::MultiRampDiffuse, "MultiRampDiffuse",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kMultiRampDiffuseInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase, "Multi-row ramp diffuse with layer weights."},
    MaterialNodeDefinition{MaterialNodeKind::SpecularBand, "SpecularBand",
        MaterialValueType::SpecularResponse, NodeClass::Shading, true,
        kSpecularBandInputs, kSpecularResponse, false, OutputRoute::Specular, "Stylized specular response with width/softness/intensity."},
    MaterialNodeDefinition{MaterialNodeKind::FresnelRim, "FresnelRim",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kFresnelInputs, kColorOutput, false, OutputRoute::Additive, "View-dependent rim color response using Fresnel."},
    MaterialNodeDefinition{MaterialNodeKind::MatCap, "MatCap",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kMatCapInputs, kColorOutput, false, OutputRoute::Additive, "MatCap lookup tinted by normal and view direction."},
    MaterialNodeDefinition{MaterialNodeKind::AnisotropicHair, "AnisotropicHair",
        MaterialValueType::SpecularResponse, NodeClass::Shading, true,
        kHairInputs, kSpecularResponse, false, OutputRoute::Specular, "Anisotropic hair specular response."},
    MaterialNodeDefinition{MaterialNodeKind::FaceSdf, "FaceSDF",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kFaceSdfInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase, "Face shadow response driven by an SDF mask."},
    // v5 deliberately uses EyeLayer. EyeLighting belonged to the disconnected
    // v4 additive path and must not be accepted as the current semantics.
    MaterialNodeDefinition{MaterialNodeKind::EyeLighting, "EyeLayer",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kEyeInputs, kColorOutput, false, OutputRoute::Eye, "Eye shading response from control texture and view direction."},
    MaterialNodeDefinition{MaterialNodeKind::SurfaceOutput, "SurfaceOutput",
        MaterialValueType::Surface, NodeClass::Surface, true, kSurfaceOutput, {}, false, OutputRoute::Unrestricted, "Terminal node that receives the final Surface value."},
    MaterialNodeDefinition{MaterialNodeKind::FunctionCall, "FunctionCall",
        MaterialValueType::Float1, NodeClass::Function, false,
        kFunctionInputs, kFunctionOutputs, false, OutputRoute::Unrestricted, "Calls a Material Function asset by GUID."},
    MaterialNodeDefinition{MaterialNodeKind::SkinLighting, "SkinLighting",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kSkinInputs, kDiffuseResponse, false, OutputRoute::DiffuseAfterSkin, "Skin diffuse response with wrap and subsurface controls."},
    MaterialNodeDefinition{MaterialNodeKind::FabricSheen, "FabricSheen",
        MaterialValueType::Float3, NodeClass::Shading, true,
        kFabricInputs, kColorOutput, false, OutputRoute::Additive, "Fabric sheen color response."},
    MaterialNodeDefinition{MaterialNodeKind::HybridLighting, "HybridLighting",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kHybridInputs, kDiffuseResponse, false, OutputRoute::DiffuseFinal, "Blends direct and indirect diffuse/specular responses."},
    MaterialNodeDefinition{MaterialNodeKind::Outline, "Outline",
        MaterialValueType::OutlineResponse, NodeClass::Shading, true,
        kOutlineInputs, kOutlineOutput, false, OutputRoute::Outline, "Outline response with color and thickness."},
    MaterialNodeDefinition{MaterialNodeKind::HairShadow, "HairShadow",
        MaterialValueType::DiffuseResponse, NodeClass::Shading, true,
        kHairShadowInputs, kDiffuseResponse, false, OutputRoute::DiffuseBase, "Hair shadow response layered on diffuse."},
    MaterialNodeDefinition{MaterialNodeKind::CorneaHighlight, "CorneaHighlight",
        MaterialValueType::Float3, NodeClass::Shading, true, {}, kColorOutput,
        false, OutputRoute::Eye, "Eye cornea highlight color response."},
    MaterialNodeDefinition{MaterialNodeKind::Subtract, "Subtract",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Subtracts B from A."},
    MaterialNodeDefinition{MaterialNodeKind::Divide, "Divide",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Divides A by B."},
    MaterialNodeDefinition{MaterialNodeKind::Minimum, "Minimum",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Element-wise minimum of A and B."},
    MaterialNodeDefinition{MaterialNodeKind::Maximum, "Maximum",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Element-wise maximum of A and B."},
    MaterialNodeDefinition{MaterialNodeKind::Absolute, "Absolute",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Absolute value of Value."},
    MaterialNodeDefinition{MaterialNodeKind::Negate, "Negate",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Negates Value."},
    MaterialNodeDefinition{MaterialNodeKind::Floor, "Floor",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Floors each component."},
    MaterialNodeDefinition{MaterialNodeKind::Ceil, "Ceil",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Ceils each component."},
    MaterialNodeDefinition{MaterialNodeKind::Fraction, "Fraction",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Fractional part of each component."},
    MaterialNodeDefinition{MaterialNodeKind::Modulo, "Modulo",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Modulo of A by B."},
    MaterialNodeDefinition{MaterialNodeKind::SquareRoot, "SquareRoot",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Square root of Value."},
    MaterialNodeDefinition{MaterialNodeKind::Sine, "Sine",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Sine of Value (radians)."},
    MaterialNodeDefinition{MaterialNodeKind::Cosine, "Cosine",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Cosine of Value (radians)."},
    MaterialNodeDefinition{MaterialNodeKind::Step, "Step",
        MaterialValueType::Float1, NodeClass::Math, true,
        kStepInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Returns 1 where Value >= Edge, else 0."},
    MaterialNodeDefinition{MaterialNodeKind::SmoothStep, "SmoothStep",
        MaterialValueType::Float1, NodeClass::Math, true,
        kSmoothStepInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Hermite interpolation between Edge0 and Edge1."},
    MaterialNodeDefinition{MaterialNodeKind::Remap, "Remap",
        MaterialValueType::Float1, NodeClass::Math, true,
        kRemapInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Remaps Value from InMin..InMax to OutMin..OutMax."},
    MaterialNodeDefinition{MaterialNodeKind::Cross, "Cross",
        MaterialValueType::Float3, NodeClass::Math, true,
        kCrossInputs, kFloat3Output, true, OutputRoute::Unrestricted, "Cross product of two Float3 vectors."},
    MaterialNodeDefinition{MaterialNodeKind::Length, "Length",
        MaterialValueType::Float1, NodeClass::Math, true,
        kLengthInputs, kFloatResult, true, OutputRoute::Unrestricted, "Length/magnitude of a vector."},
    MaterialNodeDefinition{MaterialNodeKind::Normalize, "Normalize",
        MaterialValueType::Float3, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Normalizes a vector to unit length."},
    MaterialNodeDefinition{MaterialNodeKind::Distance, "Distance",
        MaterialValueType::Float1, NodeClass::Math, true,
        kDistanceInputs, kFloatResult, true, OutputRoute::Unrestricted, "Distance between A and B."},
    MaterialNodeDefinition{MaterialNodeKind::Reflect, "Reflect",
        MaterialValueType::Float3, NodeClass::Math, true,
        kReflectInputs, kFloat3Output, true, OutputRoute::Unrestricted, "Reflects Incident around Normal."},
    MaterialNodeDefinition{MaterialNodeKind::MakeFloat2, "MakeFloat2",
        MaterialValueType::Float2, NodeClass::Math, true,
        kMakeFloat2Inputs, kFloat2Output, true, OutputRoute::Unrestricted, "Combines X/Y scalars into Float2."},
    MaterialNodeDefinition{MaterialNodeKind::MakeFloat3, "MakeFloat3",
        MaterialValueType::Float3, NodeClass::Math, true,
        kMakeFloat3Inputs, kFloat3Output, true, OutputRoute::Unrestricted, "Combines X/Y/Z scalars into Float3."},
    MaterialNodeDefinition{MaterialNodeKind::MakeFloat4, "MakeFloat4",
        MaterialValueType::Float4, NodeClass::Math, true,
        kMakeFloat4Inputs, kFloat4Output, true, OutputRoute::Unrestricted, "Combines X/Y/Z/W scalars into Float4."},
    MaterialNodeDefinition{MaterialNodeKind::Split, "Split",
        MaterialValueType::Float4, NodeClass::Math, true,
        kSplitInputs, kSplitOutputs, true, OutputRoute::Unrestricted, "Splits a vector into X/Y/Z/W scalars."},
    MaterialNodeDefinition{MaterialNodeKind::Select, "Select",
        MaterialValueType::Float1, NodeClass::Math, true,
        kSelectInputs, kDynamicResult, true, OutputRoute::Unrestricted, "Chooses True or False by Condition."},
    MaterialNodeDefinition{MaterialNodeKind::TextureCoordinate, "TextureCoordinate",
        MaterialValueType::Float2, NodeClass::Input, true,
        {}, kTextureCoordinateOutputs, false, OutputRoute::Unrestricted, "Provides UV0/UV1 texture coordinates."},
    MaterialNodeDefinition{MaterialNodeKind::Time, "Time",
        MaterialValueType::Float1, NodeClass::Input, true, {}, kTimeOutputs, false, OutputRoute::Unrestricted, "Provides elapsed/delta time and frame index."},
    MaterialNodeDefinition{MaterialNodeKind::WorldPosition, "WorldPosition",
        MaterialValueType::Float3, NodeClass::Input, true, {}, kFloat3Output, false, OutputRoute::Unrestricted, "World-space position."},
    MaterialNodeDefinition{MaterialNodeKind::WorldNormal, "WorldNormal",
        MaterialValueType::Float3, NodeClass::Input, true, {}, kFloat3Output, false, OutputRoute::Unrestricted, "World-space normal."},
    MaterialNodeDefinition{MaterialNodeKind::WorldTangent, "WorldTangent",
        MaterialValueType::Float3, NodeClass::Input, true, {}, kFloat3Output, false, OutputRoute::Unrestricted, "World-space tangent."},
    MaterialNodeDefinition{MaterialNodeKind::ViewDirection, "ViewDirection",
        MaterialValueType::Float3, NodeClass::Input, true, {}, kFloat3Output, false, OutputRoute::Unrestricted, "Camera-to-pixel view direction."},
    MaterialNodeDefinition{MaterialNodeKind::CameraPosition, "CameraPosition",
        MaterialValueType::Float3, NodeClass::Input, true, {}, kFloat3Output, false, OutputRoute::Unrestricted, "Camera world position."},
    MaterialNodeDefinition{MaterialNodeKind::ScreenPosition, "ScreenPosition",
        MaterialValueType::Float4, NodeClass::Input, true, {}, kFloat4Output, false, OutputRoute::Unrestricted, "Screen-space position."},
    MaterialNodeDefinition{MaterialNodeKind::DerivativeX, "DerivativeX",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, false, OutputRoute::Unrestricted, "Screen-space derivative along X."},
    MaterialNodeDefinition{MaterialNodeKind::DerivativeY, "DerivativeY",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, false, OutputRoute::Unrestricted, "Screen-space derivative along Y."},
    MaterialNodeDefinition{MaterialNodeKind::FilterWidth, "FilterWidth",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kDynamicResult, false, OutputRoute::Unrestricted, "Approximate pixel footprint width."},
    MaterialNodeDefinition{MaterialNodeKind::Atan2, "math.atan2",
        MaterialValueType::Float1, NodeClass::Math, true,
        kBinaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Returns atan2(A, B) angle."},
    MaterialNodeDefinition{MaterialNodeKind::Exp, "math.exp",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Returns e raised to Value."},
    MaterialNodeDefinition{MaterialNodeKind::Log2, "math.log2",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Returns base-2 logarithm of Value."},
    MaterialNodeDefinition{MaterialNodeKind::Sign, "math.sign",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Returns sign of Value (-1, 0 or 1)."},
    MaterialNodeDefinition{MaterialNodeKind::Round, "math.round",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Rounds Value to nearest integer."},
    MaterialNodeDefinition{MaterialNodeKind::Trunc, "math.trunc",
        MaterialValueType::Float1, NodeClass::Math, true,
        kUnaryInputs, kFloatResult, true, OutputRoute::Unrestricted, "Truncates Value toward zero."},
    MaterialNodeDefinition{MaterialNodeKind::UvTransform, "uv.transform",
        MaterialValueType::Float2, NodeClass::Math, true,
        kUvTransformInputs, kFloat2Output, false, OutputRoute::Unrestricted, "Transforms UV by pivot/scale/offset/rotation."},
    MaterialNodeDefinition{MaterialNodeKind::ValueNoise, "noise.value",
        MaterialValueType::Float1, NodeClass::Math, true,
        kNoiseInputs, kFloatResult, false, OutputRoute::Unrestricted, "Value noise from UV; scalar 0..1."},
    MaterialNodeDefinition{MaterialNodeKind::Voronoi, "noise.voronoi",
        MaterialValueType::Float1, NodeClass::Math, true,
        kNoiseInputs, kFloatResult, false, OutputRoute::Unrestricted, "Voronoi distance noise from UV."},
    MaterialNodeDefinition{MaterialNodeKind::Fresnel, "math.fresnel",
        MaterialValueType::Float1, NodeClass::Math, true,
        kGenericFresnelInputs, kFloatResult, false, OutputRoute::Unrestricted, "Fresnel factor from normal/view; scalar 0..1."},
};

static_assert(kDefinitions.size() ==
    static_cast<size_t>(MaterialNodeKind::Fresnel) + 1u);

constexpr bool AllPaletteVisibleNodesHaveSummary() {
    for (const MaterialNodeDefinition& definition : kDefinitions) {
        if (definition.paletteVisible && definition.summary.empty()) {
            return false;
        }
    }
    return true;
}
static_assert(AllPaletteVisibleNodesHaveSummary());

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
