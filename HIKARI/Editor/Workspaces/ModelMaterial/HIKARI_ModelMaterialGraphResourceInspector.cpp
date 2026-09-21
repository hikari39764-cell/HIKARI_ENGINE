#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::
    DrawMaterialGraphResponseResourceInspector(
        DocumentSceneBase &scene, MaterialAssetData &source,
        MaterialGraphData &graph, MaterialGraphNodeData &node) {
#if defined(HIKARI_WITH_EDITOR)
  (void)scene;
  (void)source;
  (void)graph;
  (void)node;
  ImGui::SeparatorText(TEXT::Localize("editor.ui.node.owned.authoring.b47942").c_str());
  ImGui::TextWrapped(TEXT::Localize("editor.ui.resources.and.values.are.edited.on.the.selected.node.card.add.an.439f92").c_str());
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)node;
#endif
}

} // namespace HIKARI::EDITOR
