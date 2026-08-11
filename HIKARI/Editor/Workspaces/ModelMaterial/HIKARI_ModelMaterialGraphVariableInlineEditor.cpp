#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "Editor/Widgets/HIKARI_RuntimeBindingFieldWidget.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::
    DrawMaterialGraphVariableInlineControl(
        DocumentSceneBase &scene,
        MaterialAssetData &source,
        MaterialGraphNodeData &node) {
#if defined(HIKARI_WITH_EDITOR)
  if (node.kind != MaterialNodeKind::Variable) {
    return false;
  }
  constexpr std::array allowedTypes{
      BINDING::ValueType::Bool,
      BINDING::ValueType::Float,
      BINDING::ValueType::Float2,
      BINDING::ValueType::Float3,
      BINDING::ValueType::Float4,
      BINDING::ValueType::Color,
  };
  std::string selected = node.referenceId;
  if (DrawRuntimeVariableField(
          scene.GetRuntimeBindingStore().GetCatalog(),
          "MaterialVariable",
          allowedTypes,
          selected) &&
      !selected.empty()) {
    const BINDING::VariableDefinition* variable =
        scene.GetRuntimeBindingStore().GetCatalog().FindVariable(selected);
    if (variable != nullptr) {
      const std::optional<MaterialValueType> materialType =
          BINDING::MATERIAL_ADAPTER::ToMaterialValueType(
              variable->defaultValue.type);
      MaterialValueData fallback{};
      if (materialType.has_value() &&
          BINDING::MATERIAL_ADAPTER::TryConvertValue(
              variable->defaultValue,
              *materialType,
              fallback)) {
        MaterialAssetData before = source;
        node.referenceId = variable->id;
        node.valueType = *materialType;
        node.value = fallback;
        node.displayName = variable->name;
        CommitMaterialMutation(
            std::move(before),
            "Bind material runtime variable",
            scene);
      }
    }
  }
  return ImGui::IsItemHovered() || ImGui::IsItemActive();
#else
  (void)scene;
  (void)source;
  (void)node;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
