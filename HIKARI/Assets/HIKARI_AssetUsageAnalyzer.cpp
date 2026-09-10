#include "HIKARI_AssetUsageAnalyzer.h"

#include "HIKARI_AssetDatabase.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Render3D/Lighting/HIKARI_DefaultSkyEnvironment.h"

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

        void AddMaterialReferences(
            const std::string& materialGuid,
            const std::string& owner,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary,
            std::unordered_set<std::string>& visitedMaterials) {

            if (materialGuid.empty()) {
                return;
            }

            // A material graph can be shared by multiple scene owners.  Keep
            // cycle protection local to each owner so every owner's package
            // closure still records the material's transitive dependencies.
            std::string visitKey = owner;
            visitKey.push_back('\x1f');
            visitKey += materialGuid;
            if (!visitedMaterials.emplace(visitKey).second) {
                return;
            }

            const AssetRecord* materialRecord = assetDatabase.FindByGuid(AssetGuid{ materialGuid });
            if (!materialRecord || materialRecord->type != AssetType::Material) {
                return;
            }

            MaterialAssetData data{};
            std::string error{};
            if (!LoadMaterialAssetData(
                    assetDatabase.GetProjectRoot() / materialRecord->sourcePath,
                    data,
                    error)) {
                return;
            }

            // Material v4 の Texture / Function / Ramp / Profile / Parent を同じ依存契約で辿る。
            for (const MaterialAssetDependency& dependency :
                CollectMaterialAssetDependencies(data)) {
                AddReference(
                    dependency.assetGuid.value,
                    owner,
                    "Material " + dependency.role,
                    assetDatabase,
                    summary);
                if (dependency.kind != MaterialDependencyKind::Texture) {
                    AddMaterialReferences(
                        dependency.assetGuid.value,
                        owner,
                        assetDatabase,
                        summary,
                        visitedMaterials);
                }
            }
        }

        void AddModelMaterialBindingReferences(
            const std::string& modelGuid,
            const std::string& materialSetGuid,
            bool explicitSelection,
            const std::string& owner,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary,
            std::unordered_set<std::string>& visitedMaterials) {

            if (modelGuid.empty()) {
                return;
            }
            if (explicitSelection && materialSetGuid.empty()) {
                return;
            }
            for (const AssetRecord* record :
                assetDatabase.CollectByType(
                    AssetType::ModelMaterialBinding)) {
                if (record == nullptr || !record->guid.IsValid()) {
                    continue;
                }
                if (explicitSelection &&
                    record->guid.value != materialSetGuid) {
                    continue;
                }
                const std::filesystem::path sourcePath =
                    record->sourcePath.is_absolute()
                        ? record->sourcePath
                        : (assetDatabase.GetProjectRoot() /
                            record->sourcePath).lexically_normal();
                ASSETS::MODELS::ModelMaterialBindingAssetData data{};
                std::string error{};
                if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(
                        sourcePath, data, error) ||
                    data.modelAssetGuid.value != modelGuid) {
                    continue;
                }
                AddReference(
                    record->guid.value,
                    owner,
                    "Material Set",
                    assetDatabase,
                    summary);
                for (const auto& binding : data.bindings) {
                    AddReference(
                        binding.materialAssetGuid.value,
                        owner,
                        "Material Set Part",
                        assetDatabase,
                        summary);
                    AddMaterialReferences(
                        binding.materialAssetGuid.value,
                        owner,
                        assetDatabase,
                        summary,
                        visitedMaterials);
                }
            }
        }

        bool IsGameProjectAssetGuidProperty(
            std::string_view propertyName) noexcept {
            return propertyName.ends_with("AssetGuid") ||
                propertyName.ends_with("assetGuid");
        }

        void AddGameProjectPropertyReferences(
            const nlohmann::json& value,
            std::string_view propertyName,
            const std::string& owner,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary) {
            if (value.is_object()) {
                for (auto it = value.begin(); it != value.end(); ++it) {
                    AddGameProjectPropertyReferences(
                        it.value(),
                        it.key(),
                        owner,
                        assetDatabase,
                        summary);
                }
                return;
            }
            if (value.is_array()) {
                for (const nlohmann::json& element : value) {
                    AddGameProjectPropertyReferences(
                        element,
                        propertyName,
                        owner,
                        assetDatabase,
                        summary);
                }
                return;
            }
            if (value.is_string() &&
                IsGameProjectAssetGuidProperty(propertyName)) {
                AddReference(
                    value.get<std::string>(),
                    owner,
                    "GameProject " + std::string(propertyName),
                    assetDatabase,
                    summary);
            }
        }
    }

    AssetUsageSummary AnalyzeAssetUsage(
        const SceneDocument& document,
        const AssetDatabase& assetDatabase) {

        AssetUsageSummary summary{};
        std::unordered_set<std::string> visitedMaterials{};

        if (document.environment.sky.skyAsset != RENDER3D::LIGHTING::kDefaultSkyAssetName) {
            AddReference(document.environment.sky.skyAsset,
                "Scene Environment", "Sky", assetDatabase, summary);
        }
        AddReference(
            document.environment.sky.panoramaCloudAsset,
            "Scene Environment",
            "Panorama Cloud Layer",
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
                    const std::string modelGuid =
                        component.properties.value(
                            "assetId", std::string{});
                    AddReference(
                        modelGuid,
                        owner,
                        "Model",
                        assetDatabase,
                        summary);
                    const bool hasMaterialSetSelection =
                        component.properties.contains(
                            "materialSetAssetGuid");
                    AddModelMaterialBindingReferences(
                        modelGuid,
                        component.properties.value(
                            "materialSetAssetGuid",
                            std::string{}),
                        hasMaterialSetSelection,
                        owner,
                        assetDatabase,
                        summary,
                        visitedMaterials);
                    if (component.properties.contains("materialOverrides") &&
                        component.properties["materialOverrides"].is_array()) {
                        for (const nlohmann::json& slot : component.properties["materialOverrides"]) {
                            const std::string materialGuid = slot.value("materialAssetGuid", std::string{});
                            AddReference(materialGuid, owner, "Material Override", assetDatabase, summary);
                            AddMaterialReferences(
                                materialGuid,
                                owner,
                                assetDatabase,
                                summary,
                                visitedMaterials);
                        }
                    }
                } else if (component.type == "ProceduralMeshComponent") {
                    const auto material = component.properties.value("materialSelection", std::string{});
                    if (!ASSETS::MATERIAL::IsBuiltInWhiteMaterialAssetId(material) &&
                        !ASSETS::MATERIAL::IsBuiltInCheckerMaterialAssetId(material) &&
                        !ASSETS::MATERIAL::IsBuiltInSandMaterialAssetId(material)) {
                        AddReference(material, owner, "Procedural Material", assetDatabase, summary);
                        AddMaterialReferences(material, owner, assetDatabase, summary, visitedMaterials);
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
                } else if (component.type ==
                        "DialogueTriggerComponent") {
                    AddReference(
                        component.properties.value(
                            "dialogueAssetGuid",
                            std::string{}),
                        owner,
                        "Dialogue Trigger",
                        assetDatabase,
                        summary);
                } else if (component.type == "VfxPlayerComponent") {
                    const auto slots = component.properties.find("slots");
                    if (slots != component.properties.end() &&
                        slots->is_array()) {
                        for (const nlohmann::json& slot : *slots) {
                            if (!slot.is_object()) {
                                continue;
                            }
                            AddReference(
                                slot.value(
                                    "effectAssetGuid",
                                    std::string{}),
                                owner,
                                "VFX Player Slot",
                                assetDatabase,
                                summary);
                        }
                    }
                } else if (component.type ==
                        "ReflectionProbeComponent") {
                    AddReference(
                        component.properties.value(
                            "sourceCubemapAsset",
                            std::string{}),
                        owner,
                        "Reflection Probe Cubemap",
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
                // GameProject component schemas are owned by their module and
                // cannot be hard-coded into the engine analyzer. A narrow
                // AssetGuid naming contract closes over model/VFX/UI presets
                // such as Summer.MonsterSpawnComponent without scanning
                // arbitrary strings or source code.
                AddGameProjectPropertyReferences(
                    component.properties,
                    {},
                    owner,
                    assetDatabase,
                    summary);
            }
        }

        return summary;
    }

} // namespace HIKARI
