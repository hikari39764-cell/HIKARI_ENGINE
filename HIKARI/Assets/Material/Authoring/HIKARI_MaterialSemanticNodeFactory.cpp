#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {
namespace {

using namespace std::literals;

constexpr std::array kNoBindings{std::string_view{}};
constexpr std::array kPbrParameters{"parameter.baseColor"sv,
                                    "parameter.metallic"sv,
                                    "parameter.roughness"sv,
                                    "parameter.specular"sv,
                                    "parameter.specularColor"sv,
                                    "parameter.normalScale"sv,
                                    "parameter.occlusionStrength"sv,
                                    "parameter.emissive"sv,
                                    "parameter.emissiveStrength"sv,
                                    "parameter.clearCoat"sv,
                                    "parameter.clearCoatRoughness"sv,
                                    "parameter.transmission"sv,
                                    "parameter.ior"sv,
                                    "parameter.subsurface"sv,
                                    "parameter.subsurfaceRadius"sv};
constexpr std::array kRampParameters{
    "parameter.shadowThreshold"sv,  "parameter.shadowSoftness"sv,    "parameter.midThreshold"sv,
    "parameter.shadowTint"sv,       "parameter.midTint"sv,           "parameter.litTint"sv,
    "parameter.rampRow"sv,          "parameter.rampMaskInfluence"sv, "parameter.rampBlend"sv,
    "parameter.rampLayerWeights"sv, "parameter.rampIntensity"sv,     "parameter.toonDirectGain"sv};
constexpr std::array kRampResources{"resource.diffuseRamp"sv};
constexpr std::array kMultiRampResources{"resource.diffuseRamp"sv, "resource.secondaryRamp"sv,
                                         "resource.tertiaryRamp"sv, "resource.quaternaryRamp"sv};
constexpr std::array kSpecularBandParameters{"parameter.specularBandWidth"sv, "parameter.specularBandSoftness"sv,
                                             "parameter.specularIntensity"sv, "parameter.specularPower"sv,
                                             "parameter.specularTint"sv};
constexpr std::array kFresnelParameters{"parameter.rimColor"sv, "parameter.rimPower"sv, "parameter.rimWidth"sv,
                                        "parameter.rimIntensity"sv, "parameter.rimLightAlignment"sv};
constexpr std::array kMatCapParameters{"parameter.matCapTint"sv, "parameter.matCapIntensity"sv,
                                       "parameter.matCapViewCorrection"sv, "parameter.matCapSphericalNormalBlend"sv};
constexpr std::array kHairParameters{"parameter.hairShift"sv,
                                     "parameter.hairWidth"sv,
                                     "parameter.hairSecondaryShift"sv,
                                     "parameter.hairIntensity"sv,
                                     "parameter.hairPrimaryPower"sv,
                                     "parameter.hairSecondaryPower"sv,
                                     "parameter.hairSecondaryIntensity"sv,
                                     "parameter.hairFlowStrength"sv,
                                     "parameter.hairHighlightTint"sv};
constexpr std::array kFaceParameters{
    "parameter.shadowThreshold"sv,  "parameter.shadowSoftness"sv,    "parameter.midThreshold"sv,
    "parameter.shadowTint"sv,       "parameter.midTint"sv,           "parameter.litTint"sv,
    "parameter.rampRow"sv,          "parameter.rampMaskInfluence"sv, "parameter.rampBlend"sv,
    "parameter.rampIntensity"sv,    "parameter.toonDirectGain"sv,    "parameter.faceShadowSoftness"sv,
    "parameter.faceShadowOffset"sv, "parameter.faceRight"sv,         "parameter.faceForward"sv};
constexpr std::array kFaceResources{"resource.diffuseRamp"sv};
constexpr std::array kEyeParameters{"parameter.irisDepth"sv,
                                    "parameter.eyeSparkle"sv,
                                    "parameter.eyeSparklePower"sv,
                                    "parameter.irisCenter"sv,
                                    "parameter.irisRadius"sv,
                                    "parameter.irisMaskSoftness"sv,
                                    "parameter.eyeLayerMode"sv,
                                    "parameter.eyeHighlightOpacity"sv,
                                    "parameter.eyeHighlightThreshold"sv,
                                    "parameter.eyeHighlightSoftness"sv,
                                    "parameter.eyeParallaxScale"sv,
                                    "parameter.eyeHighlightIntensity"sv,
                                    "parameter.eyeBaseLift"sv,
                                    "parameter.eyeCausticIntensity"sv,
                                    "parameter.eyeCausticTint"sv,
                                    "parameter.eyeCausticWidth"sv};
constexpr std::array kCorneaHighlightParameters{"parameter.irisDepth"sv,
                                                "parameter.irisCenter"sv,
                                                "parameter.irisRadius"sv,
                                                "parameter.irisMaskSoftness"sv,
                                                "parameter.eyeLayerMode"sv,
                                                "parameter.eyeHighlightOpacity"sv,
                                                "parameter.eyeHighlightThreshold"sv,
                                                "parameter.eyeHighlightSoftness"sv,
                                                "parameter.eyeParallaxScale"sv,
                                                "parameter.eyeHighlightIntensity"sv,
                                                "parameter.corneaHighlightMode"sv,
                                                "parameter.corneaHighlightTravel"sv,
                                                "parameter.corneaHighlightViewInfluence"sv,
                                                "parameter.corneaHighlightLightInfluence"sv,
                                                "parameter.corneaHighlightCurvature"sv,
                                                "parameter.corneaHighlightMinBrightness"sv,
                                                "parameter.corneaHighlightLightResponse"sv,
                                                "parameter.corneaHighlightEdgeFade"sv,
                                                "parameter.corneaHighlightSharpness"sv,
                                                "parameter.corneaHighlightLightColor"sv};
constexpr std::array kSkinParameters{"parameter.skinWrap"sv, "parameter.skinSubsurface"sv, "parameter.skinTint"sv};
constexpr std::array kFabricParameters{"parameter.fabricSheen"sv, "parameter.fiberRoughness"sv,
                                       "parameter.fabricSheenPower"sv, "parameter.fabricSheenIntensity"sv};
constexpr std::array kHybridParameters{"parameter.directDiffusePbrBlend"sv, "parameter.directSpecularPbrBlend"sv,
                                       "parameter.indirectPbrBlend"sv};
constexpr std::array kOutlineParameters{"parameter.outlineMode"sv, "parameter.outlineColor"sv,
                                        "parameter.outlineThickness"sv};
constexpr std::array kHairShadowParameters{"parameter.hairShadowStrength"sv};
constexpr std::array kShellResources{"resource.strokeMask"sv, "resource.strokeNormal"sv};

template <size_t Size>
std::span<const std::string_view> AsSpan(const std::array<std::string_view, Size>& values) noexcept {
    return values;
}

std::span<const std::string_view> EmptyBindings() noexcept { return {kNoBindings.data(), 0u}; }

void EnsureParameter(MaterialGraphData& graph, std::string_view id, std::string_view name, MaterialValueData value) {
    if (std::any_of(graph.parameters.begin(), graph.parameters.end(),
                    [id](const auto& parameter) { return parameter.id == id; })) {
        return;
    }
    graph.parameters.push_back(MaterialGraphParameterData{std::string{id}, std::string{name}, value, true});
}

void EnsureResource(MaterialGraphData& graph, std::string_view id, std::string_view name, MaterialResourceType type,
                    MaterialResourceSemantic semantic) {
    if (std::any_of(graph.resources.begin(), graph.resources.end(),
                    [id](const auto& resource) { return resource.id == id; })) {
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
                    MakeMaterialValue(MaterialValueType::Float1, 0.46f));
    EnsureParameter(graph, "parameter.shadowSoftness", "Shadow Softness",
                    MakeMaterialValue(MaterialValueType::Float1, 0.025f));
    EnsureParameter(graph, "parameter.midThreshold", "Midtone Threshold",
                    MakeMaterialValue(MaterialValueType::Float1, 0.70f));
    EnsureParameter(graph, "parameter.shadowTint", "Shadow Tint",
                    MakeMaterialValue(MaterialValueType::Float3, 0.38f, 0.45f, 0.62f));
    EnsureParameter(graph, "parameter.midTint", "Midtone Tint",
                    MakeMaterialValue(MaterialValueType::Float3, 0.72f, 0.78f, 0.88f));
    EnsureParameter(graph, "parameter.litTint", "Lit Tint",
                    MakeMaterialValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
    EnsureParameter(graph, "parameter.rampRow", "Ramp Row", MakeMaterialValue(MaterialValueType::Float1, 0.5f));
    EnsureParameter(graph, "parameter.rampMaskInfluence", "Ramp Mask Influence",
                    MakeMaterialValue(MaterialValueType::Float1, 0.0f));
    EnsureParameter(graph, "parameter.rampBlend", "Secondary Ramp Blend",
                    MakeMaterialValue(MaterialValueType::Float1, multiRamp ? 0.5f : 0.0f));
    EnsureParameter(graph, "parameter.rampLayerWeights", "Ramp Layer Weights",
                    MakeMaterialValue(MaterialValueType::Float4, 1.0f, 0.0f, 0.0f, 0.0f));
    EnsureParameter(graph, "parameter.rampIntensity", "Ramp Influence",
                    MakeMaterialValue(MaterialValueType::Float1, 1.0f));
    EnsureParameter(graph, "parameter.toonDirectGain", "Direct Light Gain",
                    MakeMaterialValue(MaterialValueType::Float1, 1.0f));
    EnsureResource(graph, "resource.diffuseRamp", "Diffuse Ramp",
                   multiRamp ? MaterialResourceType::Ramp2D : MaterialResourceType::Ramp1D,
                   MaterialResourceSemantic::Ramp);
    if (multiRamp) {
        EnsureResource(graph, "resource.secondaryRamp", "Secondary Ramp", MaterialResourceType::Ramp2D,
                       MaterialResourceSemantic::Ramp);
        EnsureResource(graph, "resource.tertiaryRamp", "Tertiary Ramp", MaterialResourceType::Ramp2D,
                       MaterialResourceSemantic::Ramp);
        EnsureResource(graph, "resource.quaternaryRamp", "Quaternary Ramp", MaterialResourceType::Ramp2D,
                       MaterialResourceSemantic::Ramp);
    }
}

} // namespace

const MaterialNodeSemanticContract& GetMaterialNodeSemanticContract(MaterialNodeKind kind) noexcept {
    static constexpr MaterialNodeSemanticContract kConstant{
        "Any compatible numeric input", "The literal value on this node", "A constant numeric value",
        "Uses the displayed literal", "Type-incompatible links are rejected"};
    static constexpr MaterialNodeSemanticContract kParameter{
        "Any compatible numeric input", "The referenced material parameter", "An exposed editable value",
        "Uses the parameter default", "A missing parameter reference is a compile error"};
    static constexpr MaterialNodeSemanticContract kTexture{
        "PBR texture inputs or documented feature mask inputs",
        "The selected texture, UV set and UV transform",
        "Color and individual sampled channels",
        "No generated image",
        "A reachable Texture Sample without a valid asset fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired};
    static constexpr MaterialNodeSemanticContract kMath{
        "Any compatible numeric input", "Its connected numeric operands", "A statically evaluable numeric result",
        "All required inputs must link", "Unsupported dynamic expressions fail compilation"};
    static constexpr MaterialNodeSemanticContract kNormalUnpack{
        "PBR Surface / Normal", "A connected packed tangent-space normal", "An unpacked tangent-space normal",
        "No normal texture is generated", "A missing or non-texture input fails compilation"};
    static constexpr MaterialNodeSemanticContract kPbr{
        "Surface Output / Surface",
        "Base color, normal, metal, roughness, occlusion, emissive and connected lighting responses",
        "The final bounded game surface",
        "Uses displayed factors; textures only apply through reachable Texture Sample links",
        "Unsupported response routes fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride};
    static constexpr MaterialNodeSemanticContract kRamp{
        "PBR Surface / Diffuse Response",
        "NdotL, scene shadows, up to four weighted Ramp layers, thresholds, mask and direct-light gain",
        "A normalized multi-layer stepped diffuse lighting response",
        "Uses an editable analytic Ramp until a Ramp asset is assigned",
        "Invalid connected masks or Ramp assets are diagnosed",
        MaterialNodeResourcePolicy::GeneratedDefault,
        "resource.diffuseRamp"};
    static constexpr MaterialNodeSemanticContract kSpecularBand{
        "PBR Surface / Specular Response",
        "Normal, view/light half-vector, width, softness, power, intensity and tint",
        "A quantized specular highlight",
        "Uses the displayed analytic band",
        "Unsupported dynamic inputs fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kFresnel{
        "PBR Surface / Additive, directly or through Add",
        "Normal, view direction, light alignment, color, width and power",
        "An additive Fresnel rim",
        "Uses the displayed analytic rim",
        "Non-additive destinations fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kMatCap{
        "PBR Surface / Additive, directly or through Add",
        "Camera-corrected view normal, optional eye-sphere normal, mask, image, tint and intensity",
        "An additive perspective-aware MatCap lobe",
        "No style image is invented",
        "A reachable MatCap without a valid MatCap texture fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired,
        "resource.matCap"};
    static constexpr MaterialNodeSemanticContract kHair{
        "PBR Surface / Specular Response",
        "Generated mesh tangent, light/view directions, shifts, width, tint and optional flow map",
        "A dual-lobe tangent-shifted Kajiya-Kay hair highlight",
        "Uses generated mesh tangents; RGBA hair control is an optional per-pixel override",
        "Degenerate UVs receive a stable normal-derived tangent fallback",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.hairControl"};
    static constexpr MaterialNodeSemanticContract kFace{
        "PBR Surface / Diffuse Response",
        "Face SDF, character right/forward axes, main light and Ramp controls",
        "A face-oriented toon diffuse response",
        "No face-specific field is generated",
        "A reachable Face SDF without a valid linear Face SDF texture fails compilation",
        MaterialNodeResourcePolicy::AuthoredRequired,
        "resource.faceSdf"};
    static constexpr MaterialNodeSemanticContract kEye{
        "PBR Surface / Eye",
        "View/light directions, iris parallax, HDR highlight, lift, caustic controls and optional RGBA Eye Control",
        "A bounded iris-parallax, spherical catchlight and caustic eye layer",
        "Uses procedural iris/catchlight/caustic defaults; RGBA control specializes iris, cornea, catchlight and parallax placement",
        "An invalid connected Eye Control sample fails compilation",
        MaterialNodeResourcePolicy::GeneratedDefault,
        "resource.eyeControl"};
    static constexpr MaterialNodeSemanticContract kCorneaHighlight{
        "PBR Surface / Eye",
        "The current material Base Color highlight layer, the eye tangent frame, view direction and scene key-light direction",
        "A transparent authored cornea highlight whose radiance and optional UV projection respond to the key light",
        "Uses the displayed fixed, light-intensity or light-projection mode without generating replacement artwork",
        "The node requires a Blend surface and remains inert when it is disconnected",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kSkin{
        "PBR Surface or Hybrid / Diffuse Response",
        "Upstream diffuse response, wrap, subsurface tint and optional thickness mask",
        "A wrapped skin diffuse response",
        "Uses a neutral thickness mask",
        "Unsupported response or mask routes fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.thicknessMask"};
    static constexpr MaterialNodeSemanticContract kFabric{
        "PBR Surface / Additive, directly or through Add",
        "View angle, sheen, fiber roughness, tint and optional fabric mask",
        "An additive fabric sheen",
        "Uses a neutral fabric mask",
        "Non-additive destinations fail compilation",
        MaterialNodeResourcePolicy::OptionalAuthoredOverride,
        "resource.fabricMask"};
    static constexpr MaterialNodeSemanticContract kHybrid{
        "PBR Surface / Diffuse Response",
        "Upstream NPR diffuse plus direct diffuse/specular and indirect PBR blend weights",
        "A controlled PBR + NPR lighting response",
        "Uses the displayed blend weights",
        "Missing or unsupported upstream response links fail compilation",
        MaterialNodeResourcePolicy::GeneratedDefault};
    static constexpr MaterialNodeSemanticContract kOutline{
        "PBR Surface / Outline",
        "Mode, color, thickness and the selected outline backend",
        "A material outline feature request",
        "Uses the displayed outline profile",
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
        "Requires exactly one Surface connection", "Missing or multiple output ownership fails compilation"};
    static constexpr MaterialNodeSemanticContract kParticleOutput{
        "Particle graph root", "Color, opacity, emissive and alpha-clip expressions",
        "The particle pixel material response", "Color is required; other inputs use deterministic defaults",
        "A ParticleOutput in a Surface graph or SurfaceOutput in a Particle graph fails compilation"};
    static constexpr MaterialNodeSemanticContract kFunction{
        "A compatible function destination", "The referenced Material Function", "The function result",
        "No fallback function is inserted",
        "Missing, cyclic, or signature-incompatible Functions fail compilation before Runtime publication"};
    static constexpr MaterialNodeSemanticContract kVariable{
        "Any bool or compatible numeric input", "One stable project runtime variable ID", "The latest published value",
        "Uses the authored fallback when the binding is missing or incompatible",
        "Unsupported Int bindings and type mismatches remain on fallback"};

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
    case MaterialNodeKind::Subtract:
    case MaterialNodeKind::Divide:
    case MaterialNodeKind::Minimum:
    case MaterialNodeKind::Maximum:
    case MaterialNodeKind::Absolute:
    case MaterialNodeKind::Negate:
    case MaterialNodeKind::Floor:
    case MaterialNodeKind::Ceil:
    case MaterialNodeKind::Fraction:
    case MaterialNodeKind::Modulo:
    case MaterialNodeKind::SquareRoot:
    case MaterialNodeKind::Sine:
    case MaterialNodeKind::Cosine:
    case MaterialNodeKind::Step:
    case MaterialNodeKind::SmoothStep:
    case MaterialNodeKind::Remap:
    case MaterialNodeKind::Cross:
    case MaterialNodeKind::Length:
    case MaterialNodeKind::Normalize:
    case MaterialNodeKind::Distance:
    case MaterialNodeKind::Reflect:
    case MaterialNodeKind::MakeFloat2:
    case MaterialNodeKind::MakeFloat3:
    case MaterialNodeKind::MakeFloat4:
    case MaterialNodeKind::Split:
    case MaterialNodeKind::Select:
    case MaterialNodeKind::DerivativeX:
    case MaterialNodeKind::DerivativeY:
    case MaterialNodeKind::FilterWidth:
    case MaterialNodeKind::Atan2:
    case MaterialNodeKind::Exp:
    case MaterialNodeKind::Log2:
    case MaterialNodeKind::Sign:
    case MaterialNodeKind::Round:
    case MaterialNodeKind::Trunc:
    case MaterialNodeKind::UvTransform:
    case MaterialNodeKind::ValueNoise:
    case MaterialNodeKind::Voronoi:
    case MaterialNodeKind::Fresnel: return kMath;
    case MaterialNodeKind::TextureCoordinate:
    case MaterialNodeKind::Time:
    case MaterialNodeKind::WorldPosition:
    case MaterialNodeKind::WorldNormal:
    case MaterialNodeKind::WorldTangent:
    case MaterialNodeKind::ViewDirection:
    case MaterialNodeKind::CameraPosition:
    case MaterialNodeKind::ScreenPosition:
    case MaterialNodeKind::ParticleState:
    case MaterialNodeKind::ParticleColor:
    case MaterialNodeKind::ParticleUv: return kConstant;
    case MaterialNodeKind::NormalUnpack: return kNormalUnpack;
    case MaterialNodeKind::PbrSurface: return kPbr;
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse: return kRamp;
    case MaterialNodeKind::SpecularBand: return kSpecularBand;
    case MaterialNodeKind::FresnelRim: return kFresnel;
    case MaterialNodeKind::MatCap: return kMatCap;
    case MaterialNodeKind::AnisotropicHair: return kHair;
    case MaterialNodeKind::FaceSdf: return kFace;
    case MaterialNodeKind::EyeLighting: return kEye;
    case MaterialNodeKind::CorneaHighlight: return kCorneaHighlight;
    case MaterialNodeKind::SkinLighting: return kSkin;
    case MaterialNodeKind::FabricSheen: return kFabric;
    case MaterialNodeKind::HybridLighting: return kHybrid;
    case MaterialNodeKind::Outline: return kOutline;
    case MaterialNodeKind::HairShadow: return kHairShadow;
    case MaterialNodeKind::SurfaceOutput: return kOutput;
    case MaterialNodeKind::ParticleOutput: return kParticleOutput;
    case MaterialNodeKind::FunctionCall: return kFunction;
    case MaterialNodeKind::Variable: return kVariable;
    }
    return kConstant;
}

const MaterialNodeSemanticContract& GetMaterialNodeSemanticContract(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) ? GetMaterialNodeSemanticContract(kind)
                                                       : GetMaterialNodeSemanticContract(MaterialNodeKind::Constant);
}

std::span<const std::string_view> GetMaterialNodeOwnedParameterIds(MaterialNodeKind kind) noexcept {
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
    case MaterialNodeKind::CorneaHighlight: return AsSpan(kCorneaHighlightParameters);
    case MaterialNodeKind::SkinLighting: return AsSpan(kSkinParameters);
    case MaterialNodeKind::FabricSheen: return AsSpan(kFabricParameters);
    case MaterialNodeKind::HybridLighting: return AsSpan(kHybridParameters);
    case MaterialNodeKind::Outline: return AsSpan(kOutlineParameters);
    case MaterialNodeKind::HairShadow: return AsSpan(kHairShadowParameters);
    default: return EmptyBindings();
    }
}

std::span<const std::string_view> GetMaterialNodeOwnedParameterIds(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) ? GetMaterialNodeOwnedParameterIds(kind)
                                                       : std::span<const std::string_view>{};
}

std::span<const std::string_view> GetMaterialNodeOwnedResourceIds(MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface: return EmptyBindings();
    case MaterialNodeKind::RampDiffuse: return AsSpan(kRampResources);
    case MaterialNodeKind::MultiRampDiffuse: return AsSpan(kMultiRampResources);
    case MaterialNodeKind::MatCap: return EmptyBindings();
    case MaterialNodeKind::AnisotropicHair: return EmptyBindings();
    case MaterialNodeKind::FaceSdf: return AsSpan(kFaceResources);
    case MaterialNodeKind::EyeLighting: return EmptyBindings();
    case MaterialNodeKind::CorneaHighlight: return EmptyBindings();
    case MaterialNodeKind::SkinLighting: return EmptyBindings();
    case MaterialNodeKind::FabricSheen: return EmptyBindings();
    case MaterialNodeKind::HairShadow: return EmptyBindings();
    case MaterialNodeKind::Shell: return AsSpan(kShellResources);
    default: return EmptyBindings();
    }
}

std::span<const std::string_view> GetMaterialNodeOwnedResourceIds(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) ? GetMaterialNodeOwnedResourceIds(kind)
                                                       : std::span<const std::string_view>{};
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
    case MaterialNodeKind::CorneaHighlight:
    case MaterialNodeKind::SkinLighting:
    case MaterialNodeKind::FabricSheen:
    case MaterialNodeKind::HybridLighting:
    case MaterialNodeKind::Outline:
    case MaterialNodeKind::HairShadow: return true;
    default: return false;
    }
}

bool IsReadyMadeMaterialNode(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) && IsReadyMadeMaterialNode(kind);
}

bool IsSingletonReadyMadeMaterialNode(MaterialNodeKind kind) noexcept {
    return IsReadyMadeMaterialNode(kind) || kind == MaterialNodeKind::SurfaceOutput ||
           kind == MaterialNodeKind::ParticleOutput;
}

bool IsSingletonReadyMadeMaterialNode(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) && IsSingletonReadyMadeMaterialNode(kind);
}

std::string_view ReadyMadeMaterialNodeDisplayName(MaterialNodeKind kind) noexcept {
    switch (kind) {
    case MaterialNodeKind::PbrSurface: return "PBR Surface";
    case MaterialNodeKind::RampDiffuse: return "Ramp Diffuse";
    case MaterialNodeKind::MultiRampDiffuse: return "Multi-Ramp Diffuse";
    case MaterialNodeKind::SpecularBand: return "Specular Band";
    case MaterialNodeKind::FresnelRim: return "Fresnel Rim";
    case MaterialNodeKind::MatCap: return "MatCap";
    case MaterialNodeKind::AnisotropicHair: return "Anime Hair Highlight";
    case MaterialNodeKind::FaceSdf: return "Anime Face SDF";
    case MaterialNodeKind::EyeLighting: return "Eye Layer";
    case MaterialNodeKind::CorneaHighlight: return "Cornea Highlight";
    case MaterialNodeKind::SkinLighting: return "Skin Lighting";
    case MaterialNodeKind::FabricSheen: return "Fabric Sheen";
    case MaterialNodeKind::HybridLighting: return "PBR + NPR Hybrid";
    case MaterialNodeKind::Outline: return "Outline";
    case MaterialNodeKind::HairShadow: return "Hair Shadow Mask";
    default: return ToString(kind);
    }
}

std::string_view ReadyMadeMaterialNodeDisplayName(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    return TryGetBuiltinMaterialNodeKind(typeId, kind) ? ReadyMadeMaterialNodeDisplayName(kind) : ToString(typeId);
}

void InitializeReadyMadeMaterialNode(MaterialAssetData& material, MaterialGraphNodeData& node) {
    if (!IsReadyMadeMaterialNode(node.kind)) {
        return;
    }
    MaterialNodeKind builtinKind{};
    if (!TryGetBuiltinMaterialNodeKind(node.kind, builtinKind)) {
        return;
    }
    MaterialGraphData& graph = material.graph;
    switch (builtinKind) {
    case MaterialNodeKind::PbrSurface:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Surface;
        EnsureParameter(graph, "parameter.baseColor", "Base Color",
                        MakeMaterialValue(MaterialValueType::Float4, material.pbr.baseColorFactor.x,
                                          material.pbr.baseColorFactor.y, material.pbr.baseColorFactor.z,
                                          material.pbr.baseColorFactor.w));
        EnsureParameter(graph, "parameter.metallic", "Metallic",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.metallicFactor));
        EnsureParameter(graph, "parameter.roughness", "Roughness",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.roughnessFactor));
        EnsureParameter(graph, "parameter.specular", "Specular",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.specularFactor));
        EnsureParameter(graph, "parameter.specularColor", "Specular Color",
                        MakeMaterialValue(MaterialValueType::Float3, material.pbr.specularColorFactor.x,
                                          material.pbr.specularColorFactor.y, material.pbr.specularColorFactor.z));
        EnsureParameter(graph, "parameter.normalScale", "Normal Scale",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.normalScale));
        EnsureParameter(graph, "parameter.occlusionStrength", "Occlusion Strength",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.occlusionStrength));
        EnsureParameter(graph, "parameter.emissive", "Emissive",
                        MakeMaterialValue(MaterialValueType::Float3, material.pbr.emissiveFactor.x,
                                          material.pbr.emissiveFactor.y, material.pbr.emissiveFactor.z));
        EnsureParameter(graph, "parameter.emissiveStrength", "Emissive Strength",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.emissiveStrength));
        EnsureParameter(graph, "parameter.clearCoat", "Clear Coat",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.clearCoatFactor));
        EnsureParameter(graph, "parameter.clearCoatRoughness", "Clear Coat Roughness",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.clearCoatRoughnessFactor));
        EnsureParameter(graph, "parameter.transmission", "Transmission",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.transmissionFactor));
        EnsureParameter(graph, "parameter.ior", "Index of Refraction",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.ior));
        EnsureParameter(graph, "parameter.subsurface", "Subsurface",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.subsurfaceFactor));
        EnsureParameter(graph, "parameter.subsurfaceRadius", "Subsurface Radius",
                        MakeMaterialValue(MaterialValueType::Float1, material.pbr.subsurfaceRadius));
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
                        MakeMaterialValue(MaterialValueType::Float1, 0.075f));
        EnsureParameter(graph, "parameter.specularBandSoftness", "Band Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.012f));
        EnsureParameter(graph, "parameter.specularIntensity", "Band Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.specularPower", "Band Power",
                        MakeMaterialValue(MaterialValueType::Float1, 96.0f));
        EnsureParameter(graph, "parameter.specularTint", "Band Tint",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
        break;
    case MaterialNodeKind::FresnelRim:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.rimColor", "Rim Color",
                        MakeMaterialValue(MaterialValueType::Float3, 0.68f, 0.78f, 1.0f));
        EnsureParameter(graph, "parameter.rimPower", "Rim Power", MakeMaterialValue(MaterialValueType::Float1, 4.0f));
        EnsureParameter(graph, "parameter.rimWidth", "Rim Width", MakeMaterialValue(MaterialValueType::Float1, 0.22f));
        EnsureParameter(graph, "parameter.rimIntensity", "Rim Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.28f));
        EnsureParameter(graph, "parameter.rimLightAlignment", "Light Alignment",
                        MakeMaterialValue(MaterialValueType::Float1, 0.35f));
        break;
    case MaterialNodeKind::MatCap:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.matCapTint", "MatCap Tint",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
        EnsureParameter(graph, "parameter.matCapIntensity", "MatCap Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.25f));
        EnsureParameter(graph, "parameter.matCapViewCorrection", "View Correction",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.matCapSphericalNormalBlend", "Spherical Normal Blend",
                        MakeMaterialValue(MaterialValueType::Float1, 0.0f));
        break;
    case MaterialNodeKind::AnisotropicHair:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::SpecularResponse;
        EnsureParameter(graph, "parameter.hairShift", "Primary Shift",
                        MakeMaterialValue(MaterialValueType::Float1, 0.08f));
        EnsureParameter(graph, "parameter.hairWidth", "Highlight Width",
                        MakeMaterialValue(MaterialValueType::Float1, 0.18f));
        EnsureParameter(graph, "parameter.hairSecondaryShift", "Secondary Shift",
                        MakeMaterialValue(MaterialValueType::Float1, -0.10f));
        EnsureParameter(graph, "parameter.hairIntensity", "Highlight Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.hairPrimaryPower", "Primary Sharpness",
                        MakeMaterialValue(MaterialValueType::Float1, 96.0f));
        EnsureParameter(graph, "parameter.hairSecondaryPower", "Secondary Sharpness",
                        MakeMaterialValue(MaterialValueType::Float1, 48.0f));
        EnsureParameter(graph, "parameter.hairSecondaryIntensity", "Secondary Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.45f));
        EnsureParameter(graph, "parameter.hairFlowStrength", "Flow Map Strength",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.hairHighlightTint", "Hair Highlight Tint",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 0.92f, 0.84f));
        break;
    case MaterialNodeKind::FaceSdf:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureRamp(graph, false);
        EnsureParameter(graph, "parameter.faceShadowSoftness", "Shadow Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.04f));
        EnsureParameter(graph, "parameter.faceShadowOffset", "Shadow Offset",
                        MakeMaterialValue(MaterialValueType::Float1, 0.0f));
        EnsureParameter(graph, "parameter.faceRight", "Face Local Right",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 0.0f, 0.0f));
        EnsureParameter(graph, "parameter.faceForward", "Face Local Forward",
                        MakeMaterialValue(MaterialValueType::Float3, 0.0f, 0.0f, 1.0f));
        break;
    case MaterialNodeKind::EyeLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.irisDepth", "Iris Depth",
                        MakeMaterialValue(MaterialValueType::Float1, 0.12f));
        EnsureParameter(graph, "parameter.eyeSparkle", "Catchlight Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.eyeSparklePower", "Catchlight Sharpness",
                        MakeMaterialValue(MaterialValueType::Float1, 64.0f));
        EnsureParameter(graph, "parameter.irisCenter", "Iris UV Center",
                        MakeMaterialValue(MaterialValueType::Float2, 0.50f, 0.50f));
        EnsureParameter(graph, "parameter.irisRadius", "Iris UV Radius",
                        MakeMaterialValue(MaterialValueType::Float1, 0.48f));
        EnsureParameter(graph, "parameter.irisMaskSoftness", "Iris Mask Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.08f));
        EnsureParameter(graph, "parameter.eyeLayerMode", "Eye Layer Mode",
                        MakeMaterialValue(MaterialValueType::Float1, 0.0f));
        EnsureParameter(graph, "parameter.eyeHighlightOpacity", "Highlight Opacity",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.eyeHighlightThreshold", "Highlight Threshold",
                        MakeMaterialValue(MaterialValueType::Float1, 0.02f));
        EnsureParameter(graph, "parameter.eyeHighlightSoftness", "Highlight Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.04f));
        EnsureParameter(graph, "parameter.eyeParallaxScale", "Parallax Scale",
                        MakeMaterialValue(MaterialValueType::Float1, 0.10f));
        EnsureParameter(graph, "parameter.eyeHighlightIntensity", "Highlight Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 2.0f));
        EnsureParameter(graph, "parameter.eyeBaseLift", "Iris Light Lift",
                        MakeMaterialValue(MaterialValueType::Float1, 0.18f));
        EnsureParameter(graph, "parameter.eyeCausticIntensity", "Caustic Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 0.25f));
        EnsureParameter(graph, "parameter.eyeCausticTint", "Caustic Tint",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 0.72f, 0.38f));
        EnsureParameter(graph, "parameter.eyeCausticWidth", "Caustic Width",
                        MakeMaterialValue(MaterialValueType::Float1, 0.28f));
        break;
    case MaterialNodeKind::CorneaHighlight:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        material.surface.alphaMode = MaterialAlphaMode::Blend;
        EnsureParameter(graph, "parameter.irisDepth", "Iris Depth",
                        MakeMaterialValue(MaterialValueType::Float1, 0.12f));
        EnsureParameter(graph, "parameter.irisCenter", "Iris UV Center",
                        MakeMaterialValue(MaterialValueType::Float2, 0.50f, 0.50f));
        EnsureParameter(graph, "parameter.irisRadius", "Iris UV Radius",
                        MakeMaterialValue(MaterialValueType::Float1, 0.48f));
        EnsureParameter(graph, "parameter.irisMaskSoftness", "Iris Mask Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.08f));
        EnsureParameter(graph, "parameter.eyeLayerMode", "Eye Layer Mode",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.eyeHighlightOpacity", "Highlight Opacity",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        EnsureParameter(graph, "parameter.eyeHighlightThreshold", "Highlight Threshold",
                        MakeMaterialValue(MaterialValueType::Float1, 0.02f));
        EnsureParameter(graph, "parameter.eyeHighlightSoftness", "Highlight Softness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.04f));
        EnsureParameter(graph, "parameter.eyeParallaxScale", "Parallax Scale",
                        MakeMaterialValue(MaterialValueType::Float1, 0.10f));
        EnsureParameter(graph, "parameter.eyeHighlightIntensity", "Highlight Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 2.0f));
        EnsureParameter(graph, "parameter.corneaHighlightMode", "Lighting Mode",
                        MakeMaterialValue(MaterialValueType::Float1, 2.0f));
        EnsureParameter(graph, "parameter.corneaHighlightTravel", "Highlight Travel",
                        MakeMaterialValue(MaterialValueType::Float1, 0.10f));
        EnsureParameter(graph, "parameter.corneaHighlightViewInfluence", "View Influence",
                        MakeMaterialValue(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.corneaHighlightLightInfluence", "Light Influence",
                        MakeMaterialValue(MaterialValueType::Float1, 0.65f));
        EnsureParameter(graph, "parameter.corneaHighlightCurvature", "Cornea Curvature",
                        MakeMaterialValue(MaterialValueType::Float1, 0.75f));
        EnsureParameter(graph, "parameter.corneaHighlightMinBrightness", "Minimum Brightness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.45f));
        EnsureParameter(graph, "parameter.corneaHighlightLightResponse", "Light Response",
                        MakeMaterialValue(MaterialValueType::Float1, 0.65f));
        EnsureParameter(graph, "parameter.corneaHighlightEdgeFade", "Edge Fade",
                        MakeMaterialValue(MaterialValueType::Float1, 0.0f));
        EnsureParameter(graph, "parameter.corneaHighlightSharpness", "Highlight Sharpness",
                        MakeMaterialValue(MaterialValueType::Float1, 12.0f));
        EnsureParameter(graph, "parameter.corneaHighlightLightColor", "Follow Light Color",
                        MakeMaterialValue(MaterialValueType::Float1, 0.25f));
        break;
    case MaterialNodeKind::SkinLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.skinWrap", "Light Wrap", MakeMaterialValue(MaterialValueType::Float1, 0.18f));
        EnsureParameter(graph, "parameter.skinSubsurface", "Subsurface",
                        MakeMaterialValue(MaterialValueType::Float1, 0.12f));
        EnsureParameter(graph, "parameter.skinTint", "Scatter Tint",
                        MakeMaterialValue(MaterialValueType::Float3, 1.0f, 0.42f, 0.32f));
        break;
    case MaterialNodeKind::FabricSheen:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::Float3;
        EnsureParameter(graph, "parameter.fabricSheen", "Sheen", MakeMaterialValue(MaterialValueType::Float1, 0.28f));
        EnsureParameter(graph, "parameter.fiberRoughness", "Fiber Roughness",
                        MakeMaterialValue(MaterialValueType::Float1, 0.72f));
        EnsureParameter(graph, "parameter.fabricSheenPower", "Sheen Power",
                        MakeMaterialValue(MaterialValueType::Float1, 4.0f));
        EnsureParameter(graph, "parameter.fabricSheenIntensity", "Sheen Intensity",
                        MakeMaterialValue(MaterialValueType::Float1, 1.0f));
        break;
    case MaterialNodeKind::HybridLighting:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.directDiffusePbrBlend", "Diffuse PBR Blend",
                        MakeMaterialValue(MaterialValueType::Float1, 0.25f));
        EnsureParameter(graph, "parameter.directSpecularPbrBlend", "Specular PBR Blend",
                        MakeMaterialValue(MaterialValueType::Float1, 0.35f));
        EnsureParameter(graph, "parameter.indirectPbrBlend", "Indirect PBR Blend",
                        MakeMaterialValue(MaterialValueType::Float1, 0.65f));
        break;
    case MaterialNodeKind::Outline:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::OutlineResponse;
        EnsureParameter(graph, "parameter.outlineMode", "Outline Mode",
                        MakeMaterialValue(MaterialValueType::Float1, 2.0f));
        EnsureParameter(graph, "parameter.outlineColor", "Outline Color",
                        MakeMaterialValue(MaterialValueType::Float3, 0.025f, 0.030f, 0.045f));
        EnsureParameter(graph, "parameter.outlineThickness", "Outline Thickness",
                        MakeMaterialValue(MaterialValueType::Float1, 2.0f));
        break;
    case MaterialNodeKind::HairShadow:
        node.displayName = ReadyMadeMaterialNodeDisplayName(node.kind);
        node.valueType = MaterialValueType::DiffuseResponse;
        EnsureParameter(graph, "parameter.hairShadowStrength", "Hair Shadow Strength",
                        MakeMaterialValue(MaterialValueType::Float1, 0.65f));
        break;
    default: break;
    }
    node.value.type = node.valueType;
}

} // namespace HIKARI::ASSETS::MATERIAL
