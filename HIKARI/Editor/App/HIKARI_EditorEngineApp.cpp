#include "Editor/App/HIKARI_EditorEngineApp.h"

#include <string>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
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
    } else if (gameProjectEditorModule_.IsLoaded() &&
               !RegisterEditorModule(gameProjectEditorModule_, {}, moduleMessage)) {
        HIKARI_LOG_ERROR("[GameProjectEditor] " + moduleMessage);
        gameProjectWorkspaceHost_.ReportModuleFailure(std::move(moduleMessage));
        gameProjectEditorModule_.Reset();
    }
    for (InstalledModuleStatus& status : runtimeApp_.GetInstalledModules()) {
        if (!status.runtimeLoaded || status.manifest.editorLibrary.empty()) {
            continue;
        }
        auto library = std::make_unique<GameProjectEditorModuleLibrary>();
        std::string message{};
        if (!library->Load(status.manifest.directory / status.manifest.editorLibrary, message) ||
            !RegisterEditorModule(*library, status.manifest.name, message)) {
            status.error = message;
            HIKARI_LOG_ERROR("[ProjectModules] " + status.manifest.id + " editor: " + message);
            gameProjectWorkspaceHost_.ReportModuleFailure(std::move(message));
            continue;
        }
        status.editorLoaded = true;
        installedEditorModules_.push_back(std::move(library));
    }
    if (const auto workspace = ReadRuntimeCommandLinePath(L"--open-workspace"))
        gameProjectWorkspaceHost_.RequestWorkspace(TEXT::PathToUtf8(*workspace));
    return true;
}

bool EditorEngineApp::RegisterEditorModule(GameProjectEditorModuleLibrary& module, std::string_view displayName,
                                           std::string& outMessage) {
    const std::string projectRootUtf8 = TEXT::PathToUtf8(GetProjectContext().GetRoot());
    if (projectRootUtf8.empty()) {
        outMessage = "Project root is unavailable for the requested Editor companion.";
        return false;
    }
    if (!gameProjectWorkspaceHost_.RegisterModule(module, displayName, projectRootUtf8,
                                                  documentSceneEditorController_.Operations(), outMessage)) {
        return false;
    }
    HIKARI_LOG_INFO("[GameProjectEditor] composed module=" + std::string(module.GetModuleId()) +
                    " workspaces=" + std::to_string(module.GetWorkspaceCount()));
    return true;
}

void EditorEngineApp::Update(float deltaTime) { runtimeApp_.Update(deltaTime); }

void EditorEngineApp::Render() { runtimeApp_.Render(); }

void EditorEngineApp::RenderHostUi() {
    if (!SERVICES::IsEditorHost() || !SERVICES::IsEditorUIEnabled()) {
        return;
    }

    if (auto* documentScene = dynamic_cast<DocumentSceneBase*>(runtimeApp_.GetCurrentScene())) {
        // 新窗口 Play の間は編集器の窓が隠れていて、ゲームの窓に重ねる開発用の画面だけを描く。
        if (SERVICES::IsInProcessGamePresentationActive())
            documentSceneEditorController_.DrawPlayWindowOverlay(*documentScene);
        else
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
    documentSceneEditorController_.UpdatePlayWindowOverlay(editorPlaySession_);
}

void EditorEngineApp::RequestHostStop() { editorPlaySession_.RequestStop(PlayStopReason::Escape); }

bool EditorEngineApp::IsWindowedHostSessionRunning() const { return editorPlaySession_.IsWindowedRunning(); }

bool EditorEngineApp::IsHostTransitioning() const { return editorPlaySession_.IsTransitioning(); }

bool EditorEngineApp::ShouldExitHost() const {
    return projectSession_.ShouldExitHost() || runtimeApp_.ShouldExitHost();
}

void EditorEngineApp::Shutdown() {
    documentSceneEditorController_.DiscardModuleUndoEntries();
    gameProjectWorkspaceHost_.Shutdown();
    while (!installedEditorModules_.empty()) {
        installedEditorModules_.pop_back();
    }
    gameProjectEditorModule_.Reset();
    editorPlaySession_.Shutdown(dynamic_cast<DocumentSceneBase*>(runtimeApp_.GetCurrentScene()));
    // 场景は runtimeApp_ が壊すので、その前に编辑器の文書の会話を閉じる（镜头演出の预览が场景を使う）。
    documentSceneEditorController_.Shutdown();
    runtimeApp_.Shutdown();
}

} // namespace HIKARI::EDITOR
