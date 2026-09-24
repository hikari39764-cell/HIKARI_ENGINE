#include "Editor/App/HIKARI_EditorEngineApp.h"

#include <string>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool EditorEngineApp::Initialize() {
    documentSceneEditorController_.SetProjectWorkspaceHost(&gameProjectWorkspaceHost_);
    if (!runtimeApp_.Initialize()) {
        return false;
    }
    if (HasRuntimeCommandLineFlag(L"--play-on-start")) {
        if (HasRuntimeCommandLineFlag(L"--headless-render"))
            editorPlaySession_.RequestEmbeddedStart();
        else
            editorPlaySession_.RequestWindowedStart();
    }

    std::string moduleMessage{};
    if (!gameProjectEditorModule_.LoadForLaunch(moduleMessage)) {
        HIKARI_LOG_ERROR("[GameProjectEditor] " + moduleMessage);
        gameProjectWorkspaceHost_.ReportModuleFailure(std::move(moduleMessage));
        gameProjectEditorModule_.Reset();
        return true;
    }
    if (!gameProjectEditorModule_.IsLoaded()) {
        return true;
    }

    const std::string projectRootUtf8 = TEXT::PathToUtf8(GetProjectContext().GetRoot());
    if (projectRootUtf8.empty()) {
        moduleMessage = "Project root is unavailable for the requested Editor "
                        "companion.";
        HIKARI_LOG_ERROR("[GameProjectEditor] " + moduleMessage);
        gameProjectWorkspaceHost_.ReportModuleFailure(std::move(moduleMessage));
        gameProjectEditorModule_.Reset();
        return true;
    }
    if (!gameProjectWorkspaceHost_.RegisterModule(gameProjectEditorModule_, projectRootUtf8, moduleMessage)) {
        HIKARI_LOG_ERROR("[GameProjectEditor] " + moduleMessage);
        gameProjectWorkspaceHost_.ReportModuleFailure(std::move(moduleMessage));
        gameProjectEditorModule_.Reset();
        return true;
    }
    HIKARI_LOG_INFO("[GameProjectEditor] composed module=" + std::string(gameProjectEditorModule_.GetModuleId()) +
                    " workspaces=" + std::to_string(gameProjectEditorModule_.GetWorkspaceCount()));
    if (const auto workspace = ReadRuntimeCommandLinePath(L"--open-workspace"))
        gameProjectWorkspaceHost_.RequestWorkspace(TEXT::PathToUtf8(*workspace));
    return true;
}

void EditorEngineApp::Update(float deltaTime) { runtimeApp_.Update(deltaTime); }

void EditorEngineApp::Render() { runtimeApp_.Render(); }

void EditorEngineApp::RenderHostUi() {
    runtimeApp_.RenderHostUi();
    if (!SERVICES::IsEditorHost() || !SERVICES::IsEditorUIEnabled()) {
        return;
    }

    if (auto* documentScene = dynamic_cast<DocumentSceneBase*>(runtimeApp_.GetCurrentScene())) {
        documentSceneEditorController_.Draw(*documentScene, editorPlaySession_, projectSession_);
    }
}

void EditorEngineApp::UpdateHostSessions() {
    if (auto* documentScene = dynamic_cast<DocumentSceneBase*>(runtimeApp_.GetCurrentScene())) {
        if (SERVICES::ConsumeRuntimeSessionEndRequest()) {
            editorPlaySession_.RequestStop(PlayStopReason::WindowClose);
        }
        editorPlaySession_.Update(*documentScene);
    }
}

void EditorEngineApp::RequestHostStop() { editorPlaySession_.RequestStop(PlayStopReason::Escape); }

bool EditorEngineApp::IsWindowedHostSessionRunning() const { return editorPlaySession_.IsWindowedRunning(); }

bool EditorEngineApp::IsHostTransitioning() const { return editorPlaySession_.IsTransitioning(); }

bool EditorEngineApp::ShouldExitHost() const {
    return projectSession_.ShouldExitHost() || runtimeApp_.ShouldExitHost();
}

void EditorEngineApp::Shutdown() {
    gameProjectWorkspaceHost_.Shutdown();
    gameProjectEditorModule_.Reset();
    editorPlaySession_.Shutdown(dynamic_cast<DocumentSceneBase*>(runtimeApp_.GetCurrentScene()));
    runtimeApp_.Shutdown();
}

} // namespace HIKARI::EDITOR
