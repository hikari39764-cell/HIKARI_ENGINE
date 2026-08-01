#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

bool ContainsText(std::string_view value, std::string_view query) {
  if (query.empty()) {
    return true;
  }
  return std::search(value.begin(), value.end(), query.begin(), query.end(),
                     [](char left, char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                     }) != value.end();
}

bool IsRenderableKind(MaterialAssetKind kind) noexcept {
  return kind == MaterialAssetKind::Material ||
         kind == MaterialAssetKind::Instance;
}

MaterialGraphData *EditableGraph(MaterialAssetData &material) noexcept {
  if (material.kind == MaterialAssetKind::Material) {
    return &material.graph;
  }
  if (material.kind == MaterialAssetKind::Function) {
    return &material.function.graph;
  }
  return nullptr;
}

bool LooksLikeColor(const MaterialGraphParameterData &parameter) {
  std::string folded = parameter.displayName + parameter.id;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return folded.find("color") != std::string::npos ||
         folded.find("tint") != std::string::npos;
}

bool DrawParameterValue(MaterialGraphParameterData &parameter) {
  MaterialValueData &value = parameter.defaultValue;
  switch (value.type) {
  case MaterialValueType::Bool:
    return ImGui::Checkbox("##Value", &value.boolean);
  case MaterialValueType::Float1:
    return ImGui::DragFloat("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float2:
    return ImGui::DragFloat2("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float3:
    return LooksLikeColor(parameter)
               ? ImGui::ColorEdit3("##Value", &value.numeric.x)
               : ImGui::DragFloat3("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float4:
    return LooksLikeColor(parameter)
               ? ImGui::ColorEdit4("##Value", &value.numeric.x)
               : ImGui::DragFloat4("##Value", &value.numeric.x, 0.01f);
  default:
    ImGui::TextDisabled("%s", ToString(value.type).data());
    return false;
  }
}

} // namespace

void ModelMaterialWorkspaceController::DrawMaterialWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Material###ModelMaterial/Material")) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##NewMaterialName", "New material name",
                           newMaterialName_.data(), newMaterialName_.size());
  const auto &templates = ASSETS::MATERIAL::GetBuiltInMaterialTemplates();
  const auto selectedTemplate = std::find_if(
      templates.begin(), templates.end(), [this](const auto &entry) {
        return entry.id == selectedTemplate_;
      });
  const char *templateLabel = selectedTemplate != templates.end()
                                  ? selectedTemplate->displayName.data()
                                  : "Template";
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##MaterialTemplate", templateLabel)) {
    for (const auto &entry : templates) {
      if (ImGui::Selectable(entry.displayName.data(),
                            selectedTemplate_ == entry.id)) {
        selectedTemplate_ = entry.id;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", entry.description.data());
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::Button("Create Material", ImVec2(-1.0f, 0.0f))) {
    (void)CreateMaterial(scene, selectedTemplate_, false, statusMessage_);
  }
  if (ImGui::Button("Create 6-Role Character Set", ImVec2(-1.0f, 0.0f))) {
    (void)CreateCharacterMaterialSet(scene, statusMessage_);
  }

  if (ImGui::Button("New Ramp")) {
    (void)CreateAuxiliaryMaterialAsset(scene, MaterialAssetKind::Ramp,
                                       statusMessage_);
  }
  ImGui::SameLine();
  if (ImGui::Button("New Function")) {
    (void)CreateAuxiliaryMaterialAsset(scene, MaterialAssetKind::Function,
                                       statusMessage_);
  }
  ImGui::SameLine();
  if (ImGui::Button("New Profile")) {
    (void)CreateAuxiliaryMaterialAsset(scene, MaterialAssetKind::Profile,
                                       statusMessage_);
  }

  const AssetChoice *openChoice =
      FindMaterialChoice(candidateAuthoringAssetGuid_);
  const char *openLabel = openChoice != nullptr
                              ? openChoice->label.c_str()
                              : "Open authoring asset...";
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##OpenMaterialAuthoringAsset", openLabel)) {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##AuthoringAssetSearch",
                             "Filter materials, ramps, functions, profiles...",
                             materialSearch_.data(), materialSearch_.size());
    const std::string_view query(materialSearch_.data());
    for (const AssetChoice &choice : materialChoices_) {
      if (!ContainsText(choice.label, query) &&
          !ContainsText(choice.guid.value, query) &&
          !ContainsText(ToString(choice.materialKind), query)) {
        continue;
      }
      ImGui::PushID(choice.guid.value.c_str());
      const std::string label =
          choice.label + "  [" + std::string(ToString(choice.materialKind)) +
          "]" + (choice.ready ? "" : "  [import required]");
      if (ImGui::Selectable(label.c_str(),
                            choice.guid == candidateAuthoringAssetGuid_)) {
        candidateAuthoringAssetGuid_ = choice.guid;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::BeginDisabled(openChoice == nullptr);
  if (ImGui::Button("Open Selected Asset", ImVec2(-1.0f, 0.0f))) {
    (void)OpenMaterialAsset(scene, candidateAuthoringAssetGuid_,
                            statusMessage_);
  }
  ImGui::EndDisabled();

  if (!authoringDocument_.HasMaterial()) {
    ImGui::TextWrapped(
        "Create a material from a production template or open a bound asset "
        "from the Binding panel.");
    ImGui::End();
    return;
  }

  MaterialAssetData &source = authoringDocument_.EditMaterial();
  ImGui::SeparatorText("Document");
  ImGui::Text("%s%s", source.materialName.c_str(),
              authoringDocument_.IsMaterialDirty() ? " *" : "");
  ImGui::TextDisabled("%s | %s", ToString(source.kind).data(),
                      ToString(source.templateId).data());
  ImGui::BeginDisabled(!authoringDocument_.IsMaterialDirty());
  if (ImGui::Button("Save")) {
    (void)SaveMaterial(scene, false, statusMessage_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Save As...")) {
    ImGui::OpenPopup("MaterialSaveAsPopup");
  }
  if (ImGui::BeginPopup("MaterialSaveAsPopup")) {
    ImGui::TextUnformatted("Path under Assets");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("##MaterialSaveAsPath", saveAsPath_.data(),
                     saveAsPath_.size());
    if (ImGui::Button("Save Copy")) {
      if (SaveMaterial(scene, true, statusMessage_)) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if (source.kind == MaterialAssetKind::Ramp) {
    ImGui::TextWrapped(
        "Edit the reusable curve, interpolation, resolution and points in "
        "the Ramp panel. Materials reference this asset through a Ramp slot.");
    ImGui::End();
    return;
  }

  if (source.kind == MaterialAssetKind::Profile) {
    ImGui::SeparatorText("Profile");
    const auto profileTemplate = std::find_if(
        templates.begin(), templates.end(), [&source](const auto &entry) {
          return entry.id == source.profile.baseTemplate;
        });
    const char *profileLabel = profileTemplate != templates.end()
                                   ? profileTemplate->displayName.data()
                                   : "Template";
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("Base Template", profileLabel)) {
      for (const auto &entry : templates) {
        if (ImGui::Selectable(entry.displayName.data(),
                              source.profile.baseTemplate == entry.id)) {
          MaterialAssetData before = source;
          source.profile.baseTemplate = entry.id;
          CommitMaterialMutation(std::move(before),
                                 "Change profile base template", scene);
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SeparatorText("Render Features");
    for (MaterialFeatureRequest feature : {
             MaterialFeatureRequest::GeometryOutline,
             MaterialFeatureRequest::ScreenOutline,
             MaterialFeatureRequest::VisibilityOverlay,
             MaterialFeatureRequest::FaceHairShadow}) {
      const auto found = std::find(source.profile.featureRequests.begin(),
                                   source.profile.featureRequests.end(), feature);
      bool enabled = found != source.profile.featureRequests.end();
      if (ImGui::Checkbox(ToString(feature).data(), &enabled)) {
        MaterialAssetData before = source;
        if (enabled) {
          source.profile.featureRequests.push_back(feature);
        } else if (found != source.profile.featureRequests.end()) {
          source.profile.featureRequests.erase(found);
        }
        CommitMaterialMutation(std::move(before),
                               "Change profile render feature", scene);
      }
    }
    ImGui::End();
    return;
  }

  DrawMaterialFeatureControls(scene, source);

  if (IsRenderableKind(source.kind)) {
    ImGui::SeparatorText("A/B Template Preview");
  }
  if (source.kind == MaterialAssetKind::Material &&
      !authoringDocument_.IsAbPreviewActive()) {
    if (ImGui::Button("Preview Template B", ImVec2(-1.0f, 0.0f))) {
      MaterialAssetData candidate =
          ASSETS::MATERIAL::CreateMaterialFromTemplate(
              selectedTemplate_, source.materialName);
      candidate.pbr = source.pbr;
      candidate.surface = source.surface;
      if (authoringDocument_.BeginAbPreview(std::move(candidate),
                                            statusMessage_)) {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                     scene.GetAssetRegistry(), statusMessage_);
      }
    }
  } else if (source.kind == MaterialAssetKind::Material) {
    StatusBadge("B preview - not saved", EditorStatusTone::Warning);
    if (ImGui::Button("Keep B")) {
      if (authoringDocument_.KeepAbPreview(statusMessage_)) {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                     scene.GetAssetRegistry(), statusMessage_);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard B (Esc)")) {
      if (authoringDocument_.DiscardAbPreview(statusMessage_)) {
        (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                     scene.GetAssetRegistry(), statusMessage_);
      }
    }
  }

  if (source.kind == MaterialAssetKind::Instance) {
    ImGui::TextWrapped("This is a material instance. Edit inherited overrides "
                       "in the Instance panel.");
    ImGui::End();
    return;
  }

  if (source.kind == MaterialAssetKind::Material) {
    ImGui::SeparatorText("Surface");
  {
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Double-sided", &source.surface.doubleSided)) {
      CommitMaterialMutation(std::move(before), "Toggle double-sided", scene);
    }
  }
  ImGui::SameLine();
  {
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Unlit", &source.surface.unlit)) {
      CommitMaterialMutation(std::move(before), "Toggle unlit", scene);
    }
  }
  }

  MaterialGraphData *graph = EditableGraph(source);
  if (graph == nullptr) {
    ImGui::TextDisabled("This material asset kind has no editable graph.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Exposed Parameters");
  std::optional<size_t> deleteParameter{};
  for (size_t index = 0; index < graph->parameters.size(); ++index) {
    MaterialGraphParameterData &parameter = graph->parameters[index];
    if (!parameter.exposed) {
      continue;
    }
    ImGui::PushID(parameter.id.c_str());
    ImGui::TextUnformatted(parameter.displayName.empty()
                               ? parameter.id.c_str()
                               : parameter.displayName.c_str());
    MaterialAssetData before = source;
    ImGui::SetNextItemWidth(-32.0f);
    const bool changed = DrawParameterValue(parameter);
    const bool continuous =
        parameter.defaultValue.type != MaterialValueType::Bool;
    if (continuous && ImGui::IsItemActivated() && !materialGesture_) {
      materialGesture_ = MaterialEditGesture{std::move(before),
                                             "Edit material parameter"};
    }
    if (continuous && ImGui::IsItemDeactivatedAfterEdit()) {
      CommitMaterialGesture(scene);
    }
    if (!continuous && changed) {
      CommitMaterialMutation(std::move(before), "Edit material parameter",
                             scene);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      deleteParameter = index;
    }
    ImGui::PopID();
  }
  if (deleteParameter) {
    MaterialAssetData before = source;
    graph->parameters.erase(graph->parameters.begin() +
                            static_cast<std::ptrdiff_t>(*deleteParameter));
    CommitMaterialMutation(std::move(before), "Delete material parameter",
                           scene);
  }
  if (ImGui::Button("Add Exposed Parameter")) {
    MaterialAssetData before = source;
    const std::string id = "Parameter_" +
                           std::to_string(authoringIdCounter_++);
    graph->parameters.push_back(
        {id, id, MaterialValueData{}, true});
    CommitMaterialMutation(std::move(before), "Add material parameter", scene);
  }

  ImGui::SeparatorText("Resources");
  std::optional<size_t> deleteResource{};
  for (size_t index = 0; index < graph->resources.size(); ++index) {
    MaterialGraphResourceData &resource = graph->resources[index];
    ImGui::PushID(resource.id.c_str());
    ImGui::Text("%s (%s)", resource.displayName.empty()
                                ? resource.id.c_str()
                                : resource.displayName.c_str(),
                ToString(resource.type).data());
    MaterialAssetData beforeEnabled = source;
    if (ImGui::Checkbox("Enabled", &resource.enabled)) {
      CommitMaterialMutation(std::move(beforeEnabled),
                             "Toggle material resource", scene);
    }
    const AssetChoice *boundChoice = FindResourceChoice(resource.assetGuid);
    const char *resourceLabel = boundChoice != nullptr
                                    ? boundChoice->label.c_str()
                                    : resource.assetGuid.IsValid()
                                          ? "Missing resource"
                                          : "Choose resource...";
    const bool wantsRamp = resource.type == MaterialResourceType::Ramp1D ||
                           resource.type == MaterialResourceType::Ramp2D ||
                           resource.semantic == MaterialResourceSemantic::Ramp;
    ImGui::SetNextItemWidth(-32.0f);
    if (ImGui::BeginCombo("##ResourceAsset", resourceLabel)) {
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##ResourceSearch", "Filter compatible assets...",
                               resourceSearch_.data(), resourceSearch_.size());
      const std::string_view query(resourceSearch_.data());
      if (ImGui::Selectable("None", !resource.assetGuid.IsValid())) {
        MaterialAssetData before = source;
        resource.assetGuid = {};
        resource.enabled = false;
        CommitMaterialMutation(std::move(before), "Clear material resource",
                               scene);
      }
      if (wantsRamp) {
        for (const AssetChoice &choice : materialChoices_) {
          if (choice.materialKind != MaterialAssetKind::Ramp ||
              (!ContainsText(choice.label, query) &&
               !ContainsText(choice.guid.value, query))) {
            continue;
          }
          if (ImGui::Selectable(choice.label.c_str(),
                                choice.guid == resource.assetGuid)) {
            MaterialAssetData before = source;
            resource.assetGuid = choice.guid;
            resource.enabled = true;
            CommitMaterialMutation(std::move(before), "Bind Ramp resource",
                                   scene);
          }
        }
      } else {
        for (const AssetChoice &choice : textureChoices_) {
          if (!ContainsText(choice.label, query) &&
              !ContainsText(choice.guid.value, query)) {
            continue;
          }
          if (ImGui::Selectable(choice.label.c_str(),
                                choice.guid == resource.assetGuid)) {
            MaterialAssetData before = source;
            resource.assetGuid = choice.guid;
            resource.enabled = true;
            CommitMaterialMutation(std::move(before), "Bind texture resource",
                                   scene);
          }
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      deleteResource = index;
    }
    ImGui::PopID();
  }
  if (deleteResource) {
    MaterialAssetData before = source;
    graph->resources.erase(graph->resources.begin() +
                           static_cast<std::ptrdiff_t>(*deleteResource));
    CommitMaterialMutation(std::move(before), "Delete material resource",
                           scene);
  }
  if (ImGui::Button("Add Texture Resource")) {
    MaterialAssetData before = source;
    const std::string id = "Texture_" +
                           std::to_string(authoringIdCounter_++);
    MaterialGraphResourceData resource{};
    resource.id = id;
    resource.displayName = id;
    graph->resources.push_back(std::move(resource));
    CommitMaterialMutation(std::move(before), "Add material resource", scene);
  }
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
