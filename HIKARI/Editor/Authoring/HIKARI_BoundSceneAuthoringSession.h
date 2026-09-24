#pragma once
#include "HIKARI_EditorAuthoringSessionHost.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
namespace HIKARI {
class DocumentSceneBase;
}
namespace HIKARI::EDITOR {
class EditorWorkspaceHost;
struct BoundSceneAuthoringContext {
    std::function<DocumentSceneBase*()> scene;
    std::function<uint64_t()> editRevision;
    std::function<EditorOperationResult(const EditorOperationDefinition&, const nlohmann::json&)> execute;
};
// Scene/Terrain の権威ある文書は一つ。版検査と UI 状態の分離で共有する。
// 独立資産の provider と同じセッション API を使い、別 World は生成しない。
void RegisterBoundSceneAuthoringSessions(EditorAuthoringSessionHost& host, EditorOperationRegistry& operations,
                                         EditorWorkspaceHost& workspaceHost, BoundSceneAuthoringContext context);
} // namespace HIKARI::EDITOR
