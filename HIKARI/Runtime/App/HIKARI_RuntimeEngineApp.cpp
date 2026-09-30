#include "Runtime/App/HIKARI_RuntimeEngineApp.h"

#include <memory>
#include <string>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RUNTIME {
namespace {

// Standalone startup is the sole owner and consumer of this Scene
// specialization. Keep it private instead of carrying a one-client
// Scene module and public header.
class GameDocumentScene final : public DocumentSceneBase {
public:
    explicit GameDocumentScene(std::string sceneId) : DocumentSceneBase(std::move(sceneId)) {}

    void OnEnter() override {
        DocumentSceneBase::OnEnter();
        if (SERVICES::IsStandaloneGameHost() && !BeginRuntimePlay()) {
            HIKARI_LOG_ERROR("[RuntimeLifecycle] standalone game scene failed "
                             "to enter Runtime Play state.");
        }
    }

    const char* GetSceneName() const override { return GetSceneDocument().sceneName.c_str(); }
};

} // namespace

RuntimeEngineApp::RuntimeEngineApp()
    : gameProjectSessionExtension_(gameProjectModule_), sceneTransitionBus_(sceneManager_) {}

bool RuntimeEngineApp::Initialize() {
    HIKARI_LOG_INFO("RuntimeEngineApp initialization started.");
    standaloneRuntimeEntryFailed_ = false;

    // 起動 Scene は Scene Asset GUID から DocumentSceneBase が解決する。
    auto initialScene = std::make_unique<GameDocumentScene>("StartupSceneAsset");
    std::string moduleMessage{};
    const bool allowMissingProjectModule =
        SERVICES::IsEditorHost() && HasRuntimeCommandLineFlag(L"--allow-missing-project-module");
    if (!gameProjectModule_.LoadForLaunch(allowMissingProjectModule, moduleMessage)) {
        HIKARI_LOG_ERROR("[GameProject] " + moduleMessage);
        return false;
    }

    if (!initialScene->AddBorrowedRuntimeExtension(gameProjectSessionExtension_)) {
        HIKARI_LOG_ERROR("[GameProjectSession] failed to add the runtime session owner extension.");
        return false;
    }

    if (gameProjectModule_.IsLoaded()) {
        const std::string moduleId(gameProjectModule_.GetModuleId());
        const size_t extensionCount = gameProjectModule_.GetRuntimeExtensionCount();
        for (size_t index = 0u; index < extensionCount; ++index) {
            IRuntimeExtension* extension = gameProjectModule_.GetRuntimeExtension(index);
            if (extension == nullptr || !initialScene->AddBorrowedRuntimeExtension(*extension)) {
                HIKARI_LOG_ERROR("[GameProject] invalid or duplicate runtime extension: " + moduleId);
                return false;
            }
        }
        HIKARI_LOG_INFO("[GameProject] composed module=" + moduleId + " extensions=" + std::to_string(extensionCount));
    } else {
        HIKARI_LOG_WARN("[GameProject] " + moduleMessage);
    }

    RuntimeSceneContext::SetTransitionBus(&sceneTransitionBus_);
    sceneManager_.ChangeScene(std::move(initialScene));
    HIKARI_LOG_INFO("Initial document scene created.");
    HIKARI_LOG_INFO("RuntimeEngineApp initialization completed.");
    return true;
}

void RuntimeEngineApp::Update(float deltaTime) {
    sceneManager_.Update(deltaTime);
    if (SERVICES::IsStandaloneGameHost()) {
        const auto* gameScene = dynamic_cast<const GameDocumentScene*>(sceneManager_.GetCurrentScene());
        if (gameScene != nullptr && !gameScene->IsRuntimePlayActive()) {
            if (!standaloneRuntimeEntryFailed_) {
                HIKARI_LOG_ERROR("[RuntimeLifecycle] standalone runtime entry "
                                 "failed; the host will stop.");
            }
            standaloneRuntimeEntryFailed_ = true;
            return;
        }
    }

    // Scene transitions are host/UI flow and must keep advancing while
    // gameplay is paused by a dialogue, guide, or pause menu. The scene
    // itself still receives the scaled game delta above.
    sceneTransitionBus_.Update(TIME::GetFrameContext().unscaledDt);
    const TransitionVisualState visualState = sceneTransitionBus_.GetVisualState();
    if (visualState.active) {
        POST::PostSystem::SetTransitionState(visualState);
    } else {
        POST::PostSystem::ClearTransitionState();
    }
}

void RuntimeEngineApp::Render() {
    if (standaloneRuntimeEntryFailed_) {
        return;
    }
    sceneManager_.Render();
}

void RuntimeEngineApp::RenderHostUi() { sceneManager_.RenderImGui(); }

void RuntimeEngineApp::Shutdown() {
    RuntimeSceneContext::SetTransitionBus(nullptr);
    sceneManager_.ChangeScene(nullptr);
    sceneManager_.Update(0.0f);
    standaloneRuntimeEntryFailed_ = false;
}

bool RuntimeEngineApp::ShouldExitHost() const {
    return standaloneRuntimeEntryFailed_ ||
           (SERVICES::IsStandaloneGameHost() && SERVICES::IsRuntimeSessionEndRequested());
}

int RuntimeEngineApp::GetExitCode() const { return standaloneRuntimeEntryFailed_ ? 2 : 0; }

IScene* RuntimeEngineApp::GetCurrentScene() { return sceneManager_.GetCurrentScene(); }

const IScene* RuntimeEngineApp::GetCurrentScene() const { return sceneManager_.GetCurrentScene(); }

} // namespace HIKARI::RUNTIME
