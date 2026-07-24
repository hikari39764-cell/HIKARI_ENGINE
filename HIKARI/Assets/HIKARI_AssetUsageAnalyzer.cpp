#include "HIKARI_AssetUsageAnalyzer.h"

#include "HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    namespace {
        void AddReference(
            const std::string& assetId,
            const std::string& owner,
            const std::string& role,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary) {

            if (assetId.empty()) {
                return;
            }

            if (const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ assetId })) {
                summary.usedGuids.insert(record->guid.value);
                return;
            }

            summary.missingReferences.push_back(AssetMissingReference{
                assetId,
                owner,
                role
            });
        }

        void AddMaterialTextureReference(
            const MaterialTextureSlotData& slot,
            const std::string& owner,
            const std::string& role,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid()) {
                return;
            }
            AddReference(slot.textureAssetGuid.value, owner, role, assetDatabase, summary);
        }

        void AddMaterialTextureReferences(
            const std::string& materialGuid,
            const std::string& owner,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary) {

            const AssetRecord* materialRecord = assetDatabase.FindByGuid(AssetGuid{ materialGuid });
            if (!materialRecord || materialRecord->type != AssetType::Material) {
                return;
            }

            PbrMaterialAssetData data{};
            std::string error{};
            if (!LoadPbrMaterialAssetData(assetDatabase.GetProjectRoot() / materialRecord->sourcePath, data, error)) {
                return;
            }

            // Material Asset が参照する Texture も scene usage に含める。
            AddMaterialTextureReference(data.baseColorTexture, owner, "Material BaseColor", assetDatabase, summary);
            AddMaterialTextureReference(data.normalTexture, owner, "Material Normal", assetDatabase, summary);
            AddMaterialTextureReference(data.metallicRoughnessTexture, owner, "Material MetallicRoughness", assetDatabase, summary);
            AddMaterialTextureReference(data.occlusionTexture, owner, "Material Occlusion", assetDatabase, summary);
            AddMaterialTextureReference(data.emissiveTexture, owner, "Material Emissive", assetDatabase, summary);
            AddMaterialTextureReference(data.specularTexture, owner, "Material Specular", assetDatabase, summary);
            AddMaterialTextureReference(data.specularColorTexture, owner, "Material SpecularColor", assetDatabase, summary);
        }
    }

    AssetUsageSummary AnalyzeAssetUsage(
        const SceneDocument& document,
        const AssetDatabase& assetDatabase) {

        AssetUsageSummary summary{};

        AddReference(
            document.environment.sky.skyAsset,
            "Scene Environment",
            "Sky",
            assetDatabase,
            summary);
        AddReference(
            document.environment.reflectionProbe.sourceCubemapAsset,
            "Scene Environment",
            "Reflection Probe Cubemap",
            assetDatabase,
            summary);

        for (const SceneObjectData& object : document.objects) {
            const std::string owner = object.name.empty()
                ? ("Object " + std::to_string(object.id.value))
                : object.name;

            for (const SceneComponentData& component : object.components) {
                if (component.type == "ModelComponent") {
                    AddReference(
                        component.properties.value("assetId", std::string{}),
                        owner,
                        "Model",
                        assetDatabase,
                        summary);
                    if (component.properties.contains("materialOverrides") &&
                        component.properties["materialOverrides"].is_array()) {
                        for (const nlohmann::json& slot : component.properties["materialOverrides"]) {
                            const std::string materialGuid = slot.value("materialAssetGuid", std::string{});
                            AddReference(materialGuid, owner, "Material Override", assetDatabase, summary);
                            AddMaterialTextureReferences(materialGuid, owner, assetDatabase, summary);
                        }
                    }
                } else if (component.type ==
                        "SequencePlayerComponent") {
                    AddReference(
                        component.properties.value(
                            "sequenceAssetGuid",
                            std::string{}),
                        owner,
                        "Sequence",
                        assetDatabase,
                        summary);
                } else if (component.type ==
                        "AnimationStateMachineComponent") {
                    AddReference(
                        component.properties.value(
                            "stateMachineAssetGuid",
                            std::string{}),
                        owner,
                        "Animation State Machine",
                        assetDatabase,
                        summary);
                } else if (component.type == "ColliderComponent" &&
                    component.properties.value(
                        "fitMode",
                        std::string{}) == "CollisionAsset") {
                    AddReference(
                        component.properties.value(
                            "collisionAssetId",
                            std::string{}),
                        owner,
                        "Collision Geometry",
                        assetDatabase,
                        summary);
                }
            }
        }

        return summary;
    }

} // namespace HIKARI
