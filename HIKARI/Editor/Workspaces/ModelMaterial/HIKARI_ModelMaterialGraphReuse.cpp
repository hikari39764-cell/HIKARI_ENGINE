#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialGraphReuse.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "Assets/Material/HIKARI_MaterialAssetLookup.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>

namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::InsertMaterialModule(DocumentSceneBase& scene, std::string_view id,
                                                            MATH::Vec2 position, std::string& message) {
    std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
    auto& material = authoringDocument_.EditMaterial();
    auto* graph = material.ActiveGraph();
    if (!authoringDocument_.HasMaterial() || !graph) {
        message = TEXT::Localize("material.layout.empty");
        return false;
    }
    auto fragment = ASSETS::MATERIAL::CreateMaterialGraphModule(id);
    if (fragment.nodes.empty()) {
        message = TEXT::Localize("material.module.unknown");
        return false;
    }
    const auto modules = ASSETS::MATERIAL::GetMaterialGraphModules();
    const auto info = std::find_if(modules.begin(), modules.end(), [&](const auto& item) { return item.id == id; });
    const std::string name = TEXT::Localize(info->labelKey);
    for (auto& parameter : fragment.parameters)
        parameter.group = name;
    MaterialGraphGroupData group;
    group.id = "module";
    group.name = name;
    for (const auto& node : fragment.nodes)
        group.nodeIds.push_back(node.id);
    fragment.groups.push_back(std::move(group));
    MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(fragment);
    const bool changed = PasteMaterialGraphFragment(
        material, *graph, fragment, {position.x - 40.0f, position.y - 40.0f}, true, "Insert material module");
    graphFocusRequested_ = changed;
    if (changed)
        (void)FlushMaterialPreview(scene, statusMessage_);
    message = TEXT::Localize("material.module.inserted");
    return changed;
}

bool ModelMaterialWorkspaceController::ExtractSelectedMaterialFunction(DocumentSceneBase& scene, std::string_view name,
                                                                       std::string& message) {
    std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
    if (!authoringDocument_.HasMaterial()) {
        message = TEXT::Localize("material.function.select");
        return false;
    }
    const auto before = authoringDocument_.GetMaterial();
    auto selection = selectedGraphNodeIds_;
    if (selection.empty() && !selectedGraphNodeId_.empty())
        selection.push_back(selectedGraphNodeId_);
    const auto temporaryGuid = GenerateAssetGuid();
    const std::string callId = "Node_" + std::to_string(authoringIdCounter_++);
    MaterialAssetData function, replaced;
    std::string error;
    if (!ASSETS::MATERIAL::ExtractMaterialGraphFunction(before, selection, temporaryGuid,
                                                        name.empty() ? "Material Function" : name, callId, function,
                                                        replaced, error)) {
        message = TEXT::Localize(error);
        return false;
    }
    auto options = ASSETS::MATERIAL::MakeMaterialCompileOptions(scene.GetAssetDatabase());
    const auto resolver = options.resolveAsset;
    options.resolveAsset = [resolver, &function, &temporaryGuid](const AssetGuid& guid, MaterialAssetData& asset,
                                                                 std::string& reason) {
        if (guid == temporaryGuid) {
            asset = function;
            return true;
        }
        return resolver && resolver(guid, asset, reason);
    };
    const auto compile = CompileMaterialAsset(replaced, options);
    if (!compile.success) {
        message = TEXT::Localize("material.function.invalid");
        for (const auto& diagnostic : compile.diagnostics)
            message += "\n" + diagnostic.message;
        return false;
    }
    MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(function.function.graph);
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(scene.GetAssetDatabase());
    const auto saved = service.Create(function);
    if (!saved.success) {
        message = saved.message;
        return false;
    }
    auto* graph = replaced.ActiveGraph();
    FindMaterialGraphNode(*graph, callId)->assetReference = saved.assetGuid;
    // 新しい関数を登録してから、元の文書を一つの履歴操作で置換する。
    if (!scene.ReloadAssets()) {
        message = saved.message + "; asset registry refresh failed";
        return false;
    }
    authoringDocument_.EditMaterial() = std::move(replaced);
    authoringDocument_.CommitMaterial(before, "Extract material function");
    SetGraphNodeSelection(callId, false);
    graphFocusRequested_ = true;
    message = TEXT::Localize("material.function.created") + " " + saved.sourcePath.generic_string();
    return true;
}
} // namespace HIKARI::EDITOR
