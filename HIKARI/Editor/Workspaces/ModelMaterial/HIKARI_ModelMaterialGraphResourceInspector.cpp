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
  ImGui::SeparatorText("Node-owned authoring");
  ImGui::TextWrapped(
      "Resources and values are edited on the selected node card. Add an "
      "Image Texture node and connect its typed output when an effect needs "
      "an image; Ramp stacks are edited directly on the Ramp node.");
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)node;
#endif
}

} // namespace HIKARI::EDITOR
