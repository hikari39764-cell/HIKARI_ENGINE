#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialAssetOperations.h"

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

std::filesystem::path ResolveMaterialSourcePath(
    const AssetDatabase& database,
    const AssetRecord& record) {
  return record.sourcePath.is_absolute()
      ? record.sourcePath.lexically_normal()
      : (database.GetProjectRoot() / record.sourcePath).lexically_normal();
}

bool TryLoadMaterialIdentity(
    const AssetDatabase& database,
    const AssetRecord& record,
    MaterialAssetData& outSource) {
  std::string error{};
  return LoadMaterialAssetData(
      ResolveMaterialSourcePath(database, record), outSource, error);
}

EditorOperationResult PendingMaterialConfirmation() {
  EditorOperationResult result{};
  result.code = "PENDING_CONFIRMATION";
  result.message =
      "当前材质文档有未保存修改；创建操作尚未执行。";
  result.hints = {
      "先调用 material.save 保存，再重试本操作。",
  };
  return result;
}

} // namespace

void RegisterModelMaterialAssetOperations(
    EditorOperationRegistry& registry,
    ModelMaterialWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider) {
  registry.Register({"material.templates.query", "List available material templates with their purpose. 材质模板 / マテリアルテンプレート.",
      "model_material", {}, [](const nlohmann::json&) {
        auto templates = nlohmann::json::array();
        for (const auto& info : ASSETS::MATERIAL::GetBuiltInMaterialTemplates())
          templates.push_back({{"id",ToString(info.id)},{"name",std::string(info.displayName)},
              {"description",std::string(info.description)}});
        return EditorOperationResult{true,"OK","Material templates",{{"templates",std::move(templates)}}};
      }}, EditorOperationKind::Query, false);
  auto assetQuery = EditorOperationDefinition{
      "material.list_assets",
      "List Material assets with domain, template, guid, name and source path.",
      "model_material",
      {
          EditorOperationParameter{
              "keyword", EditorOperationParameterType::String,
              false, "可选名称/路径关键字过滤"},
          EditorOperationParameter{
              "domain", EditorOperationParameterType::String,
              false, "可选领域过滤：Surface 或 Particle"},
      },
      [sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }

        const std::string keyword =
            parameters.value("keyword", std::string{});
        const std::string domainText =
            parameters.value("domain", std::string{});
        MaterialDomain domain = MaterialDomain::Surface;
        const bool filterDomain = !domainText.empty();
        if (filterDomain && !TryParseMaterialDomain(domainText, domain)) {
          result.code = "INVALID_DOMAIN";
          result.message = "domain 必须是 Surface 或 Particle。";
          return result;
        }

        result.data["materials"] = nlohmann::json::array();
        const AssetDatabase& database = scene->GetAssetDatabase();
        for (const AssetRecord* record :
             database.CollectByType(AssetType::Material)) {
          if (record == nullptr || !record->guid.IsValid()) {
            continue;
          }
          const std::string name = GetAssetRecordDisplayName(*record);
          const std::string path = record->sourcePath.generic_string();
          if (!keyword.empty() &&
              name.find(keyword) == std::string::npos &&
              path.find(keyword) == std::string::npos) {
            continue;
          }

          MaterialAssetData source{};
          if (!TryLoadMaterialIdentity(database, *record, source) ||
              (filterDomain && source.domain != domain)) {
            continue;
          }
          nlohmann::json material = {
              {"domain", std::string(ToString(source.domain))},
              {"templateId", std::string(ToString(source.templateId))},
              {"guid", record->guid.value},
              {"name", name},
              {"path", path},
          };
          result.data["materials"].push_back(material);
          result.details.push_back(material.dump());
        }
        if (result.data["materials"].empty()) {
          result.code = "NO_MATERIAL_ASSETS";
          result.message = "没有匹配的 Material 资产。";
          return result;
        }
        result.ok = true;
        result.code = "OK";
        result.message =
            "已返回 " +
            std::to_string(result.data["materials"].size()) +
            " 个 Material 资产。";
        return result;
      },
  };
    assetQuery.requiresDocumentSession = false;
    assetQuery.kind = EditorOperationKind::Query;
    registry.Register(std::move(assetQuery));

  registry.Register({
      "material.create_from_template",
      "Create a Material asset from a built-in template and open it in the background workspace.",
      "model_material",
      {
          EditorOperationParameter{
              "templateId", EditorOperationParameterType::String,
              true, "内置模板 ID，例如 ParticleUnlit"},
          EditorOperationParameter{
              "name", EditorOperationParameterType::String,
              false, "可选材质资产名称"},
      },
      [&controller, sceneProvider](const nlohmann::json& parameters) {
        EditorOperationResult result{};
        DocumentSceneBase* scene = nullptr;
        if (!RequireScene(sceneProvider, result, scene)) {
          return result;
        }
        if (controller.HasUnsavedMaterialChanges()) {
          return PendingMaterialConfirmation();
        }

        const std::string templateText =
            parameters.value("templateId", std::string{});
        MaterialTemplateId templateId = MaterialTemplateId::StandardPbr;
        if (!TryParseMaterialTemplateId(templateText, templateId) ||
            !ASSETS::MATERIAL::IsBuiltInMaterialTemplate(templateId)) {
          result.code = "INVALID_TEMPLATE";
          result.message = "templateId 不是可创建的内置材质模板。";
          result.hints = {
              "调用 list_material_templates 查询可用模板。",
          };
          return result;
        }

        const std::string displayName =
            parameters.value("name", std::string{});
        ASSETS::MATERIAL::MaterialAssetAuthoringService service(
            scene->GetAssetDatabase());
        const ASSETS::MATERIAL::MaterialAuthoringResult created =
            service.Create(templateId, displayName);
        if (!created.success) {
          result.code = "CREATE_FAILED";
          result.message = created.message;
          return result;
        }
        if (!scene->ReloadAssets()) {
          result.code = "ASSET_RELOAD_FAILED";
          result.message =
              "材质资产已创建，但项目资产刷新失败。";
          result.data["guid"] = created.assetGuid.value;
          result.data["materialGuid"] = created.assetGuid.value;
          result.data["path"] = created.sourcePath.generic_string();
          return result;
        }

        std::string openMessage{};
        if (!controller.OpenMaterialAsset(
                *scene, created.assetGuid, openMessage)) {
          result.code = "OPEN_FAILED";
          result.message =
              "材质资产已创建，但后台材质文档打开失败：" +
              openMessage;
          result.data["guid"] = created.assetGuid.value;
          result.data["materialGuid"] = created.assetGuid.value;
          result.data["path"] = created.sourcePath.generic_string();
          return result;
        }

        result.ok = true;
        result.code = "OK";
        result.message = "材质资产已创建并在后台材质工作区打开。";
        result.data = {
            {"guid", created.assetGuid.value},
            {"materialGuid", created.assetGuid.value},
            {"domain", std::string(ToString(created.compile.program.domain))},
            {"templateId", std::string(ToString(templateId))},
            {"workspace", "model_material"},
            {"path", created.sourcePath.generic_string()},
        };
        return result;
      },
  }, HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
