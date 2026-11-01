#include "HIKARI_AssetUsageAnalyzer.h"

#include "HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRegistryBuilder.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialBindingAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingSource.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI {

namespace {
// 参照された資産と、その中身から辿れる資産を summary に集める。
class UsageCollector {
public:
    UsageCollector(const AssetDatabase& assetDatabase, AssetUsageSummary& summary)
        : assetDatabase_(assetDatabase), summary_(summary) {
        (void)RegisterEngineContentTextures(CONTENT::GetEngineContentContext(), engineTextures_);
    }

    // 資産 ID を参照として数える。データベースにもエンジン内蔵のテクスチャにも無ければ欠落として残す。
    void AddReference(const std::string& assetId, const std::string& owner, const std::string& role) {
        if (assetId.empty()) {
            return;
        }
        if (const AssetRecord* record = assetDatabase_.FindByGuid(AssetGuid{assetId})) {
            // 初めて見た資産だけ中身を辿る（循環した参照もここで止まる）。
            if (summary_.usedGuids.insert(record->guid.value).second) {
                ExpandAsset(*record, owner);
            }
            return;
        }
        if (engineTextures_.FindAs<TextureAssetDescriptor>(AssetGuid{assetId})) {
            return;
        }
        summary_.missingReferences.push_back(AssetMissingReference{assetId, owner, role});
    }

    // 属性 JSON を再帰的に調べ、資産 GUID の形をした文字列を全て参照として数える。
    // フィールドの名前や種類は問わない（プロジェクトのコンポーネントも同じ）。path は役割の表示用。
    void ScanProperties(const nlohmann::json& value, const std::string& owner, const std::string& path) {
        if (value.is_string()) {
            const std::string& text = value.get_ref<const std::string&>();
            if (IsValidAssetGuid(text)) {
                AddReference(text, owner, path);
            }
            return;
        }
        // 数値や真偽値は辿る必要が無い。path の文字列も作らない。
        const auto descends = [](const nlohmann::json& child) { return child.is_string() || child.is_structured(); };
        if (value.is_array()) {
            for (size_t index = 0; index < value.size(); ++index) {
                if (descends(value[index])) {
                    ScanProperties(value[index], owner, path + "[" + std::to_string(index) + "]");
                }
            }
        } else if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (descends(it.value())) {
                    ScanProperties(it.value(), owner, path + "." + it.key());
                }
            }
        }
    }

    // 素材セットを選んでいない（materialSetAssetGuid が無い）ModelComponent は、そのモデルに結び付いた素材セットを全て使う。
    // 選んでいる場合は、その GUID を ScanProperties が拾う。
    void AddDefaultMaterialSets(const nlohmann::json& modelProperties, const std::string& owner) {
        if (!modelProperties.is_object() || modelProperties.contains("materialSetAssetGuid")) {
            return;
        }
        const auto model = modelProperties.find("assetId");
        if (model == modelProperties.end() || !model->is_string()) {
            return;
        }
        const std::string& modelGuid = model->get_ref<const std::string&>();
        if (modelGuid.empty()) {
            return;
        }
        for (const AssetRecord* record : assetDatabase_.CollectByType(AssetType::ModelMaterialBinding)) {
            ASSETS::MODELS::ModelMaterialBindingAssetData data{};
            std::string error{};
            // 読めない素材セットは、どのモデル用か分からない。このシーンが選んだ資産でもないので記録せず飛ばす。
            if (record != nullptr && record->guid.IsValid() &&
                ASSETS::MODELS::LoadModelMaterialBindingAsset(assetDatabase_.ResolveSourcePath(*record), data, error) &&
                data.modelAssetGuid.value == modelGuid) {
                AddReference(record->guid.value, owner, "Material Set");
            }
        }
    }

private:
    // 資産の中身を読み、そこから参照される資産を数える。読めなければ unreadableReferences に残す。
    void ExpandAsset(const AssetRecord& record, const std::string& owner) {
        const std::filesystem::path path = assetDatabase_.ResolveSourcePath(record);
        std::string error{};
        const auto addAll = [&](const std::vector<AssetDependencyDesc>& dependencies) {
            for (const AssetDependencyDesc& dependency : dependencies) {
                AddReference(dependency.guid.value, owner, dependency.role);
            }
        };
        switch (record.type) {
        case AssetType::TextStyle: {
            TextStyleAsset style;
            if (!LoadTextStyleAsset(path, style, error)) {
                break;
            }
            AddReference(style.font.value, owner, "TextStyle.Font");
            return;
        }
        case AssetType::PostProfile: {
            POST::PostProfileAsset profile;
            if (!POST::LoadPostProfileAsset(path, profile, error)) {
                break;
            }
            addAll(POST::CollectPostProfileDependencies(profile));
            return;
        }
        case AssetType::PostEffect: {
            POST::PostEffectDefinition effect;
            if (!POST::LoadPostEffectDefinition(path, effect, error)) {
                break;
            }
            addAll(POST::CollectPostEffectDependencies(effect));
            return;
        }
        case AssetType::Environment: {
            ENVIRONMENT::EnvironmentProfileAsset environment;
            if (!ENVIRONMENT::LoadEnvironmentProfileAsset(path, environment, error)) {
                break;
            }
            addAll(ENVIRONMENT::CollectEnvironmentProfileDependencies(environment));
            return;
        }
        case AssetType::RenderStyle: {
            RENDERSTYLE::RenderStyleAsset style;
            if (!RENDERSTYLE::LoadRenderStyleAsset(path, style, error)) {
                break;
            }
            addAll(RENDERSTYLE::CollectRenderStyleDependencies(style));
            return;
        }
        case AssetType::Material: {
            MaterialAssetData data{};
            if (!MATERIAL_SERIALIZATION::LoadMaterialSourceJson(path, data, error)) {
                break;
            }
            // Material v4 の Texture / Function / Ramp / Profile / Parent を同じ依存契約で辿る。
            for (const MaterialAssetDependency& dependency : CollectMaterialAssetDependencies(data)) {
                AddReference(dependency.assetGuid.value, owner, "Material " + dependency.role);
            }
            return;
        }
        case AssetType::ModelMaterialBinding: {
            ASSETS::MODELS::ModelMaterialBindingAssetData data{};
            if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(path, data, error)) {
                break;
            }
            for (const ASSETS::MODELS::ModelMaterialBindingDependency& dependency :
                 ASSETS::MODELS::CollectModelMaterialBindingDependencies(data)) {
                AddReference(dependency.assetGuid.value, owner, "Material Set " + dependency.role);
            }
            return;
        }
        default: return;
        }
        summary_.unreadableReferences.push_back(
            AssetUnreadableReference{record.guid.value, owner, error.empty() ? "could not be read" : error});
    }

    const AssetDatabase& assetDatabase_;
    AssetUsageSummary& summary_;
    AssetRegistry engineTextures_{};
};
} // namespace

AssetUsageSummary AnalyzeAssetUsage(const SceneDocument& document, const AssetDatabase& assetDatabase,
                                    AssetUsageScope scope) {
    AssetUsageSummary summary{};
    UsageCollector usage(assetDatabase, summary);
    if (scope == AssetUsageScope::Scene) {
        std::error_code styleError;
        if (std::filesystem::is_regular_file(assetDatabase.GetProjectRoot() / DIALOGUE::kDialogueProjectStylePath,
                                             styleError)) {
            DIALOGUE::DialogueProjectStyle style;
            if (DIALOGUE::LoadDialogueProjectStyle(assetDatabase.GetProjectRoot(), style)) {
                for (const auto& guid :
                     {style.theme.speakerTextStyle, style.theme.bodyTextStyle, style.theme.choiceTextStyle})
                    usage.AddReference(guid.value, "Dialogue UI", "TextStyle");
                for (const auto& speaker : style.speakers)
                    usage.AddReference(speaker.portraitAssetGuid, "Dialogue UI", "Portrait");
            }
        }

        // エンジン内蔵の環境と天気はエンジンの内容として出力されるので、プロジェクト資産だけを数える。
        const ENVIRONMENT::SceneEnvironmentBinding& environment = document.environment;
        if (!environment.profileId.starts_with(ENVIRONMENT::kBuiltinEnvironmentPrefix)) {
            usage.AddReference(environment.profileId, "Scene Environment", "Environment");
        }
        if (!environment.weatherId.starts_with(ENVIRONMENT::kBuiltinWeatherPrefix)) {
            usage.AddReference(environment.weatherId, "Scene Environment", "Weather");
        }
        if (!environment.renderStyleId.starts_with(RENDERSTYLE::kBuiltinRenderStylePrefix)) {
            usage.AddReference(environment.renderStyleId, "Scene Environment", "RenderStyle");
        }
    }

    for (const SceneObjectData& object : document.objects) {
        const std::string owner = object.name.empty() ? ("Object " + std::to_string(object.id.value)) : object.name;

        for (const SceneComponentData& component : object.components) {
            usage.ScanProperties(component.properties, owner, component.type);
            if (component.type == "ModelComponent") {
                usage.AddDefaultMaterialSets(component.properties, owner);
            }
        }
    }

    if (scope == AssetUsageScope::Scene) {
        for (const auto& sequence : document.cinematics.sequences) {
            for (const auto& cue : sequence.presentationCueTrack.clips)
                if (cue.kind == SEQUENCER::PresentationCueKind::Vfx)
                    usage.AddReference(cue.vfxAsset.value, sequence.name, "Sequence.VfxCue");
            for (const auto& text : sequence.textTrack.clips)
                if (text.style.IsValid())
                    usage.AddReference(text.style.value, sequence.name, "Sequence.TextStyle");
        }
    }
    return summary;
}

} // namespace HIKARI
