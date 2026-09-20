#pragma once
#include <functional>
namespace HIKARI { class DocumentSceneBase; }
namespace HIKARI::EDITOR {
class EditorAuthoringSessionHost;
class EditorWorkspaceHost;
void RegisterVfxAuthoringSessions(EditorAuthoringSessionHost& host,
    EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> scene);
void RegisterDocumentAuthoringSessions(EditorAuthoringSessionHost& host,
    EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> scene);
void RegisterMaterialAuthoringSessions(EditorAuthoringSessionHost& host,
    EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> scene);
}
