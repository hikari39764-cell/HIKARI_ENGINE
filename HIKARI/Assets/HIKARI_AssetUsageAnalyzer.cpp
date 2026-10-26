#include "HIKARI_AssetUsageAnalyzer.h"

#include "HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "HIKARI/Project/PropertyFields.h"

namespace HIKARI {

namespace {
void AddReference(const std::string& assetId, const std::string& owner, const std::string& role,
                  const AssetDatabase& assetDatabase, const AssetRegistry& engineTextures, AssetUsageSummary& summary) {
    if (assetId.empty()) {
        return;
    }

    if (const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{assetId})) {
        const bool first = summary.usedGuids.insert(record->guid.value).second;
        if (first && record->type == AssetType::TextStyle) {
            TextStyleAsset style;
            std::string error;
            if (LoadTextStyleAsset(assetDatabase.ResolveSourcePath(*record), style, error))
                AddReference(style.font.value, owner, "TextStyle.Font", assetDatabase, engineTextures, summary);
        }
        if (first && record->type == AssetType::PostProfile) {
            POST::PostProfileAsset profile;
            std::string error;
            if (POST::LoadPostProfileAsset(assetDatabase.ResolveSourcePath(*record), profile, error)) {
                for (const AssetDependencyDesc& dependency : POST::CollectPostProfileDependencies(profile))
                    AddReference(dependency.guid.value, owner, dependency.role, assetDatabase, engineTextures, summary);
            }
        }
        if (first && record->type == AssetType::Environment) {
            ENVIRONMENT::EnvironmentProfileAsset environment;
            std::string error;
            if (ENVIRONMENT::LoadEnvironmentProfileAsset(assetDatabase.ResolveSourcePath(*record), environment,
                                                         error)) {
                for (const AssetDependencyDesc& dependency :
                     ENVIRONMENT::CollectEnvironmentProfileDependencies(environment))
                    AddReference(dependency.guid.value, owner, dependency.role, assetDatabase, engineTextures, summary);
            }
        }
        if (first && record->type == AssetType::RenderStyle) {
            RENDERSTYLE::RenderStyleAsset style;
            std::string error;
            if (RENDERSTYLE::LoadRenderStyleAsset(assetDatabase.ResolveSourcePath(*record), style, error)) {
                for (const AssetDependencyDesc& dependency : RENDERSTYLE::CollectRenderStyleDependencies(style))
                    AddReference(dependency.guid.value, owner, dependency.role, assetDatabase, engineTextures, summary);
            }
        }
        if (first && record->type == AssetType::PostEffect) {
            POST::PostEffectDefinition effect;
            std::string error;
            if (POST::LoadPostEffectDefinition(assetDatabase.ResolveSourcePath(*record), effect, error)) {
                for (const AssetDependencyDesc& dependency : POST::CollectPostEffectDependencies(effect))
                    AddReference(dependency.guid.value, owner, dependency.role, assetDatabase, engineTextures, summary);
            }
        }
        return;
    }

    if (engineTextures.FindAs<TextureAssetDescriptor>(AssetId{assetId})) {
        return;
    }
    summary.missingReferences.push_back(AssetMissingReference{assetId, owner, role});
}

void AddMaterialReferences(const std::string& materialGuid, const std::string& owner,
                           const AssetDatabase& assetDatabase, const AssetRegistry& engineTextures,
                           AssetUsageSummary& summary, std::unordered_set<std::string>& visitedMaterials) {
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

    const AssetRecord* materialRecord = assetDatabase.FindByGuid(AssetGuid{materialGuid});
    if (!materialRecord || materialRecord->type != AssetType::Material) {
        return;
    }

    MaterialAssetData data{};
    std::string error{};
    if (!LoadMaterialAssetData(assetDatabase.ResolveSourcePath(*materialRecord), data, error)) {
        return;
    }

    // Material v4 の Texture / Function / Ramp / Profile / Parent を同じ依存契約で辿る。
    for (const MaterialAssetDependency& dependency : CollectMaterialAssetDependencies(data)) {
        AddReference(dependency.assetGuid.value, owner, "Material " + dependency.role, assetDatabase, engineTextures,
                     summary);
        if (dependency.kind != MaterialDependencyKind::Texture) {
            AddMaterialReferences(dependency.assetGuid.value, owner, assetDatabase, engineTextures, summary,
                                  visitedMaterials);
        }
    }
}

void AddModelMaterialBindingReferences(const std::string& modelGuid, const std::string& materialSetGuid,
                                       bool explicitSelection, const std::string& owner,
                                       const AssetDatabase& assetDatabase, const AssetRegistry& engineTextures,
                                       AssetUsageSummary& summary, std::unordered_set<std::string>& visitedMaterials) {
    if (modelGuid.empty()) {
        return;
    }
    if (explicitSelection && materialSetGuid.empty()) {
        return;
    }
    for (const AssetRecord* record : assetDatabase.CollectByType(AssetType::ModelMaterialBinding)) {
        if (record == nullptr || !record->guid.IsValid()) {
            continue;
        }
        if (explicitSelection && record->guid.value != materialSetGuid) {
            continue;
        }
        const std::filesystem::path sourcePath = assetDatabase.ResolveSourcePath(*record);
        ASSETS::MODELS::ModelMaterialBindingAssetData data{};
        std::string error{};
        if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, data, error) ||
            data.modelAssetGuid.value != modelGuid) {
            continue;
        }
        AddReference(record->guid.value, owner, "Material Set", assetDatabase, engineTextures, summary);
        for (const auto& binding : data.bindings) {
            AddReference(binding.materialAssetGuid.value, owner, "Material Set Part", assetDatabase, engineTextures,
                         summary);
            AddMaterialReferences(binding.materialAssetGuid.value, owner, assetDatabase, engineTextures, summary,
                                  visitedMaterials);
        }
    }
}

bool IsGameProjectAssetGuidProperty(std::string_view propertyName) noexcept {
    return propertyName.ends_with("AssetGuid") || propertyName.ends_with("assetGuid");
}

void AddGameProjectPropertyReferences(const nlohmann::json& value, std::string_view propertyName,
                                      const std::string& owner, const AssetDatabase& assetDatabase,
                                      const AssetRegistry& engineTextures, AssetUsageSummary& summary) {
    if (value.is_object()) {
        const auto taggedAssetGuid = value.find(std::string(kGameProjectAssetGuidArchiveMarker));
        if (taggedAssetGuid != value.end() && taggedAssetGuid->is_string()) {
            AddReference(taggedAssetGuid->get<std::string>(), owner, "GameProject AssetGuid", assetDatabase,
                         engineTextures, summary);
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() != kGameProjectAssetGuidArchiveMarker) {
                AddGameProjectPropertyReferences(it.value(), it.key(), owner, assetDatabase, engineTextures, summary);
            }
        }
        return;
    }
    if (value.is_array()) {
        for (const nlohmann::json& element : value) {
            AddGameProjectPropertyReferences(element, propertyName, owner, assetDatabase, engineTextures, summary);
        }
        return;
    }
    if (value.is_string() && IsGameProjectAssetGuidProperty(propertyName)) {
        AddReference(value.get<std::string>(), owner, "GameProject " + std::string(propertyName), assetDatabase,
                     engineTextures, summary);
    }
}
} // namespace

AssetUsageSummary AnalyzeAssetUsage(const SceneDocument& document, const AssetDatabase& assetDatabase,
                                    AssetUsageScope scope) {
    AssetUsageSummary summary{};
    AssetRegistry engineTextures{};
    (void)RegisterEngineContentTextures(CONTENT::GetEngineContentContext(), engineTextures);
    std::unordered_set<std::string> visitedMaterials{};
    if (scope == AssetUsageScope::Scene) {
        std::error_code styleError;
        if (std::filesystem::is_regular_file(assetDatabase.GetProjectRoot() / DIALOGUE::kDialogueProjectStylePath,
                                             styleError)) {
            DIALOGUE::DialogueProjectStyle style;
            if (DIALOGUE::LoadDialogueProjectStyle(assetDatabase.GetProjectRoot(), style)) {
                for (const auto& guid :
                     {style.theme.speakerTextStyle, style.theme.bodyTextStyle, style.theme.choiceTextStyle})
                    AddReference(guid.value, "Dialogue UI", "TextStyle", assetDatabase, engineTextures, summary);
                for (const auto& speaker : style.speakers)
                    AddReference(speaker.portraitAssetGuid, "Dialogue UI", "Portrait", assetDatabase, engineTextures,
                                 summary);
            }
        }

        // エンジン内蔵の環境と天気はエンジンの内容として出力されるので、プロジェクト資産だけを数える。
        const ENVIRONMENT::SceneEnvironmentBinding& environment = document.environment;
        if (!environment.profileId.starts_with(ENVIRONMENT::kBuiltinEnvironmentPrefix)) {
            AddReference(environment.profileId, "Scene Environment", "Environment", assetDatabase, engineTextures,
                         summary);
        }
        if (!environment.weatherId.starts_with(ENVIRONMENT::kBuiltinWeatherPrefix)) {
            AddReference(environment.weatherId, "Scene Environment", "Weather", assetDatabase, engineTextures, summary);
        }
        if (!environment.renderStyleId.starts_with(RENDERSTYLE::kBuiltinRenderStylePrefix)) {
            AddReference(environment.renderStyleId, "Scene Environment", "RenderStyle", assetDatabase, engineTextures,
                         summary);
        }
    }

    for (const SceneObjectData& object : document.objects) {
        const std::string owner = object.name.empty() ? ("Object " + std::to_string(object.id.value)) : object.name;

        for (const SceneComponentData& component : object.components) {
            if (component.type == "ModelComponent") {
                const std::string modelGuid = component.properties.value("assetId", std::string{});
                AddReference(modelGuid, owner, "Model", assetDatabase, engineTextures, summary);
                const bool hasMaterialSetSelection = component.properties.contains("materialSetAssetGuid");
                AddModelMaterialBindingReferences(
                    modelGuid, component.properties.value("materialSetAssetGuid", std::string{}),
                    hasMaterialSetSelection, owner, assetDatabase, engineTextures, summary, visitedMaterials);
                if (component.properties.contains("materialOverrides") &&
                    component.properties["materialOverrides"].is_array()) {
                    for (const nlohmann::json& slot : component.properties["materialOverrides"]) {
                        const std::string materialGuid = slot.value("materialAssetGuid", std::string{});
                        AddReference(materialGuid, owner, "Material Override", assetDatabase, engineTextures, summary);
                        AddMaterialReferences(materialGuid, owner, assetDatabase, engineTextures, summary,
                                              visitedMaterials);
                    }
                }
            } else if (component.type == "ProceduralMeshComponent") {
                const std::string material{ASSETS::MATERIAL::CanonicalizeProceduralMaterialSelection(
                    component.properties.value("materialSelection", std::string{}))};
                if (!ASSETS::MATERIAL::IsBuiltInWhiteMaterialAssetId(material) &&
                    !ASSETS::MATERIAL::IsBuiltInCheckerMaterialAssetId(material) &&
                    !ASSETS::MATERIAL::IsBuiltInSandMaterialAssetId(material)) {
                    AddReference(material, owner, "Procedural Material", assetDatabase, engineTextures, summary);
                    AddMaterialReferences(material, owner, assetDatabase, engineTextures, summary, visitedMaterials);
                }
            } else if (component.type == "CameraFollowComponent") {
                AddReference(component.properties.value("cameraProfile", std::string{}), owner, "Gameplay Camera",
                             assetDatabase, engineTextures, summary);
            } else if (component.type == "SequencePlayerComponent" || component.type == "CameraFlowComponent") {
                AddReference(component.properties.value("sequenceAssetGuid", std::string{}), owner, "Sequence",
                             assetDatabase, engineTextures, summary);
            } else if (component.type == "AnimationStateMachineComponent") {
                AddReference(component.properties.value("stateMachineAssetGuid", std::string{}), owner,
                             "Animation State Machine", assetDatabase, engineTextures, summary);
            } else if (component.type == "DialogueTriggerComponent") {
                AddReference(component.properties.value("dialogueAssetGuid", std::string{}), owner, "Dialogue Trigger",
                             assetDatabase, engineTextures, summary);
            } else if (component.type == "WorldSpriteComponent") {
                AddReference(component.properties.value("spriteAssetGuid", std::string{}), owner,
                             "World Sprite Animation", assetDatabase, engineTextures, summary);
                AddReference(component.properties.value("textureAssetGuid", std::string{}), owner,
                             "World Sprite Texture", assetDatabase, engineTextures, summary);
                const auto material = component.properties.find("spriteMaterial");
                if (material != component.properties.end() && material->is_object()) {
                    AddReference(material->value("assetGuid", std::string{}), owner, "World Sprite Material",
                                 assetDatabase, engineTextures, summary);
                }
            } else if (component.type == "VfxPlayerComponent") {
                const auto slots = component.properties.find("slots");
                if (slots != component.properties.end() && slots->is_array()) {
                    for (const nlohmann::json& slot : *slots) {
                        if (!slot.is_object()) {
                            continue;
                        }
                        AddReference(slot.value("effectAssetGuid", std::string{}), owner, "VFX Player Slot",
                                     assetDatabase, engineTextures, summary);
                    }
                }
            } else if (component.type == "LightComponent") {
                AddReference(component.properties.value("iesProfile", std::string{}), owner, "Light IES Profile",
                             assetDatabase, engineTextures, summary);
                AddReference(component.properties.value("cookie", std::string{}), owner, "Light Cookie", assetDatabase,
                             engineTextures, summary);
            } else if (component.type == "DecalComponent") {
                AddReference(component.properties.value("baseColorTexture", std::string{}), owner, "Decal Base Color",
                             assetDatabase, engineTextures, summary);
                AddReference(component.properties.value("normalTexture", std::string{}), owner, "Decal Normal",
                             assetDatabase, engineTextures, summary);
            } else if (component.type == "ReflectionProbeComponent") {
                AddReference(component.properties.value("sourceCubemapAsset", std::string{}), owner,
                             "Reflection Probe Cubemap", assetDatabase, engineTextures, summary);
            } else if (component.type == "ColliderComponent" &&
                       component.properties.value("fitMode", std::string{}) == "CollisionAsset") {
                AddReference(component.properties.value("collisionAssetId", std::string{}), owner, "Collision Geometry",
                             assetDatabase, engineTextures, summary);
            }
            // GameProject component schemas belong to their module. Prefer
            // explicit PropertyArchive AssetGuid tags while retaining the
            // suffix convention used by existing component properties.
            AddGameProjectPropertyReferences(component.properties, {}, owner, assetDatabase, engineTextures, summary);
        }
    }

    if (scope == AssetUsageScope::Scene) {
        for (const auto& sequence : document.cinematics.sequences) {
            for (const auto& cue : sequence.presentationCueTrack.clips)
                if (cue.kind == SEQUENCER::PresentationCueKind::Vfx)
                    AddReference(cue.vfxAsset.value, sequence.name, "Sequence.VfxCue", assetDatabase, engineTextures,
                                 summary);
            for (const auto& text : sequence.textTrack.clips)
                if (text.style.IsValid())
                    AddReference(text.style.value, sequence.name, "Sequence.TextStyle", assetDatabase, engineTextures,
                                 summary);
        }
    }
    return summary;
}

} // namespace HIKARI
