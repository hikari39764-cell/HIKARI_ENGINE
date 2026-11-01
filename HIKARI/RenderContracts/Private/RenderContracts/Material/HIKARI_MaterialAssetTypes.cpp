#include "RenderContracts/Material/HIKARI_MaterialAssetTypes.h"

#include <algorithm>
#include <utility>

#include "RenderContracts/Material/HIKARI_MaterialNodeDefinition.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI {

namespace {

constexpr TEXT::EnumName<MaterialAssetKind> kAssetKinds[]{
    {MaterialAssetKind::Material, "Material"}, {MaterialAssetKind::Function, "Function"},
    {MaterialAssetKind::Instance, "Instance"}, {MaterialAssetKind::Profile, "Profile"},
    {MaterialAssetKind::Ramp, "Ramp"},
};
constexpr TEXT::EnumName<MaterialDomain> kDomains[]{
    {MaterialDomain::Surface, "Surface"},
    {MaterialDomain::Particle, "Particle"},
};
constexpr TEXT::EnumName<MaterialTemplateId> kTemplates[]{
    {MaterialTemplateId::StandardPbr, "StandardPBR"},
    {MaterialTemplateId::GenericToon, "GenericToon"},
    {MaterialTemplateId::Skin, "Skin"},
    {MaterialTemplateId::Face, "Face"},
    {MaterialTemplateId::Hair, "Hair"},
    {MaterialTemplateId::Eye, "Eye"},
    {MaterialTemplateId::Cloth, "Cloth"},
    {MaterialTemplateId::HybridMetal, "HybridMetal"},
    {MaterialTemplateId::Custom, "Custom"},
    {MaterialTemplateId::ParticleUnlit, "ParticleUnlit"},
    {MaterialTemplateId::ParticleUnlitTextured, "ParticleUnlitTextured"},
    {MaterialTemplateId::PainterlyShell, "PainterlyShell"},
};
constexpr TEXT::EnumName<MaterialAlphaMode> kAlphaModes[]{
    {MaterialAlphaMode::Opaque, "Opaque"},
    {MaterialAlphaMode::Mask, "Mask"},
    {MaterialAlphaMode::Blend, "Blend"},
};
constexpr TEXT::EnumName<MaterialAlphaRepresentation> kAlphaRepresentations[]{
    {MaterialAlphaRepresentation::Straight, "Straight"},
    {MaterialAlphaRepresentation::Premultiplied, "Premultiplied"},
};
constexpr TEXT::EnumName<MaterialValueType> kValueTypes[]{
    {MaterialValueType::Bool, "Bool"},
    {MaterialValueType::Float1, "Float1"},
    {MaterialValueType::Float2, "Float2"},
    {MaterialValueType::Float3, "Float3"},
    {MaterialValueType::Float4, "Float4"},
    {MaterialValueType::Surface, "Surface"},
    {MaterialValueType::DiffuseResponse, "DiffuseResponse"},
    {MaterialValueType::SpecularResponse, "SpecularResponse"},
    {MaterialValueType::OutlineResponse, "OutlineResponse"},
    {MaterialValueType::ShellSettings, "ShellSettings"},
};
constexpr TEXT::EnumName<MaterialResourceType> kResourceTypes[]{
    {MaterialResourceType::Texture2D, "Texture2D"},
    {MaterialResourceType::TextureCube, "TextureCube"},
    {MaterialResourceType::Ramp1D, "Ramp1D"},
    {MaterialResourceType::Ramp2D, "Ramp2D"},
};
constexpr TEXT::EnumName<MaterialResourceSemantic> kResourceSemantics[]{
    {MaterialResourceSemantic::Custom, "Custom"},
    {MaterialResourceSemantic::BaseColor, "BaseColor"},
    {MaterialResourceSemantic::Normal, "Normal"},
    {MaterialResourceSemantic::MetallicRoughness, "MetallicRoughness"},
    {MaterialResourceSemantic::Occlusion, "Occlusion"},
    {MaterialResourceSemantic::Emissive, "Emissive"},
    {MaterialResourceSemantic::Specular, "Specular"},
    {MaterialResourceSemantic::SpecularColor, "SpecularColor"},
    {MaterialResourceSemantic::Mask, "Mask"},
    {MaterialResourceSemantic::Ramp, "Ramp"},
    {MaterialResourceSemantic::MatCap, "MatCap"},
    {MaterialResourceSemantic::FaceSdf, "FaceSDF"},
    {MaterialResourceSemantic::HairShadow, "HairShadow"},
    {MaterialResourceSemantic::HairControl, "HairControl"},
    {MaterialResourceSemantic::EyeControl, "EyeControl"},
    {MaterialResourceSemantic::Thickness, "Thickness"},
    {MaterialResourceSemantic::Fabric, "Fabric"},
    {MaterialResourceSemantic::Noise, "Noise"},
    {MaterialResourceSemantic::DetailNoise, "DetailNoise"},
    {MaterialResourceSemantic::FlowMap, "FlowMap"},
    {MaterialResourceSemantic::StrokeMask, "StrokeMask"},
    {MaterialResourceSemantic::StrokeNormal, "StrokeNormal"},
};
constexpr TEXT::EnumName<MaterialFeatureRequest> kFeatureRequests[]{
    {MaterialFeatureRequest::GeometryOutline, "GeometryOutline"},
    {MaterialFeatureRequest::ScreenOutline, "ScreenOutline"},
    {MaterialFeatureRequest::VisibilityOverlay, "VisibilityOverlay"},
    {MaterialFeatureRequest::FaceHairShadow, "FaceHairShadow"},
};
constexpr TEXT::EnumName<MaterialRampDimension> kRampDimensions[]{
    {MaterialRampDimension::OneD, "OneD"},
    {MaterialRampDimension::TwoD, "TwoD"},
};
constexpr TEXT::EnumName<MaterialRampInterpolation> kRampInterpolations[]{
    {MaterialRampInterpolation::Constant, "Constant"},
    {MaterialRampInterpolation::Linear, "Linear"},
    {MaterialRampInterpolation::Smooth, "Smooth"},
};

} // namespace

#define HIKARI_DEFINE_MATERIAL_ENUM_IO(Type, Names, ParseName)                                                         \
    std::string_view ToString(Type value) noexcept { return TEXT::EnumToName(Names, value, "Unknown"); }               \
    bool ParseName(std::string_view text, Type& outValue) noexcept { return TEXT::TryParseEnum(Names, text, outValue); }

HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialAssetKind, kAssetKinds, TryParseMaterialAssetKind)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialDomain, kDomains, TryParseMaterialDomain)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialTemplateId, kTemplates, TryParseMaterialTemplateId)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialAlphaMode, kAlphaModes, TryParseMaterialAlphaMode)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialAlphaRepresentation, kAlphaRepresentations, TryParseMaterialAlphaRepresentation)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialValueType, kValueTypes, TryParseMaterialValueType)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialResourceType, kResourceTypes, TryParseMaterialResourceType)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialResourceSemantic, kResourceSemantics, TryParseMaterialResourceSemantic)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialFeatureRequest, kFeatureRequests, TryParseMaterialFeatureRequest)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialRampDimension, kRampDimensions, TryParseMaterialRampDimension)
HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialRampInterpolation, kRampInterpolations, TryParseMaterialRampInterpolation)

#undef HIKARI_DEFINE_MATERIAL_ENUM_IO

std::string_view ToString(MaterialNodeKind value) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(value);
    return definition == nullptr ? std::string_view{"Unknown"} : definition->stableName;
}

MaterialNodeTypeId::MaterialNodeTypeId(MaterialNodeKind builtinKind) : value(ToString(builtinKind)) {}

bool MaterialNodeTypeId::operator==(MaterialNodeKind builtinKind) const noexcept {
    return value == ToString(builtinKind);
}

bool MaterialNodeTypeId::IsValid() const noexcept {
    if (value.empty() || value.size() > 128u ||
        !((value.front() >= 'A' && value.front() <= 'Z') || (value.front() >= 'a' && value.front() <= 'z'))) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-' ||
               character == ':';
    });
}

bool operator==(MaterialNodeKind builtinKind, const MaterialNodeTypeId& typeId) noexcept {
    return typeId == builtinKind;
}

std::string_view ToString(const MaterialNodeTypeId& value) noexcept { return value.value; }

bool TryParseMaterialNodeKind(std::string_view text, MaterialNodeKind& outValue) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(text);
    if (definition == nullptr) {
        return false;
    }
    outValue = definition->kind;
    return true;
}

bool TryParseMaterialNodeTypeId(std::string_view text, MaterialNodeTypeId& outValue) noexcept {
    MaterialNodeTypeId parsed{text};
    if (!parsed.IsValid()) {
        return false;
    }
    if (const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(text)) {
        parsed.value = std::string(definition->stableName);
    }
    outValue = std::move(parsed);
    return true;
}

bool TryGetBuiltinMaterialNodeKind(const MaterialNodeTypeId& typeId, MaterialNodeKind& outValue) noexcept {
    return TryParseMaterialNodeKind(typeId.value, outValue);
}

} // namespace HIKARI
