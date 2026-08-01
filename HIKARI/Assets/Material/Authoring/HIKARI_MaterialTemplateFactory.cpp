#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"

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
                MakeValue(MaterialValueType::Float1, 0.50f));
            AddParameter(data, "parameter.shadowSoftness", "Shadow Softness",
                MakeValue(MaterialValueType::Float1, 0.08f));
            AddParameter(data, "parameter.midThreshold", "Midtone Threshold",
                MakeValue(MaterialValueType::Float1, 0.72f));
            AddParameter(data, "parameter.shadowTint", "Shadow Tint",
                MakeValue(MaterialValueType::Float3, 0.56f, 0.62f, 0.78f));
            AddParameter(data, "parameter.midTint", "Midtone Tint",
                MakeValue(MaterialValueType::Float3, 0.82f, 0.86f, 0.94f));
            AddParameter(data, "parameter.litTint", "Lit Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
            AddParameter(data, "parameter.rampRow", "Ramp Row",
                MakeValue(MaterialValueType::Float1, 0.5f));
            AddParameter(data, "parameter.rampMaskInfluence", "Ramp Mask Influence",
                MakeValue(MaterialValueType::Float1, 0.0f));
            AddParameter(data, "parameter.rampBlend", "Secondary Ramp Blend",
                MakeValue(MaterialValueType::Float1, multiRamp ? 0.5f : 0.0f));
            AddParameter(data, "parameter.rampIntensity", "Ramp Intensity",
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
                MaterialValueType::DiffuseResponse, -320.0f, 150.0f);
            Connect(data, "edge.diffuseRamp.response", "node.diffuseRamp",
                "Response", "DiffuseResponse");
        }

        void AddSpecularBand(MaterialAssetData& data) {
            AddParameter(data, "parameter.specularBandWidth", "Specular Band Width",
                MakeValue(MaterialValueType::Float1, 0.10f));
            AddParameter(data, "parameter.specularBandSoftness", "Specular Band Softness",
                MakeValue(MaterialValueType::Float1, 0.025f));
            AddParameter(data, "parameter.specularIntensity", "Specular Intensity",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddParameter(data, "parameter.specularPower", "Specular Power",
                MakeValue(MaterialValueType::Float1, 64.0f));
            AddParameter(data, "parameter.specularTint", "Specular Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 1.0f, 1.0f));
            AddNode(data, "node.specularBand", "Specular Band",
                MaterialNodeKind::SpecularBand,
                MaterialValueType::SpecularResponse, -320.0f, 300.0f);
            Connect(data, "edge.specularBand.response", "node.specularBand",
                "Response", "SpecularResponse");
        }

        void AddFresnelRim(MaterialAssetData& data) {
            AddParameter(data, "parameter.rimPower", "Rim Power",
                MakeValue(MaterialValueType::Float1, 4.0f));
            AddParameter(data, "parameter.rimWidth", "Rim Width",
                MakeValue(MaterialValueType::Float1, 0.22f));
            AddParameter(data, "parameter.rimColor", "Rim Color",
                MakeValue(MaterialValueType::Float3, 0.55f, 0.68f, 1.0f));
            AddParameter(data, "parameter.rimIntensity", "Rim Intensity",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddParameter(data, "parameter.rimLightAlignment", "Rim Light Alignment",
                MakeValue(MaterialValueType::Float1, 0.35f));
            AddNode(data, "node.fresnelRim", "Fresnel Rim",
                MaterialNodeKind::FresnelRim,
                MaterialValueType::Float3, -320.0f, 450.0f);
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
            AddNode(data, "node." + id.substr(id.find('.') + 1u), displayName + " Sample",
                MaterialNodeKind::TextureSample, MaterialValueType::Float4,
                -650.0f, 300.0f, std::move(id));
        }

        void AddGeometryOutlineParameters(MaterialAssetData& data) {
            AddParameter(data, "parameter.outlineColor", "Outline Color",
                MakeValue(MaterialValueType::Float3, 0.025f, 0.030f, 0.045f));
            AddParameter(data, "parameter.outlineThickness", "Outline Thickness (px)",
                MakeValue(MaterialValueType::Float1, 1.25f));
        }

        void ConfigureGenericToon(MaterialAssetData& data) {
            AddRamp(data);
            AddSpecularBand(data);
            AddFresnelRim(data);
            AddMaskResource(data, "resource.stylizationMask", "Stylization Mask");
            data.featureRequests = { MaterialFeatureRequest::ScreenOutline };
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
            data.featureRequests = { MaterialFeatureRequest::ScreenOutline };
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
                -320.0f, 150.0f, "resource.faceSdf");
            Connect(data, "edge.faceSdf.response", "node.faceSdf",
                "Response", "DiffuseResponse");
            data.featureRequests = {
                MaterialFeatureRequest::ScreenOutline,
                MaterialFeatureRequest::FaceHairShadow
            };
        }

        void ConfigureHair(MaterialAssetData& data) {
            AddRamp(data);
            AddGeometryOutlineParameters(data);
            AddParameter(data, "parameter.hairShift", "Hair Highlight Shift",
                MakeValue(MaterialValueType::Float1, 0.08f));
            AddParameter(data, "parameter.hairWidth", "Hair Highlight Width",
                MakeValue(MaterialValueType::Float1, 0.18f));
            AddParameter(data, "parameter.hairSecondaryShift", "Hair Secondary Shift",
                MakeValue(MaterialValueType::Float1, -0.10f));
            AddParameter(data, "parameter.hairIntensity", "Hair Highlight Intensity",
                MakeValue(MaterialValueType::Float1, 1.0f));
            AddParameter(data, "parameter.specularTint", "Hair Highlight Tint",
                MakeValue(MaterialValueType::Float3, 1.0f, 0.92f, 0.84f));
            AddMaskResource(data, "resource.strandMask", "Strand Mask");
            AddNode(data, "node.anisotropicHair", "Anisotropic Hair",
                MaterialNodeKind::AnisotropicHair,
                MaterialValueType::SpecularResponse, -320.0f, 320.0f);
            Connect(data, "edge.hair.response", "node.anisotropicHair",
                "Response", "SpecularResponse");
            data.featureRequests = {
                MaterialFeatureRequest::GeometryOutline,
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
            AddMaskResource(data, "resource.irisMask", "Iris Mask");
            AddNode(data, "node.eyeLighting", "Eye Lighting",
                MaterialNodeKind::EyeLighting, MaterialValueType::Float3,
                -320.0f, 150.0f);
            Connect(data, "edge.eyeLighting.color", "node.eyeLighting",
                "Color", "Additive");
            data.featureRequests = { MaterialFeatureRequest::ScreenOutline };
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
            data.featureRequests = { MaterialFeatureRequest::ScreenOutline };
        }

        void ConfigureHybridMetal(MaterialAssetData& data) {
            AddRamp(data, true);
            AddGeometryOutlineParameters(data);
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
            AddNode(data, "node.matCap", "MatCap",
                MaterialNodeKind::MatCap, MaterialValueType::Float3,
                -650.0f, 480.0f, "resource.matCap");
            AddNode(data, "node.additive", "Rim + MatCap",
                MaterialNodeKind::Add, MaterialValueType::Float3,
                -320.0f, 480.0f);
            auto rimEdge = std::find_if(
                data.graph.edges.begin(),
                data.graph.edges.end(),
                [](const MaterialGraphEdgeData& edge) {
                    return edge.id == "edge.fresnelRim.color";
                });
            if (rimEdge != data.graph.edges.end()) {
                rimEdge->targetNodeId = "node.additive";
                rimEdge->targetPin = "A";
            }
            data.graph.edges.push_back(MaterialGraphEdgeData{
                "edge.matCap.color", "node.matCap", "Color",
                "node.additive", "B"
            });
            data.graph.edges.push_back(MaterialGraphEdgeData{
                "edge.additive.color", "node.additive", "Result",
                "node.standardPbr", "Additive"
            });
            data.featureRequests = { MaterialFeatureRequest::GeometryOutline };
        }

    } // namespace

    const std::array<MaterialTemplateInfo, 8u>&
        GetBuiltInMaterialTemplates() noexcept {
        return kTemplates;
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

    DefaultCharacterMaterialSet CreateDefaultCharacterMaterialSet(
        std::string_view displayNamePrefix) {

        const std::string prefix = displayNamePrefix.empty()
            ? "Character" : std::string(displayNamePrefix);
        struct EntrySeed {
            std::string_view roleId;
            std::string_view label;
            std::string_view use;
            MaterialTemplateId templateId;
        };
        constexpr std::array<EntrySeed, 6u> seeds{ {
            { "character.skin", "Skin",
                "skin and exposed body surfaces", MaterialTemplateId::Skin },
            { "character.face", "Face",
                "face surfaces that can provide an optional Face SDF",
                MaterialTemplateId::Face },
            { "character.hair", "Hair",
                "hair cards or strand-like surfaces", MaterialTemplateId::Hair },
            { "character.eye", "Eye",
                "iris, cornea and eye-highlight surfaces", MaterialTemplateId::Eye },
            { "character.cloth", "Cloth",
                "fabric, uniform and soft accessory surfaces",
                MaterialTemplateId::Cloth },
            { "character.hybridMetal", "Hybrid Metal",
                "metal accessories requiring both PBR and stylized response",
                MaterialTemplateId::HybridMetal },
        } };

        DefaultCharacterMaterialSet result{};
        result.entries.reserve(seeds.size());
        for (const EntrySeed& seed : seeds) {
            const std::string name = prefix + " " + std::string(seed.label);
            result.entries.push_back(CharacterMaterialSetEntry{
                std::string(seed.roleId),
                name,
                std::string(seed.use),
                seed.templateId,
                CreateMaterialFromTemplate(seed.templateId, name)
            });
        }
        result.diagnostics = {
            "Default Character Material Set is role-based; it does not infer or hard-code model slot names or indices.",
            "Unassigned Ramp resources use the engine neutral ramp fallback until an authored Ramp asset is bound.",
            "Face SDF, mask and MatCap resources are intentionally optional and use semantic fallbacks when unassigned."
        };
        return result;
    }

} // namespace HIKARI::ASSETS::MATERIAL
