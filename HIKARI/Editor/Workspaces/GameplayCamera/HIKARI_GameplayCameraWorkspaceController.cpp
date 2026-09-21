#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>

namespace HIKARI::EDITOR {
nlohmann::json GameplayCameraWorkspaceController::Describe() const {
    return {{"preview", true},
            {"previewParameters", {"cameraId", "targetId", "lookTargetId", "mode", "width", "height"}},
            {"open", document_.IsOpen()},
            {"dirty", document_.IsDirty()},
            {"assetGuid", document_.Guid().value},
            {"name", document_.Profile().name},
            {"revision", document_.Revision()},
            {"canUndo", document_.CanUndo()},
            {"canRedo", document_.CanRedo()},
            {"profile", CAMERA::WriteGameplayCameraProfile(document_.Profile())}};
}
bool GameplayCameraWorkspaceController::Edit(const nlohmann::json &command, std::string &message) {
    try {
        auto root = CAMERA::WriteGameplayCameraProfile(document_.Profile());
        const std::string action = command.at("action").get<std::string>();
        if (action == "replace")
            root = command.at("profile");
        else if (action == "rename")
            root["name"] = command.at("name");
        else if (action == "set_default")
            root["defaultMode"] = command.at("id");
        else if (action == "add_mode")
            root["modes"].push_back(command.at("mode"));
        else if (action == "add_effect") root["effects"].push_back(command.at("effect"));
        else if (action == "edit_effect" || action == "remove_effect") {
            auto& effects=root["effects"];
            const auto it=std::find_if(effects.begin(),effects.end(),[&](const auto& e){return e.at("id")==command.at("id");});
            if(it==effects.end()) {message="Effect not found.";return false;}
            if(action=="remove_effect") effects.erase(it);
            else it->merge_patch(command.at("patch"));
        }
        else if (action == "edit_mode" || action == "remove_mode" || action == "duplicate_mode") {
            auto &modes = root["modes"];
            const auto id = command.at("id").get<std::string>();
            const auto it =
                std::find_if(modes.begin(), modes.end(), [&](const auto &mode) { return mode.at("id") == id; });
            if (it == modes.end()) {
                message = "Mode not found: " + id;
                return false;
            }
            if (action == "edit_mode") {
                auto patch = command.at("patch");
                patch.erase("id");
                it->merge_patch(patch);
            } else if (action == "remove_mode")
                modes.erase(it);
            else {
                auto duplicate = *it;
                duplicate["id"] = command.at("newId");
                duplicate["name"] = command.value("name", command.at("newId"));
                modes.push_back(std::move(duplicate));
            }
        } else {
            message = "Unknown camera edit action.";
            return false;
        }
        CAMERA::GameplayCameraProfile profile;
        if (!CAMERA::ReadGameplayCameraProfile(root, profile, message))
            return false;
        return document_.Apply(std::move(profile), message);
    } catch (const std::exception &error) {
        message = error.what();
        return false;
    }
}
void GameplayCameraWorkspaceController::BindDocumentCommands(EditorCommandRouter &router) {
    router.BindOperation(EditorCommandId::SaveDocument, TEXT::Localize("menu.edit.save"), document_.IsOpen(),
                         "gameplay_camera.save");
    router.BindOperation(EditorCommandId::Undo, TEXT::Localize("menu.edit.undo"), document_.CanUndo(),
                         "gameplay_camera.undo");
    router.BindOperation(EditorCommandId::Redo, TEXT::Localize("menu.edit.redo"), document_.CanRedo(),
                         "gameplay_camera.redo");
}
void GameplayCameraWorkspaceController::ClosePreviewRequest(EditorWorkspaceHost &host) {
    RENDER3D::EDITORVIEW::ClearRequest(host.GetGameplayCameraObserverView().renderViewId);
    RENDER3D::EDITORVIEW::ClearRequest(host.GetGameplayCameraPreviewView().renderViewId);
    observer_.LeaveWorkspace();
}
void GameplayCameraWorkspaceController::ApplyActivation(DocumentSceneBase &scene,
                                                        const EditorWorkspaceActivation &activation,
                                                        EditorWorkspaceHost &host) {
    if (activation.previous == EditorWorkspaceId::GameplayCamera && activation.current != activation.previous)
        ClosePreviewRequest(host);
    if (activation.current != EditorWorkspaceId::GameplayCamera)
        return;
    if (activation.cameraProfileAssetGuid)
        document_.Open(scene.GetAssetDatabase(), *activation.cameraProfileAssetGuid, message_);
    if (activation.targetCameraObjectId) SelectCamera(scene,*activation.targetCameraObjectId);
    focused_ = false;
}
} // namespace HIKARI::EDITOR
