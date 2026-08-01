#include "HIKARI_MaterialAssetTypes.h"

#include <array>
#include <utility>

namespace HIKARI {

    namespace {

        template<class TEnum, size_t TSize>
        std::string_view EnumToString(
            TEnum value,
            const std::array<std::pair<TEnum, std::string_view>, TSize>& names) noexcept {

            for (const auto& [candidate, name] : names) {
                if (candidate == value) {
                    return name;
                }
            }
            return "Unknown";
        }

        template<class TEnum, size_t TSize>
        bool ParseEnum(
            std::string_view text,
            const std::array<std::pair<TEnum, std::string_view>, TSize>& names,
            TEnum& outValue) noexcept {

            for (const auto& [candidate, name] : names) {
                if (name == text) {
                    outValue = candidate;
                    return true;
                }
            }
            return false;
        }

        constexpr std::array kAssetKinds{
            std::pair{ MaterialAssetKind::Material, std::string_view{ "Material" } },
            std::pair{ MaterialAssetKind::Function, std::string_view{ "Function" } },
            std::pair{ MaterialAssetKind::Instance, std::string_view{ "Instance" } },
            std::pair{ MaterialAssetKind::Profile, std::string_view{ "Profile" } },
            std::pair{ MaterialAssetKind::Ramp, std::string_view{ "Ramp" } },
        };
        constexpr std::array kDomains{
            std::pair{ MaterialDomain::Surface, std::string_view{ "Surface" } },
        };
        constexpr std::array kTemplates{
            std::pair{ MaterialTemplateId::StandardPbr, std::string_view{ "StandardPBR" } },
            std::pair{ MaterialTemplateId::GenericToon, std::string_view{ "GenericToon" } },
            std::pair{ MaterialTemplateId::Skin, std::string_view{ "Skin" } },
            std::pair{ MaterialTemplateId::Face, std::string_view{ "Face" } },
            std::pair{ MaterialTemplateId::Hair, std::string_view{ "Hair" } },
            std::pair{ MaterialTemplateId::Eye, std::string_view{ "Eye" } },
            std::pair{ MaterialTemplateId::Cloth, std::string_view{ "Cloth" } },
            std::pair{ MaterialTemplateId::HybridMetal, std::string_view{ "HybridMetal" } },
            std::pair{ MaterialTemplateId::Custom, std::string_view{ "Custom" } },
        };
        constexpr std::array kAlphaModes{
            std::pair{ MaterialAlphaMode::Opaque, std::string_view{ "Opaque" } },
            std::pair{ MaterialAlphaMode::Mask, std::string_view{ "Mask" } },
            std::pair{ MaterialAlphaMode::Blend, std::string_view{ "Blend" } },
        };
        constexpr std::array kValueTypes{
            std::pair{ MaterialValueType::Bool, std::string_view{ "Bool" } },
            std::pair{ MaterialValueType::Float1, std::string_view{ "Float1" } },
            std::pair{ MaterialValueType::Float2, std::string_view{ "Float2" } },
            std::pair{ MaterialValueType::Float3, std::string_view{ "Float3" } },
            std::pair{ MaterialValueType::Float4, std::string_view{ "Float4" } },
            std::pair{ MaterialValueType::Surface, std::string_view{ "Surface" } },
            std::pair{ MaterialValueType::DiffuseResponse, std::string_view{ "DiffuseResponse" } },
            std::pair{ MaterialValueType::SpecularResponse, std::string_view{ "SpecularResponse" } },
        };
        constexpr std::array kResourceTypes{
            std::pair{ MaterialResourceType::Texture2D, std::string_view{ "Texture2D" } },
            std::pair{ MaterialResourceType::TextureCube, std::string_view{ "TextureCube" } },
            std::pair{ MaterialResourceType::Ramp1D, std::string_view{ "Ramp1D" } },
            std::pair{ MaterialResourceType::Ramp2D, std::string_view{ "Ramp2D" } },
        };
        constexpr std::array kResourceSemantics{
            std::pair{ MaterialResourceSemantic::Custom, std::string_view{ "Custom" } },
            std::pair{ MaterialResourceSemantic::BaseColor, std::string_view{ "BaseColor" } },
            std::pair{ MaterialResourceSemantic::Normal, std::string_view{ "Normal" } },
            std::pair{ MaterialResourceSemantic::MetallicRoughness, std::string_view{ "MetallicRoughness" } },
            std::pair{ MaterialResourceSemantic::Occlusion, std::string_view{ "Occlusion" } },
            std::pair{ MaterialResourceSemantic::Emissive, std::string_view{ "Emissive" } },
            std::pair{ MaterialResourceSemantic::Specular, std::string_view{ "Specular" } },
            std::pair{ MaterialResourceSemantic::SpecularColor, std::string_view{ "SpecularColor" } },
            std::pair{ MaterialResourceSemantic::Mask, std::string_view{ "Mask" } },
            std::pair{ MaterialResourceSemantic::Ramp, std::string_view{ "Ramp" } },
            std::pair{ MaterialResourceSemantic::MatCap, std::string_view{ "MatCap" } },
            std::pair{ MaterialResourceSemantic::FaceSdf, std::string_view{ "FaceSDF" } },
        };
        constexpr std::array kNodeKinds{
            std::pair{ MaterialNodeKind::Constant, std::string_view{ "Constant" } },
            std::pair{ MaterialNodeKind::Parameter, std::string_view{ "Parameter" } },
            std::pair{ MaterialNodeKind::TextureSample, std::string_view{ "TextureSample" } },
            std::pair{ MaterialNodeKind::Add, std::string_view{ "Add" } },
            std::pair{ MaterialNodeKind::Multiply, std::string_view{ "Multiply" } },
            std::pair{ MaterialNodeKind::Lerp, std::string_view{ "Lerp" } },
            std::pair{ MaterialNodeKind::Clamp, std::string_view{ "Clamp" } },
            std::pair{ MaterialNodeKind::Saturate, std::string_view{ "Saturate" } },
            std::pair{ MaterialNodeKind::OneMinus, std::string_view{ "OneMinus" } },
            std::pair{ MaterialNodeKind::Power, std::string_view{ "Power" } },
            std::pair{ MaterialNodeKind::Dot, std::string_view{ "Dot" } },
            std::pair{ MaterialNodeKind::NormalUnpack, std::string_view{ "NormalUnpack" } },
            std::pair{ MaterialNodeKind::PbrSurface, std::string_view{ "PBRSurface" } },
            std::pair{ MaterialNodeKind::RampDiffuse, std::string_view{ "RampDiffuse" } },
            std::pair{ MaterialNodeKind::MultiRampDiffuse, std::string_view{ "MultiRampDiffuse" } },
            std::pair{ MaterialNodeKind::SpecularBand, std::string_view{ "SpecularBand" } },
            std::pair{ MaterialNodeKind::FresnelRim, std::string_view{ "FresnelRim" } },
            std::pair{ MaterialNodeKind::MatCap, std::string_view{ "MatCap" } },
            std::pair{ MaterialNodeKind::AnisotropicHair, std::string_view{ "AnisotropicHair" } },
            std::pair{ MaterialNodeKind::FaceSdf, std::string_view{ "FaceSDF" } },
            std::pair{ MaterialNodeKind::EyeLighting, std::string_view{ "EyeLighting" } },
            std::pair{ MaterialNodeKind::SurfaceOutput, std::string_view{ "SurfaceOutput" } },
            std::pair{ MaterialNodeKind::FunctionCall, std::string_view{ "FunctionCall" } },
        };
        constexpr std::array kFeatureRequests{
            std::pair{ MaterialFeatureRequest::GeometryOutline, std::string_view{ "GeometryOutline" } },
            std::pair{ MaterialFeatureRequest::ScreenOutline, std::string_view{ "ScreenOutline" } },
            std::pair{ MaterialFeatureRequest::VisibilityOverlay, std::string_view{ "VisibilityOverlay" } },
            std::pair{ MaterialFeatureRequest::FaceHairShadow, std::string_view{ "FaceHairShadow" } },
        };
        constexpr std::array kRampDimensions{
            std::pair{ MaterialRampDimension::OneD, std::string_view{ "OneD" } },
            std::pair{ MaterialRampDimension::TwoD, std::string_view{ "TwoD" } },
        };
        constexpr std::array kRampInterpolations{
            std::pair{ MaterialRampInterpolation::Constant, std::string_view{ "Constant" } },
            std::pair{ MaterialRampInterpolation::Linear, std::string_view{ "Linear" } },
            std::pair{ MaterialRampInterpolation::Smooth, std::string_view{ "Smooth" } },
        };

    } // namespace

#define HIKARI_DEFINE_MATERIAL_ENUM_IO(Type, Names, ParseName) \
    std::string_view ToString(Type value) noexcept { return EnumToString(value, Names); } \
    bool ParseName(std::string_view text, Type& outValue) noexcept { return ParseEnum(text, Names, outValue); }

    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialAssetKind, kAssetKinds, TryParseMaterialAssetKind)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialDomain, kDomains, TryParseMaterialDomain)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialTemplateId, kTemplates, TryParseMaterialTemplateId)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialAlphaMode, kAlphaModes, TryParseMaterialAlphaMode)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialValueType, kValueTypes, TryParseMaterialValueType)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialResourceType, kResourceTypes, TryParseMaterialResourceType)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialResourceSemantic, kResourceSemantics, TryParseMaterialResourceSemantic)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialNodeKind, kNodeKinds, TryParseMaterialNodeKind)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialFeatureRequest, kFeatureRequests, TryParseMaterialFeatureRequest)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialRampDimension, kRampDimensions, TryParseMaterialRampDimension)
    HIKARI_DEFINE_MATERIAL_ENUM_IO(MaterialRampInterpolation, kRampInterpolations, TryParseMaterialRampInterpolation)

#undef HIKARI_DEFINE_MATERIAL_ENUM_IO

    bool IsMaterialNumericType(MaterialValueType type) noexcept {
        return type == MaterialValueType::Float1 ||
            type == MaterialValueType::Float2 ||
            type == MaterialValueType::Float3 ||
            type == MaterialValueType::Float4;
    }

    uint32_t GetMaterialValueComponentCount(MaterialValueType type) noexcept {
        switch (type) {
        case MaterialValueType::Bool:
        case MaterialValueType::Float1:
            return 1u;
        case MaterialValueType::Float2:
            return 2u;
        case MaterialValueType::Float3:
            return 3u;
        case MaterialValueType::Float4:
            return 4u;
        default:
            return 0u;
        }
    }

} // namespace HIKARI
