#pragma once

#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"
#include "Editor/Projects/HIKARI_EditorProjectSession.h"
#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/App/HIKARI_RuntimeEngineApp.h"

namespace HIKARI::EDITOR {

class EditorEngineApp final : public RUNTIME::IHostApplication {
public:
    EditorEngineApp() = default;

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Render() override;
    void RenderHostUi() override;
    const IScene* GetCurrentScene() const override { return runtimeApp_.GetCurrentScene(); }
    void UpdateHostSessions() override;
    void RequestHostStop() override;
    bool IsWindowedHostSessionRunning() const override;
    bool IsHostTransitioning() const override;
    bool ShouldExitHost() const override;
    void Shutdown() override;

private:
    RUNTIME::RuntimeEngineApp runtimeApp_;
    DocumentSceneEditorController documentSceneEditorController_{};
    EditorPlaySession editorPlaySession_{};
    EditorProjectSessionController projectSession_{};
    GameProjectEditorModuleLibrary gameProjectEditorModule_{};
    GameProjectEditorWorkspaceHost gameProjectWorkspaceHost_{};
};

} // namespace HIKARI::EDITOR
