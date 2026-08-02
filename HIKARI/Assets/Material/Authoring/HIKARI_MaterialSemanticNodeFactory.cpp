#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {
namespace {

using namespace std::literals;

constexpr std::array kNoBindings{std::string_view{}};
constexpr std::array kPbrParameters{
    "parameter.baseColor"sv, "parameter.metallic"sv,
    "parameter.roughness"sv, "parameter.specular"sv,
    "parameter.specularColor"sv, "parameter.normalScale"sv,
    "parameter.occlusionStrength"sv, "parameter.emissive"sv,
    "parameter.emissiveStrength"sv};
constexpr std::array kPbrResources{
    "resource.baseColor"sv, "resource.normal"sv,
    "resource.metallicRoughness"sv, "resource.occlusion"sv,
    "resource.emissive"sv, "resource.specular"sv,
    "resource.specularColor"sv};
constexpr std::array kRampParameters{
    "parameter.shadowThreshold"sv, "parameter.shadowSoftness"sv,
    "parameter.midThreshold"sv, "parameter.shadowTint"sv,
    "parameter.midTint"sv, "parameter.litTint"sv,
    "parameter.rampRow"sv, "parameter.rampMaskInfluence"sv,
    "parameter.rampBlend"sv, "parameter.rampIntensity"sv,
    "parameter.toonDirectGain"sv};
constexpr std::array kRampResources{"resource.diffuseRamp"sv};
constexpr std::array kMultiRampResources{
    "resource.diffuseRamp"sv, "resource.secondaryRamp"sv};
constexpr std::array kSpecularBandParameters{
    "parameter.specularBandWidth"sv,
    "parameter.specularBandSoftness"sv,
    "parameter.specularIntensity"sv, "parameter.specularPower"sv,
    "parameter.specularTint"sv};
constexpr std::array kFresnelParameters{
    "parameter.rimColor"sv, "parameter.rimPower"sv,
    "parameter.rimWidth"sv, "parameter.rimIntensity"sv,
    "parameter.rimLightAlignment"sv};
constexpr std::array kMatCapParameters{
    "parameter.matCapTint"sv, "parameter.matCapIntensity"sv};
constexpr std::array kMatCapResources{"resource.matCap"sv};
constexpr std::array kHairParameters{
    "parameter.hairShift"sv, "parameter.hairWidth"sv,
    "parameter.hairSecondaryShift"sv, "parameter.hairIntensity"sv,
    "parameter.hairHighlightTint"sv};
constexpr std::array kHairResources{"resource.hairControl"sv};
constexpr std::array kFaceParameters{
    "parameter.shadowThreshold"sv, "parameter.shadowSoftness"sv,
    "parameter.midThreshold"sv, "parameter.shadowTint"sv,
    "parameter.midTint"sv, "parameter.litTint"sv,
    "parameter.rampRow"sv, "parameter.rampMaskInfluence"sv,
    "parameter.rampBlend"sv, "parameter.rampIntensity"sv,
    "parameter.toonDirectGain"sv, "parameter.faceShadowSoftness"sv,
    "parameter.faceShadowOffset"sv, "parameter.faceRight"sv,
    "parameter.faceForward"sv};
constexpr std::array kFaceResources{
    "resource.diffuseRamp"sv, "resource.faceSdf"sv};
constexpr std::array kEyeParameters{
    "parameter.irisDepth"sv, "parameter.eyeSparkle"sv,
    "parameter.eyeSparklePower"sv};
constexpr std::array kEyeResources{"resource.eyeControl"sv};
constexpr std::array kSkinParameters{
    "parameter.skinWrap"sv, "parameter.skinSubsurface"sv,
    "parameter.skinTint"sv};
constexpr std::array kSkinResources{"resource.thicknessMask"sv};
constexpr std::array kFabricParameters{
    "parameter.fabricSheen"sv, "parameter.fiberRoughness"sv,
    "parameter.fabricSheenPower"sv,
    "parameter.fabricSheenIntensity"sv};
constexpr std::array kFabricResources{"resource.fabricMask"sv};
constexpr std::array kHybridParameters{
    "parameter.directDiffusePbrBlend"sv,
    "parameter.directSpecularPbrBlend"sv,
    "parameter.indirectPbrBlend"sv};
constexpr std::array kOutlineParameters{
    "parameter.outlineMode"sv, "parameter.outlineColor"sv,
    "parameter.outlineThickness"sv};
constexpr std::array kHairShadowParameters{
    "parameter.hairShadowStrength"sv};
constexpr std::array kHairShadowResources{"resource.hairShadow"sv};

template <size_t Size>
std::span<const std::string_view> AsSpan(
    const std::array<std::string_view, Size>& values) noexcept {
    return values;
}

std::span<const std::string_view> EmptyBindings() noexcept {
    return {kNoBindings.data(), 0u};
}

MaterialValueData Value(MaterialValueType type, float x,
                        float y = 0.0f, float z = 0.0f,
                        float w = 0.0f) {
    MaterialValueData value{};
    value.type = type;
    value.numeric = {x, y, z, w};
    return value;
}

void EnsureParameter(MaterialGraphData& graph, std::string_view id,
                     std::string_view name, MaterialValueData value) {
    if (std::any_of(graph.parameters.begin(), graph.parameters.end(),
                    [id](const auto& parameter) {
                        return parameter.id == id;
                    })) {
        return;
    }
    graph.parameters.push_back(MaterialGraphParameterData{
        std::string{id}, std::string{name}, value, true
    });
}

void EnsureResource(MaterialGraphData& graph, std::string_view id,
                    std::string_view name, MaterialResourceType type,
                    MaterialResourceSemantic semantic) {
    if (std::any_of(graph.resources.begin(), graph.resources.end(),
                    [id](const auto& resource) {
                        return resource.id == id;
                    })) {
        return;
    }
    MaterialGraphResourceData resource{};
    resource.id = id;
    resource.displayName = name;
    resource.type = type;
    resource.semantic = semantic;
    graph.resources.push_back(std::move(resource));
}

void EnsureRamp(MaterialGraphData& graph, bool multiRamp) {
    EnsureParameter(graph, "parameter.shadowThreshold", "Shadow Threshold",
                    Value(MaterialValueType::Float1, 0.46f));
    EnsureParameter(graph, "parameter.shadowSoftness", "Shadow Softness",
                    Value(MaterialValueType::Float1, 0.025f));
    EnsureParameter(graph, "parameter.midThreshold", "Midtone Threshold",
                    Value(MaterialValueType::Float1, 0.70f));
    EnsureParameter(graph, "parameter.shadowTint", "Shadow Tint",
                    Value(MaterialValueType::Float3, 0.38f, 0.45f, 0.62f));
    EnsureParameter(graph, "parameter.midTint", "Midtone Tint",
                    Value(MaterialValueType::Float3, 0.72f, 0.78f, 0.88f));
    EnsureParameter(graph, "parameter.litTint", "Lit Tint",
                    Value(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
    EnsureParameter(graph, "parameter.rampRow", "Ramp Row",
                    Value(MaterialValueType::Float1, 0.5f));
    EnsureParameter(graph, "parameter.rampMaskInfluence", "Ramp Mask Influence",
                    Value(MaterialValueType::Float1, 0.0f));
    EnsureParameter(graph, "parameter.rampBlend", "Secondary Ramp Blend",
                    Value(MaterialValueType::Float1, multiRamp ? 0.5f : 0.0f));
    EnsureParameter(graph, "parameter.rampIntensity", "Ramp Influence",
                    Value(MaterialValueType::Float1, 1.0f));
    EnsureParameter(graph, "parameter.toonDirectGain", "Direct Light Gain",
                    Value(MaterialValueType::Float1, 1.0f));
    EnsureResource(graph, "resource.diffuseRamp", "Diffuse Ramp",
                   multiRamp ? MaterialResourceType::Ramp2D
                             : MaterialResourceType::Ramp1D,
                   MaterialResourceSemantic::Ramp);
    if (multiRamp) {
        EnsureResource(graph, "resource.secondaryRamp", "Secondary Ramp",
                       MaterialResourceType::Ramp2D,
                       MaterialResourceSemantic::Ramp);
    }
}

} // namespace

const MaterialNodeSemanticContract& GetMaterialNodeSemanticContract(
    MaterialNodeKind kind) noexcept {
    static constexpr MaterialNodeSemanticContract kConstant{
        "Any compatible numeric input", "The literal value on this node",
        "A constant numeric value", "Uses the displayed literal",
        "Type-incompatible links are rejected"};
    static constexpr MaterialNodeSemanticContract kParameter{
        "Any compatible numeric input", "The referenced material parameter",
        "An exposed editable value", "Uses the parameter default",
        "A missing parameter reference is a compile error"};
    static constexpr MaterialNodeSemanticContract kTexture{
        "PBR texture inputs or documented feature mask inputs",
        "The selected texture, UV set and UV transform",
        "Color and individual sampled channels", "No generated image",
        "A reachable Texture Sample without a valid asset fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired};
    static constexpr MaterialNodeSemanticContract kMath{
        "Any compatible numeric input", "Its connected numeric operands",
        "A statically evaluable numeric result", "All required inputs must link",
        "Unsupported dynamic expressions fail compilation"};
    static constexpr MaterialNodeSemanticContract kNormalUnpack{
        "PBR Surface / Normal", "A connected packed tangent-space normal",
        "An unpacked tangent-space normal", "No normal texture is generated",
        "A missing or non-texture input fails compilation"};
    static constexpr MaterialNodeSemanticContract kPbr{
        "Surface Output / Surface",
        "Base color, normal, metal, roughness, occlusion, emissive and connected lighting responses",
        "The final bounded game surface", "Uses its displayed factors and optional imported textures",
        "Unsupported response routes fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride};
    static constexpr MaterialNodeSemanticContract kRamp{
        "PBR Surface / Diffuse Response",
        "NdotL, scene shadows, Ramp colors, thresholds, mask and direct-light gain",
        "A stepped diffuse lighting response",
        "Uses an editable analytic Ramp until a Ramp asset is assigned",
        "Invalid connected masks or Ramp assets are diagnosed",
        MaterialNodeResourcePolicy::GeneratedDefault,
        "resource.diffuseRamp"};
    static constexpr MaterialNodeSemanticContract kSpecularBand{
        "PBR Surface / Specular Response",
        "Normal, view/light half-vector, width, softness, power, intensity and tint",
        "A quantized specular highlight", "Uses the displayed analytic band",
        "Unsupported dynamic inputs fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kFresnel{
        "PBR Surface / Additive, directly or through Add",
        "Normal, view direction, light alignment, color, width and power",
        "An additive Fresnel rim", "Uses the displayed analytic rim",
        "Non-additive destinations fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kMatCap{
        "PBR Surface / Additive, directly or through Add",
        "View-space normal, mask, MatCap image, tint and intensity",
        "An additive view-space MatCap lobe", "No style image is invented",
        "A reachable MatCap without a valid MatCap texture fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired,
        "resource.matCap"};
    static constexpr MaterialNodeSemanticContract kHair{
        "PBR Surface / Specular Response",
        "Generated mesh tangent, light/view directions, shifts, width, tint and optional flow map",
        "A two-band anisotropic hair highlight",
        "Uses generated mesh tangents; RGBA hair control is an optional override",
        "Degenerate UVs receive a stable normal-derived tangent fallback",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.hairControl"};
    static constexpr MaterialNodeSemanticContract kFace{
        "PBR Surface / Diffuse Response",
        "Face SDF, character right/forward axes, main light and Ramp controls",
        "A face-oriented toon diffuse response", "No face-specific field is generated",
        "A reachable Face SDF without a valid linear Face SDF texture fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired,
        "resource.faceSdf"};
    static constexpr MaterialNodeSemanticContract kEye{
        "PBR Surface / Additive, directly or through Add",
        "View direction, iris depth, catchlight intensity/power and optional eye-control map",
        "A procedural eye/cornea catchlight lobe",
        "Uses a procedural catchlight; RGBA eye control optionally specializes it",
        "Invalid connected masks fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault,
        "resource.eyeControl"};
    static constexpr MaterialNodeSemanticContract kSkin{
        "PBR Surface or Hybrid / Diffuse Response",
        "Upstream diffuse response, wrap, subsurface tint and optional thickness mask",
        "A wrapped skin diffuse response", "Uses a neutral thickness mask",
        "Unsupported response or mask routes fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.thicknessMask"};
    static constexpr MaterialNodeSemanticContract kFabric{
        "PBR Surface / Additive, directly or through Add",
        "View angle, sheen, fiber roughness, tint and optional fabric mask",
        "An additive fabric sheen", "Uses a neutral fabric mask",
        "Non-additive destinations fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.fabricMask"};
    static constexpr MaterialNodeSemanticContract kHybrid{
        "PBR Surface / Diffuse Response",
        "Upstream NPR diffuse plus direct diffuse/specular and indirect PBR blend weights",
        "A controlled PBR + NPR lighting response", "Uses the displayed blend weights",
        "Missing or unsupported upstream response links fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kOutline{
        "PBR Surface / Outline",
        "Mode, color, thickness and the selected outline backend",
        "A material outline feature request", "Uses the displayed outline profile",
        "An unavailable backend is reported instead of changing base shading",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kHairShadow{
        "PBR Surface, Skin or Hybrid / Diffuse Response",
        "An upstream toon diffuse response, linear R hair-shadow visibility and strength",
        "A texture-authored hair-shadow-modulated diffuse response",
        "No character hair-shadow shape is invented",
        "A reachable Hair Shadow Mask without a valid linear R texture fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired,
        "resource.hairShadow"};
    static constexpr MaterialNodeSemanticContract kOutput{
        "Graph root", "The connected Surface", "The material's only runtime output",
        "Requires exactly one Surface connection",
        "Missing or multiple output ownership fails compilation"};
    static constexpr MaterialNodeSemanticContract kFunction{
        "A compatible function destination", "The referenced Material Function",
        "The function result", "No fallback function is inserted",
        "Runtime Material Functions are currently rejected explicitly"};

    switch (kind) {
    case MaterialNodeKind::Constant: return kConstant;
    case MaterialNodeKind::Parameter: return kParameter;
    case MaterialNodeKind::TextureSample: return kTexture;
    case MaterialNodeKind::Add:
    case MaterialNodeKind::Multiply:
    case MaterialNodeKind::Lerp:
    case MaterialNodeKind::Clamp:
    case MaterialNodeKind::Saturate:
    case MaterialNodeKind::OneMinus:
    case MaterialNodeKind::Power:
    case MaterialNodeKind::Dot:
        return kMath;
    case MaterialNodeKind::NormalUnpack: return kNormalUnpack;
    case MaterialNodeKind::PbrSurface: return kPbr;
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse:
        return kRamp;
    case MaterialNodeKind::SpecularBand: return kSpecularBand;
    case MaterialNodeKind::FresnelRim: return kFresnel;
    case MaterialNodeKind::MatCap: return kMatCap;
    case MaterialNodeKind::AnisotropicHair: return kHair;
    case MaterialNodeKind::FaceSdf: return kFace;
    case MaterialNodeKind::EyeLighting: return kEye;
    case MaterialNodeKind::SkinLighting: return kSkin;
    case MaterialNodeKind::FabricSheen: return kFabric;
    case MaterialNodeKind::HybridLighting: return kHybrid;
    case MaterialNodeKind::Outline: return kOutline;
    case MaterialNodeKind::HairShadow: return kHairShadow;
    case MaterialNodeKind::SurfaceOutput: return kOutput;
    case MaterialNodeKind::FunctionCall: return kFunction;
    }
    return kConstant;
}

std::span<const std::string_view> GetMaterialNodeOwnedParameterIds(
    MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface: return AsSpan(kPbrParameters);
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse: return AsSpan(kRampParameters);
    case MaterialNodeKind::SpecularBand: return AsSpan(kSpecularBandParameters);
    case MaterialNodeKind::FresnelRim: return AsSpan(kFresnelParameters);
    case MaterialNodeKind::MatCap: return AsSpan(kMatCapParameters);
    case MaterialNodeKind::AnisotropicHair: return AsSpan(kHairParameters);
    case MaterialNodeKind::FaceSdf: return AsSpan(kFaceParameters);
    case MaterialNodeKind::EyeLighting: return AsSpan(kEyeParameters);
    case MaterialNodeKind::SkinLighting: return AsSpan(kSkinParameters);
    case MaterialNodeKind::FabricSheen: return AsSpan(kFabricParameters);
    case MaterialNodeKind::HybridLighting: return AsSpan(kHybridParameters);
    case MaterialNodeKind::Outline: return AsSpan(kOutlineParameters);
    case MaterialNodeKind::HairShadow: return AsSpan(kHairShadowParameters);
    default: return EmptyBindings();
    }
}

std::span<const std::string_view> GetMaterialNodeOwnedResourceIds(
    MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface: return AsSpan(kPbrResources);
    case MaterialNodeKind::RampDiffuse: return AsSpan(kRampResources);
    case MaterialNodeKind::MultiRampDiffuse: return AsSpan(kMultiRampResources);
    case MaterialNodeKind::MatCap: return AsSpan(kMatCapResources);
    case MaterialNodeKind::AnisotropicHair: return AsSpan(kHairResources);
    case MaterialNodeKind::FaceSdf: return AsSpan(kFaceResources);
    case MaterialNodeKind::EyeLighting: return AsSpan(kEyeResources);
    case MaterialNodeKind::SkinLighting: return AsSpan(kSkinResources);
    case MaterialNodeKind::FabricSheen: return AsSpan(kFabricResources);
    case MaterialNodeKind::HairShadow: return AsSpan(kHairShadowResources);
    default: return EmptyBindings();
    }
}

bool IsReadyMadeMaterialNode(MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface:
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse:
    case MaterialNodeKind::SpecularBand:
    case MaterialNodeKind::FresnelRim:
    case MaterialNodeKind::MatCap:
    case MaterialNodeKind::AnisotropicHair:
    case MaterialNodeKind::FaceSdf:
    case MaterialNodeKind::EyeLighting:
    case MaterialNodeKind::SkinLighting:
    case MaterialNodeKind::FabricSheen:
    case MaterialNodeKind::HybridLighting:
    case MaterialNodeKind::Outline:
    case MaterialNodeKind::HairShadow:
        return true;
    default:
        return false;
    }
}

bool IsSingletonReadyMadeMaterialNode(MaterialNodeKind kind) noexcept {
    return IsReadyMadeMaterialNode(kind) ||
        kind == MaterialNodeKind::SurfaceOutput;
}

std::string_view ReadyMadeMaterialNodeDisplayName(
    MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface: return "PBR Surface";
    case MaterialNodeKind::RampDiffuse: return "Ramp Diffuse";
    case MaterialNodeKind::MultiRampDiffuse: return "Multi-Ramp Diffuse";
    case MaterialNodeKind::SpecularBand: return "Specular Band";
    case MaterialNodeKind::FresnelRim: return "Fresnel Rim";
    case MaterialNodeKind::MatCap: return "MatCap";
    case MaterialNodeKind::AnisotropicHair: return "Anime Hair Highlight";
    case MaterialNodeKind::FaceSdf: return "Anime Face SDF";
    case MaterialNodeKind::EyeLighting: return "Anime Eye";
    case MaterialNodeKind::SkinLighting: return "Skin Lighting";
    case MaterialNodeKind::FabricSheen: return "Fabric Sheen";
    case MaterialNodeKind::HybridLighting: return "PBR + NPR Hybrid";
    case MaterialNodeKind::Outline: return "Outline";
    case MaterialNodeKind::HairShadow: return "Hair Shadow Mask";
    default: return ToString(kind);
    }
}

void InitializeReadyMadeMaterialNode(MaterialAssetData& material,
                                     MaterialGraphNodeData& node) {
    if (!IsReadyMadeMaterialNode(node.kind)) {
        return;
    }
    MaterialGraphData& graph = material.graph;
    switch (node.kind) {
    case MaterialNodeKind::PbrSurface:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Surface;
        EnsureParameter(graph, "parameter.baseColor", "Base Color",
                        Value(MaterialValueType::Float4,
                              material.pbr.baseColorFactor.x,
                              material.pbr.baseColorFactor.y,
                              material.pbr.baseColorFactor.z,
                              material.pbr.baseColorFactor.w));
        EnsureParameter(graph, "parameter.metallic", "Metallic",
                        Value(MaterialValueType::Float1,
                              material.pbr.metallicFactor));
        EnsureParameter(graph, "parameter.roughness", "Roughness",
                        Value(MaterialValueType::Float1,
                              material.pbr.roughnessFactor));
        EnsureParameter(graph, "parameter.specular", "Specular",
                        Value(MaterialValueType::Float1,
                              material.pbr.specularFactor));
        EnsureParameter(graph, "parameter.specularColor", "Specular Color",
                        Value(MaterialValueType::Float3,
                              material.pbr.specularColorFactor.x,
                              material.pbr.specularColorFactor.y,
                              material.pbr.specularColorFactor.z));
        EnsureParameter(graph, "parameter.normalScale", "Normal Scale",
                        Value(MaterialValueType::Float1,
                              material.pbr.normalScale));
        EnsureParameter(graph, "parameter.occlusionStrength", "Occlusion Strength",
                        Value(MaterialValueType::Float1,
                              material.pbr.occlusionStrength));
        EnsureParameter(graph, "parameter.emissive", "Emissive",
                        Value(MaterialValueType::Float3,
                              material.pbr.emissiveFactor.x,
                              material.pbr.emissiveFactor.y,
                              material.pbr.emissiveFactor.z));
        EnsureParameter(graph, "parameter.emissiveStrength", "Emissive Strength",
                        Value(MaterialValueType::Float1,
                              material.pbr.emissiveStrength));
        EnsureResource(graph, "resource.baseColor", "Base Color",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::BaseColor);
        EnsureResource(graph, "resource.normal", "Normal",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Normal);
        EnsureResource(graph, "resource.metallicRoughness", "Metallic Roughness",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::MetallicRoughness);
        EnsureResource(graph, "resource.occlusion", "Occlusion",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Occlusion);
        EnsureResource(graph, "resource.emissive", "Emissive",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Emissive);
        EnsureResource(graph, "resource.specular", "Specular",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Specular);
        EnsureResource(graph, "resource.specularColor", "Specular Color",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::SpecularColor);
        break;
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse: {
        const bool multiRamp = node.kind == MaterialNodeKind::MultiRampDiffuse;
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureRamp(graph, multiRamp);
        node.referenceId = "resource.diffuseRamp";
        break;
    }
    case MaterialNodeKind::SpecularBand:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::SpecularResponse;
        EnsureParameter(graph, "parameter.specularBandWidth", "Band Width",
                        Value(MaterialValueType::Float1, 0.075f));
        EnsureParameter(graph, "parameter.specularBandSoftness", "Band Softness",
                        Value(MaterialValueType::Float1, 0.012f));
        EnsureParameter(graph, "parameter.specularIntensity", "Band Intensity",
                        Value(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.specularPower", "Band Power",
                        Value(MaterialValueType::Float1, 96.0f));
        EnsureParameter(graph, "parameter.specularTint", "Band Tint",
                        Value(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
        break;
    case MaterialNodeKind::FresnelRim:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.rimColor", "Rim Color",
                        Value(MaterialValueType::Float3, 0.68f, 0.78f, 1.0f));
        EnsureParameter(graph, "parameter.rimPower", "Rim Power",
                        Value(MaterialValueType::Float1, 4.0f));
        EnsureParameter(graph, "parameter.rimWidth", "Rim Width",
                        Value(MaterialValueType::Float1, 0.22f));
        EnsureParameter(graph, "parameter.rimIntensity", "Rim Intensity",
                        Value(MaterialValueType::Float1, 0.28f));
        EnsureParameter(graph, "parameter.rimLightAlignment", "Light Alignment",
                        Value(MaterialValueType::Float1, 0.35f));
        break;
    case MaterialNodeKind::MatCap:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.matCapTint", "MatCap Tint",
                        Value(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
        EnsureParameter(graph, "parameter.matCapIntensity", "MatCap Intensity",
                        Value(MaterialValueType::Float1, 0.25f));
        EnsureResource(graph, "resource.matCap", "MatCap",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::MatCap);
        node.referenceId = "resource.matCap";
        break;
    case MaterialNodeKind::AnisotropicHair:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::SpecularResponse;
        EnsureParameter(graph, "parameter.hairShift", "Primary Shift",
                        Value(MaterialValueType::Float1, 0.08f));
        EnsureParameter(graph, "parameter.hairWidth", "Highlight Width",
                        Value(MaterialValueType::Float1, 0.18f));
        EnsureParameter(graph, "parameter.hairSecondaryShift", "Secondary Shift",
                        Value(MaterialValueType::Float1, -0.10f));
        EnsureParameter(graph, "parameter.hairIntensity", "Highlight Intensity",
                        Value(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.hairHighlightTint", "Hair Highlight Tint",
                        Value(MaterialValueType::Float3, 1.0f, 0.92f, 0.84f));
        EnsureResource(graph, "resource.hairControl", "Hair Control (RG flow, B mask, A shift)",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Mask);
        node.referenceId = "resource.hairControl";
        break;
    case MaterialNodeKind::FaceSdf:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureRamp(graph, false);
        EnsureParameter(graph, "parameter.faceShadowSoftness", "Shadow Softness",
                        Value(MaterialValueType::Float1, 0.04f));
        EnsureParameter(graph, "parameter.faceShadowOffset", "Shadow Offset",
                        Value(MaterialValueType::Float1, 0.0f));
        EnsureParameter(graph, "parameter.faceRight", "Face Local Right",
                        Value(MaterialValueType::Float3, 1.0f, 0.0f, 0.0f));
        EnsureParameter(graph, "parameter.faceForward", "Face Local Forward",
                        Value(MaterialValueType::Float3, 0.0f, 0.0f, 1.0f));
        EnsureResource(graph, "resource.faceSdf", "Face SDF (linear R)",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::FaceSdf);
        node.referenceId = "resource.faceSdf";
        break;
    case MaterialNodeKind::EyeLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.irisDepth", "Iris Depth",
                        Value(MaterialValueType::Float1, 0.12f));
        EnsureParameter(graph, "parameter.eyeSparkle", "Catchlight Intensity",
                        Value(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.eyeSparklePower", "Cornea Power",
                        Value(MaterialValueType::Float1, 64.0f));
        EnsureResource(graph, "resource.eyeControl", "Eye Control (R iris, G cornea, B catchlight, A depth)",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Mask);
        node.referenceId = "resource.eyeControl";
        break;
    case MaterialNodeKind::SkinLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.skinWrap", "Light Wrap",
                        Value(MaterialValueType::Float1, 0.18f));
        EnsureParameter(graph, "parameter.skinSubsurface", "Subsurface",
                        Value(MaterialValueType::Float1, 0.12f));
        EnsureParameter(graph, "parameter.skinTint", "Scatter Tint",
                        Value(MaterialValueType::Float3, 1.0f, 0.42f, 0.32f));
        EnsureResource(graph, "resource.thicknessMask", "Thickness Mask",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Mask);
        node.referenceId = "resource.thicknessMask";
        break;
    case MaterialNodeKind::FabricSheen:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.fabricSheen", "Sheen",
                        Value(MaterialValueType::Float1, 0.28f));
        EnsureParameter(graph, "parameter.fiberRoughness", "Fiber Roughness",
                        Value(MaterialValueType::Float1, 0.72f));
        EnsureParameter(graph, "parameter.fabricSheenPower", "Sheen Power",
                        Value(MaterialValueType::Float1, 4.0f));
        EnsureParameter(graph, "parameter.fabricSheenIntensity", "Sheen Intensity",
                        Value(MaterialValueType::Float1, 1.0f));
        EnsureResource(graph, "resource.fabricMask", "Fabric Mask",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::Mask);
        node.referenceId = "resource.fabricMask";
        break;
    case MaterialNodeKind::HybridLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.directDiffusePbrBlend", "Diffuse PBR Blend",
                        Value(MaterialValueType::Float1, 0.55f));
        EnsureParameter(graph, "parameter.directSpecularPbrBlend", "Specular PBR Blend",
                        Value(MaterialValueType::Float1, 0.55f));
        EnsureParameter(graph, "parameter.indirectPbrBlend", "Indirect PBR Blend",
                        Value(MaterialValueType::Float1, 1.0f));
        break;
    case MaterialNodeKind::Outline:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::OutlineResponse;
        EnsureParameter(graph, "parameter.outlineMode", "Outline Mode",
                        Value(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.outlineColor", "Outline Color",
                        Value(MaterialValueType::Float3, 0.025f, 0.030f, 0.045f));
        EnsureParameter(graph, "parameter.outlineThickness", "Geometry Thickness",
                        Value(MaterialValueType::Float1, 1.25f));
        break;
    case MaterialNodeKind::HairShadow:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.hairShadowStrength",
                        "Hair Shadow Strength",
                        Value(MaterialValueType::Float1, 0.65f));
        EnsureResource(graph, "resource.hairShadow",
                       "Hair Shadow Visibility (linear R)",
                       MaterialResourceType::Texture2D,
                       MaterialResourceSemantic::HairShadow);
        node.referenceId = "resource.hairShadow";
        break;
    default:
        break;
    }
    node.value.type = node.valueType;
}

} // namespace HIKARI::ASSETS::MATERIAL
