#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"

#include <algorithm>
#include <string>
#include <utility>

namespace HIKARI::ASSETS::MATERIAL {

    namespace {

        constexpr std::array<MaterialTemplateInfo, 8u> kTemplates{ {
            { MaterialTemplateId::StandardPbr, "Standard PBR",
                "Physically based baseline with imported texture compatibility." },
            { MaterialTemplateId::GenericToon, "Generic Toon",
                "Ramp diffuse, banded specular and Fresnel rim defaults." },
            { MaterialTemplateId::Skin, "Skin",
                "Soft ramp response, skin masks and restrained rim lighting." },
            { MaterialTemplateId::Face, "Face",
                "Face SDF lighting with facial shadow feature requests." },
            { MaterialTemplateId::Hair, "Hair",
                "Ramp diffuse and anisotropic strand highlights." },
            { MaterialTemplateId::Eye, "Eye",
                "Iris masks, eye lighting and controlled emissive sparkle." },
            { MaterialTemplateId::Cloth, "Cloth",
                "Broad diffuse ramp with fabric sheen and fiber masking." },
            { MaterialTemplateId::HybridMetal, "Hybrid Metal",
                "PBR metal response mixed with multi-ramp stylization." },
        } };

        constexpr std::array<MaterialRampPresetInfo, 4u> kRampPresets{ {
            { MaterialRampPresetId::ToonHard, "Toon Hard 3-Step",
                "HIKARI_ToonHard3Step",
                "Hard three-band Ramp for readable cel shading." },
            { MaterialRampPresetId::ToonSoft, "Toon Soft 3-Step",
                "HIKARI_ToonSoft3Step",
                "Three-band Ramp with narrow hand-authored transitions." },
            { MaterialRampPresetId::SkinWarm, "Skin Warm",
                "HIKARI_SkinWarm",
                "Warm skin shadow and restrained highlight transition." },
            { MaterialRampPresetId::CoolShadow, "Cool Shadow",
                "HIKARI_CoolShadow",
                "Cool violet-blue shadow Ramp for character clothing." },
        } };

        MaterialRampPointData RampPoint(
            std::string id, float position,
            float red, float green, float blue) {
            MaterialRampPointData point{};
            point.id = std::move(id);
            point.position = { position, 0.0f };
            point.color = { red, green, blue, 1.0f };
            return point;
        }

        MaterialValueData MakeValue(
            MaterialValueType type,
            float x,
            float y = 0.0f,
            float z = 0.0f,
            float w = 0.0f) {

            MaterialValueData value{};
            value.type = type;
            value.numeric = { x, y, z, w };
            return value;
        }

        void AddParameter(
            MaterialAssetData& data,
            std::string id,
            std::string displayName,
            MaterialValueData value) {

            data.graph.parameters.push_back(MaterialGraphParameterData{
                std::move(id), std::move(displayName), value, true
            });
        }

        void AddResource(
            MaterialAssetData& data,
            std::string id,
            std::string displayName,
            MaterialResourceType type,
            MaterialResourceSemantic semantic) {

            MaterialGraphResourceData resource{};
            resource.id = std::move(id);
            resource.displayName = std::move(displayName);
            resource.type = type;
            resource.semantic = semantic;
            data.graph.resources.push_back(std::move(resource));
        }

        void AddNode(
            MaterialAssetData& data,
            std::string id,
            std::string displayName,
            MaterialNodeKind kind,
            MaterialValueType valueType,
            float x,
            float y,
            std::string referenceId = {}) {

            MaterialGraphNodeData node{};
            node.id = std::move(id);
            node.displayName = std::move(displayName);
            node.kind = kind;
            node.valueType = valueType;
            node.position = { x, y };
            node.referenceId = std::move(referenceId);
            InitializeReadyMadeMaterialNode(data, node);
            data.graph.nodes.push_back(std::move(node));
        }

        void Connect(
            MaterialAssetData& data,
            std::string id,
            std::string sourceNode,
            std::string sourcePin,
            std::string targetPin) {

            data.graph.edges.push_back(MaterialGraphEdgeData{
                std::move(id), std::move(sourceNode), std::move(sourcePin),
                "node.standardPbr", std::move(targetPin)
            });
        }

        void AddRampInputs(
            MaterialAssetData& data,
            bool multiRamp) {
            AddParameter(data, "parameter.shadowThreshold", "Shadow Threshold",
                MakeValue(MaterialValueType::Float1, 0.46f));
            AddParameter(data, "parameter.shadowSoftness", "Shadow Softness",
                MakeValue(MaterialValueType::Float1, 0.025f));
            AddParameter(data, "parameter.midThreshold", "Midtone Threshold",
                MakeValue(MaterialValueType::Float1, 0.70f));
            AddParameter(data, "parameter.shadowTint", "Shadow Tint",
                MakeValue(MaterialValueType::Float3, 0.38f, 0.45f, 0.62f));
            AddParameter(data, "parameter.midTint", "Midtone Tint",
                MakeValue(MaterialValueType::Float3, 0.72f, 0.78f, 0.88f));
            AddParameter(data, "parameter.litTint", "Lit Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
            AddParameter(data, "parameter.rampRow", "Ramp Row",
                MakeValue(MaterialValueType::Float1, 0.5f));
            AddParameter(data, "parameter.rampMaskInfluence", "Ramp Mask Influence",
                MakeValue(MaterialValueType::Float1, 0.0f));
            AddParameter(data, "parameter.rampBlend", "Secondary Ramp Blend",
                MakeValue(MaterialValueType::Float1, multiRamp ? 0.5f : 0.0f));
            AddParameter(data, "parameter.rampIntensity", "Ramp Influence",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddResource(data, "resource.diffuseRamp", "Diffuse Ramp",
                multiRamp ? MaterialResourceType::Ramp2D : MaterialResourceType::Ramp1D,
                MaterialResourceSemantic::Ramp);
            if (multiRamp) {
                AddResource(data, "resource.secondaryRamp", "Secondary Ramp",
                    MaterialResourceType::Ramp2D,
                    MaterialResourceSemantic::Ramp);
            }
        }

        void AddRamp(
            MaterialAssetData& data,
            bool multiRamp = false) {

            AddRampInputs(data, multiRamp);
            AddNode(data, "node.diffuseRamp", "Diffuse Ramp",
                multiRamp ? MaterialNodeKind::MultiRampDiffuse :
                    MaterialNodeKind::RampDiffuse,
                MaterialValueType::DiffuseResponse, -320.0f, 420.0f,
                "resource.diffuseRamp");
            Connect(data, "edge.diffuseRamp.response", "node.diffuseRamp",
                "Response", "DiffuseResponse");
        }

        void AddSpecularBand(MaterialAssetData& data) {
            AddParameter(data, "parameter.specularBandWidth", "Specular Band Width",
                MakeValue(MaterialValueType::Float1, 0.075f));
            AddParameter(data, "parameter.specularBandSoftness", "Specular Band Softness",
                MakeValue(MaterialValueType::Float1, 0.012f));
            AddParameter(data, "parameter.specularIntensity", "Specular Intensity",
                MakeValue(MaterialValueType::Float1, 0.35f));
            AddParameter(data, "parameter.specularPower", "Specular Power",
                MakeValue(MaterialValueType::Float1, 96.0f));
            AddParameter(data, "parameter.specularTint", "Specular Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
            AddNode(data, "node.specularBand", "Specular Band",
                MaterialNodeKind::SpecularBand,
                MaterialValueType::SpecularResponse, -320.0f, 760.0f);
            Connect(data, "edge.specularBand.response", "node.specularBand",
                "Response", "SpecularResponse");
        }

        void AddFresnelRim(MaterialAssetData& data) {
            AddParameter(data, "parameter.rimPower", "Rim Power",
                MakeValue(MaterialValueType::Float1, 4.0f));
            AddParameter(data, "parameter.rimWidth", "Rim Width",
                MakeValue(MaterialValueType::Float1, 0.22f));
            AddParameter(data, "parameter.rimColor", "Rim Color",
                MakeValue(MaterialValueType::Float3, 0.68f, 0.78f, 1.0f));
            AddParameter(data, "parameter.rimIntensity", "Rim Intensity",
                MakeValue(MaterialValueType::Float1, 0.28f));
            AddParameter(data, "parameter.rimLightAlignment", "Rim Light Alignment",
                MakeValue(MaterialValueType::Float1, 0.35f));
            AddNode(data, "node.fresnelRim", "Fresnel Rim",
                MaterialNodeKind::FresnelRim, MaterialValueType::Float3,
                -320.0f, 1040.0f);
            Connect(data, "edge.fresnelRim.color", "node.fresnelRim",
                "Color", "Additive");
        }

        void AddMaskResource(
            MaterialAssetData& data,
            std::string id,
            std::string displayName,
            MaterialResourceSemantic semantic = MaterialResourceSemantic::Mask) {

            AddResource(data, id, displayName,
                MaterialResourceType::Texture2D, semantic);
        }

        void AddOutline(MaterialAssetData& data, float mode) {
            AddNode(data, "node.outline", "Outline",
                MaterialNodeKind::Outline,
                MaterialValueType::OutlineResponse, -320.0f, 1600.0f);
            const auto parameter = std::find_if(
                data.graph.parameters.begin(), data.graph.parameters.end(),
                [](const MaterialGraphParameterData& value) {
                    return value.id == "parameter.outlineMode";
                });
            if (parameter != data.graph.parameters.end()) {
                parameter->defaultValue.numeric.x = mode;
            }
            Connect(data, "edge.outline.response", "node.outline",
                "Response", "Outline");
        }

        void ConfigureGenericToon(MaterialAssetData& data) {
            AddRamp(data);
            AddSpecularBand(data);
            AddFresnelRim(data);
            AddMaskResource(data, "resource.stylizationMask", "Stylization Mask");
            AddOutline(data, 1.0f);
            data.featureRequests.clear();
        }

        void ConfigureSkin(MaterialAssetData& data) {
            AddRamp(data);
            AddFresnelRim(data);
            AddParameter(data, "parameter.skinWrap", "Skin Light Wrap",
                MakeValue(MaterialValueType::Float1, 0.18f));
            AddParameter(data, "parameter.skinTint", "Skin Scatter Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 0.42f, 0.32f));
            AddParameter(data, "parameter.skinSubsurface", "Skin Subsurface Strength",
                MakeValue(MaterialValueType::Float1, 0.12f));
            AddMaskResource(data, "resource.thicknessMask", "Thickness Mask");
            TEMPLATE_FACTORY_INTERNAL::AddSkinLightingGraph(data);
            AddOutline(data, 1.0f);
            data.featureRequests.clear();
        }

        void ConfigureFace(MaterialAssetData& data) {
            AddRampInputs(data, false);
            AddParameter(data, "parameter.faceShadowSoftness", "Face Shadow Softness",
                MakeValue(MaterialValueType::Float1, 0.04f));
            AddParameter(data, "parameter.faceShadowOffset", "Face Shadow Offset",
                MakeValue(MaterialValueType::Float1, 0.0f));
            AddParameter(data, "parameter.faceHairShadowStrength", "Hair Shadow Strength",
                MakeValue(MaterialValueType::Float1, 0.42f));
            AddParameter(data, "parameter.faceHairShadowOffset", "Hair Shadow Offset (px)",
                MakeValue(MaterialValueType::Float1, 3.0f));
            AddParameter(data, "parameter.faceRight", "Face Local Right",
                MakeValue(MaterialValueType::Float3, 1.0f, 0.0f, 0.0f));
            AddParameter(data, "parameter.faceForward", "Face Local Forward",
                MakeValue(MaterialValueType::Float3, 0.0f, 0.0f, 1.0f));
            AddResource(data, "resource.faceSdf", "Face SDF",
                MaterialResourceType::Texture2D, MaterialResourceSemantic::FaceSdf);
            AddNode(data, "node.faceSdf", "Face SDF Lighting",
                MaterialNodeKind::FaceSdf, MaterialValueType::DiffuseResponse,
                -320.0f, 420.0f, "resource.faceSdf");
            Connect(data, "edge.faceSdf.response", "node.faceSdf",
                "Response", "DiffuseResponse");
            AddOutline(data, 1.0f);
            data.featureRequests = {
                MaterialFeatureRequest::FaceHairShadow
            };
        }

        void ConfigureHair(MaterialAssetData& data) {
            AddRamp(data);
            AddOutline(data, 0.0f);
            AddParameter(data, "parameter.hairShift", "Hair Highlight Shift",
                MakeValue(MaterialValueType::Float1, 0.08f));
            AddParameter(data, "parameter.hairWidth", "Hair Highlight Width",
                MakeValue(MaterialValueType::Float1, 0.18f));
            AddParameter(data, "parameter.hairSecondaryShift", "Hair Secondary Shift",
                MakeValue(MaterialValueType::Float1, -0.10f));
            AddParameter(data, "parameter.hairIntensity", "Hair Highlight Intensity",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddParameter(data, "parameter.hairHighlightTint", "Hair Highlight Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 0.92f, 0.84f));
            AddMaskResource(data, "resource.hairControl",
                "Hair Control (RG flow, B mask, A shift)");
            AddNode(data, "node.anisotropicHair", "Anisotropic Hair",
                MaterialNodeKind::AnisotropicHair,
                MaterialValueType::SpecularResponse, -320.0f, 760.0f);
            Connect(data, "edge.hair.response", "node.anisotropicHair",
                "Response", "SpecularResponse");
            data.featureRequests = {
                MaterialFeatureRequest::FaceHairShadow
            };
        }

        void ConfigureEye(MaterialAssetData& data) {
            AddParameter(data, "parameter.irisDepth", "Iris Depth",
                MakeValue(MaterialValueType::Float1, 0.12f));
            AddParameter(data, "parameter.eyeSparkle", "Eye Sparkle",
                MakeValue(MaterialValueType::Float1, 0.35f));
            AddParameter(data, "parameter.eyeSparklePower", "Eye Sparkle Power",
                MakeValue(MaterialValueType::Float1, 64.0f));
            AddMaskResource(data, "resource.eyeControl",
                "Eye Control (R iris, G cornea, B catchlight, A depth)");
            AddNode(data, "node.eyeLighting", "Eye Lighting",
                MaterialNodeKind::EyeLighting, MaterialValueType::Float3,
                -320.0f, 420.0f);
            Connect(data, "edge.eyeLighting.color", "node.eyeLighting",
                "Color", "Additive");
            AddOutline(data, 1.0f);
            data.featureRequests.clear();
        }

        void ConfigureCloth(MaterialAssetData& data) {
            AddRamp(data);
            AddFresnelRim(data);
            AddParameter(data, "parameter.fabricSheen", "Fabric Sheen",
                MakeValue(MaterialValueType::Float1, 0.28f));
            AddParameter(data, "parameter.fiberRoughness", "Fiber Roughness",
                MakeValue(MaterialValueType::Float1, 0.72f));
            AddParameter(data, "parameter.fabricSheenPower", "Fabric Sheen Power",
                MakeValue(MaterialValueType::Float1, 4.0f));
            AddParameter(data, "parameter.fabricSheenIntensity", "Fabric Sheen Intensity",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddMaskResource(data, "resource.fabricMask", "Fabric Mask");
            TEMPLATE_FACTORY_INTERNAL::AddFabricSheenGraph(data);
            AddOutline(data, 1.0f);
            data.featureRequests.clear();
        }

        void ConfigureHybridMetal(MaterialAssetData& data) {
            AddRamp(data, true);
            AddOutline(data, 0.0f);
            AddSpecularBand(data);
            AddFresnelRim(data);
            AddParameter(data, "parameter.directDiffusePbrBlend", "Direct Diffuse PBR Blend",
                MakeValue(MaterialValueType::Float1, 0.55f));
            AddParameter(data, "parameter.directSpecularPbrBlend", "Direct Specular PBR Blend",
                MakeValue(MaterialValueType::Float1, 0.55f));
            AddParameter(data, "parameter.indirectPbrBlend", "Indirect PBR Blend",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddParameter(data, "parameter.matCapTint", "MatCap Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
            AddParameter(data, "parameter.matCapIntensity", "MatCap Intensity",
                MakeValue(MaterialValueType::Float1, 0.25f));
            AddResource(data, "resource.matCap", "MatCap",
                MaterialResourceType::Texture2D, MaterialResourceSemantic::MatCap);
            TEMPLATE_FACTORY_INTERNAL::AddHybridLightingGraph(data);
            data.featureRequests.clear();
        }

    } // namespace

    const std::array<MaterialTemplateInfo, 8u>&
        GetBuiltInMaterialTemplates() noexcept {
        return kTemplates;
    }

    const std::array<MaterialRampPresetInfo, 4u>&
        GetBuiltInMaterialRampPresets() noexcept {
        return kRampPresets;
    }

    MaterialAssetData CreateMaterialRampFromPreset(
        MaterialRampPresetId presetId,
        std::string_view displayName) {
        const auto found = std::find_if(
            kRampPresets.begin(), kRampPresets.end(),
            [presetId](const MaterialRampPresetInfo& info) {
                return info.id == presetId;
            });
        const MaterialRampPresetInfo& preset = found != kRampPresets.end()
            ? *found : kRampPresets[1u];
        MaterialAssetData data{};
        data.kind = MaterialAssetKind::Ramp;
        data.templateId = MaterialTemplateId::Custom;
        data.materialName = displayName.empty()
            ? std::string(preset.displayName)
            : std::string(displayName);
        data.ramp.dimension = MaterialRampDimension::OneD;
        data.ramp.width = 256u;
        data.ramp.height = 1u;
        switch (preset.id) {
        case MaterialRampPresetId::ToonHard:
            data.ramp.interpolation = MaterialRampInterpolation::Constant;
            data.ramp.points = {
                RampPoint("shadow", 0.00f, 0.28f, 0.34f, 0.50f),
                RampPoint("midtone", 0.48f, 0.68f, 0.74f, 0.84f),
                RampPoint("light", 0.74f, 1.00f, 1.00f, 1.00f),
            };
            break;
        case MaterialRampPresetId::SkinWarm:
            data.ramp.interpolation = MaterialRampInterpolation::Smooth;
            data.ramp.points = {
                RampPoint("shadow", 0.00f, 0.48f, 0.25f, 0.25f),
                RampPoint("shadow_edge", 0.43f, 0.62f, 0.36f, 0.34f),
                RampPoint("midtone", 0.50f, 0.88f, 0.68f, 0.62f),
                RampPoint("light", 0.76f, 1.00f, 0.96f, 0.92f),
            };
            break;
        case MaterialRampPresetId::CoolShadow:
            data.ramp.interpolation = MaterialRampInterpolation::Smooth;
            data.ramp.points = {
                RampPoint("shadow", 0.00f, 0.22f, 0.28f, 0.48f),
                RampPoint("shadow_edge", 0.44f, 0.32f, 0.40f, 0.62f),
                RampPoint("midtone", 0.51f, 0.70f, 0.76f, 0.90f),
                RampPoint("light", 0.76f, 1.00f, 1.00f, 1.00f),
            };
            break;
        case MaterialRampPresetId::ToonSoft:
        default:
            data.ramp.interpolation = MaterialRampInterpolation::Smooth;
            data.ramp.points = {
                RampPoint("shadow", 0.00f, 0.30f, 0.36f, 0.52f),
                RampPoint("shadow_edge", 0.43f, 0.34f, 0.41f, 0.58f),
                RampPoint("midtone", 0.49f, 0.70f, 0.76f, 0.86f),
                RampPoint("midtone_edge", 0.69f, 0.74f, 0.80f, 0.90f),
                RampPoint("light", 0.75f, 1.00f, 1.00f, 1.00f),
            };
            break;
        }
        return data;
    }

    bool IsBuiltInMaterialTemplate(
        MaterialTemplateId templateId) noexcept {
        return std::any_of(
            kTemplates.begin(), kTemplates.end(),
            [templateId](const MaterialTemplateInfo& value) {
                return value.id == templateId;
            });
    }

    MaterialAssetData CreateMaterialFromTemplate(
        MaterialTemplateId templateId,
        std::string_view displayName) {

        if (!IsBuiltInMaterialTemplate(templateId)) {
            templateId = MaterialTemplateId::StandardPbr;
        }
        PbrMaterialAssetData pbr{};
        pbr.version = kMaterialAssetSourceVersion;
        pbr.materialName = displayName.empty()
            ? std::string(ToString(templateId))
            : std::string(displayName);
        MaterialAssetData data = MakeStandardPbrMaterialAssetData(pbr);
        data.sourceVersion = kMaterialAssetSourceVersion;
        data.migratedFromLegacy = false;
        data.templateId = templateId;
        switch (templateId) {
        case MaterialTemplateId::GenericToon: ConfigureGenericToon(data); break;
        case MaterialTemplateId::Skin: ConfigureSkin(data); break;
        case MaterialTemplateId::Face: ConfigureFace(data); break;
        case MaterialTemplateId::Hair: ConfigureHair(data); break;
        case MaterialTemplateId::Eye: ConfigureEye(data); break;
        case MaterialTemplateId::Cloth: ConfigureCloth(data); break;
        case MaterialTemplateId::HybridMetal: ConfigureHybridMetal(data); break;
        case MaterialTemplateId::StandardPbr:
        case MaterialTemplateId::Custom:
            break;
        }
        std::sort(data.featureRequests.begin(), data.featureRequests.end(),
            [](const auto left, const auto right) {
                return ToString(left) < ToString(right);
            });
        data.featureRequests.erase(
            std::unique(
                data.featureRequests.begin(), data.featureRequests.end()),
            data.featureRequests.end());
        return data;
    }

} // namespace HIKARI::ASSETS::MATERIAL
