#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {
namespace {

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
        EnsureParameter(graph, "parameter.faceHairShadowStrength", "Hair Shadow Strength",
                        Value(MaterialValueType::Float1, 0.42f));
        EnsureParameter(graph, "parameter.faceHairShadowOffset", "Hair Shadow Offset",
                        Value(MaterialValueType::Float1, 3.0f));
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
    default:
        break;
    }
    node.value.type = node.valueType;
}

} // namespace HIKARI::ASSETS::MATERIAL
