#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cstdio>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

MaterialValueData EffectiveParameterValue(
    const MaterialAssetData &instance,
    const MaterialGraphParameterData &parameter) {
  const auto found = std::find_if(
      instance.instance.parameterOverrides.begin(),
      instance.instance.parameterOverrides.end(),
      [&parameter](const auto &entry) {
        return entry.parameterId == parameter.id;
      });
  return found != instance.instance.parameterOverrides.end()
             ? found->value
             : parameter.defaultValue;
}

AssetGuid EffectiveResourceGuid(const MaterialAssetData &instance,
                                const MaterialGraphResourceData &resource) {
  const auto found = std::find_if(
      instance.instance.resourceOverrides.begin(),
      instance.instance.resourceOverrides.end(),
      [&resource](const auto &entry) {
        return entry.resourceId == resource.id;
      });
  return found != instance.instance.resourceOverrides.end()
             ? found->assetGuid
             : resource.assetGuid;
}

bool DrawValue(MaterialValueData &value) {
  switch (value.type) {
  case MaterialValueType::Bool:
    return ImGui::Checkbox("##InstanceValue", &value.boolean);
  case MaterialValueType::Float1:
    return ImGui::DragFloat("##InstanceValue", &value.numeric.x, 0.01f);
  case MaterialValueType::Float2:
    return ImGui::DragFloat2("##InstanceValue", &value.numeric.x, 0.01f);
  case MaterialValueType::Float3:
    return ImGui::DragFloat3("##InstanceValue", &value.numeric.x, 0.01f);
  case MaterialValueType::Float4:
    return ImGui::ColorEdit4("##InstanceValue", &value.numeric.x);
  default:
    ImGui::TextDisabled("Unsupported inherited type");
    return false;
  }
}

} // namespace

void ModelMaterialWorkspaceController::DrawInstanceWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Instance###ModelMaterial/Instance")) {
    ImGui::End();
    return;
  }
  if (!authoringDocument_.HasMaterial()) {
    ImGui::TextWrapped("Open a material before creating an instance.");
    ImGui::End();
    return;
  }

  MaterialAssetData &instance = authoringDocument_.EditMaterial();
  if (instance.kind != MaterialAssetKind::Instance) {
    ImGui::TextWrapped(
        "Create a lightweight instance that inherits this material and only "
        "stores selected parameter or resource overrides.");
    if (ImGui::Button("Create Material Instance", ImVec2(-1.0f, 0.0f))) {
      (void)CreateMaterial(scene, instance.templateId, true, statusMessage_);
    }
    ImGui::End();
    return;
  }

  ImGui::Text("Parent: %s", instance.instance.parentMaterialGuid.value.c_str());
  AssetDatabase &database = scene.GetAssetDatabase();
  const uint64_t databaseRevision = database.GetContentRevision();
  if (!instanceParentCache_ ||
      instanceParentCacheGuid_ != instance.instance.parentMaterialGuid ||
      instanceParentCacheRevision_ != databaseRevision) {
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
    ASSETS::MATERIAL::MaterialAuthoringDocument parent{};
    std::string loadMessage{};
    if (service.Load(instance.instance.parentMaterialGuid, parent,
                     loadMessage)) {
      instanceParentCache_ = std::move(parent.source);
      instanceParentCacheGuid_ = instance.instance.parentMaterialGuid;
      instanceParentCacheRevision_ = databaseRevision;
    } else {
      instanceParentCache_.reset();
      statusMessage_ = std::move(loadMessage);
    }
  }
  if (!instanceParentCache_) {
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                       "Parent material is unavailable.");
    ImGui::End();
    return;
  }

  const MaterialAssetData &parent = *instanceParentCache_;
  ImGui::SeparatorText("Parameter Overrides");
  for (const MaterialGraphParameterData &parameter : parent.graph.parameters) {
    if (!parameter.exposed) {
      continue;
    }
    ImGui::PushID(parameter.id.c_str());
    ImGui::TextUnformatted(parameter.displayName.empty()
                               ? parameter.id.c_str()
                               : parameter.displayName.c_str());
    MaterialValueData value = EffectiveParameterValue(instance, parameter);
    MaterialAssetData before = instance;
    const bool changed = DrawValue(value);
    const bool isBoolean = value.type == MaterialValueType::Bool;
    if (!isBoolean && ImGui::IsItemActivated() && !materialGesture_) {
      materialGesture_ = MaterialEditGesture{before,
                                             "Edit instance parameter"};
    }
    if (changed) {
      std::string error{};
      if (!ASSETS::MATERIAL::SetMaterialInstanceParameterOverride(
              instance, parent, parameter.id, value, error)) {
        statusMessage_ = std::move(error);
      } else if (isBoolean) {
        CommitMaterialMutation(std::move(before), "Edit instance parameter",
                               scene);
      }
    }
    if (!isBoolean && ImGui::IsItemDeactivatedAfterEdit()) {
      CommitMaterialGesture(scene);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
      MaterialAssetData resetBefore = instance;
      if (ASSETS::MATERIAL::RestoreMaterialInstanceParameterDefault(
              instance, parameter.id)) {
        CommitMaterialMutation(std::move(resetBefore),
                               "Reset instance parameter", scene);
      }
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText("Resource Overrides");
  for (const MaterialGraphResourceData &resource : parent.graph.resources) {
    ImGui::PushID(resource.id.c_str());
    ImGui::TextUnformatted(resource.displayName.empty()
                               ? resource.id.c_str()
                               : resource.displayName.c_str());
    const AssetGuid effective = EffectiveResourceGuid(instance, resource);
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%s", effective.value.c_str());
    ImGui::SetNextItemWidth(-64.0f);
    if (ImGui::InputTextWithHint("##InstanceResource", "Asset GUID", buffer,
                                 sizeof(buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
      MaterialAssetData before = instance;
      std::string error{};
      if (ASSETS::MATERIAL::SetMaterialInstanceResourceOverride(
              instance, parent, resource.id, AssetGuid{buffer}, error)) {
        CommitMaterialMutation(std::move(before), "Bind instance resource",
                               scene);
      } else {
        statusMessage_ = std::move(error);
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
      MaterialAssetData before = instance;
      if (ASSETS::MATERIAL::RestoreMaterialInstanceResourceDefault(
              instance, resource.id)) {
        CommitMaterialMutation(std::move(before), "Reset instance resource",
                               scene);
      }
    }
    ImGui::PopID();
  }

  ImGui::Separator();
  ImGui::BeginDisabled(instance.instance.parameterOverrides.empty() &&
                       instance.instance.resourceOverrides.empty());
  if (ImGui::Button("Reset All Instance Overrides", ImVec2(-1.0f, 0.0f))) {
    MaterialAssetData before = instance;
    instance.instance.parameterOverrides.clear();
    instance.instance.resourceOverrides.clear();
    CommitMaterialMutation(std::move(before), "Reset instance overrides",
                           scene);
  }
  ImGui::EndDisabled();
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
