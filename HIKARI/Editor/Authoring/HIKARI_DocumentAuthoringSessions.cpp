#include "HIKARI_EditorAuthoringSessionHost.h"
#include "HIKARI_EditorWorkspaceSessionProviders.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineOperations.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueWorkspaceController.h"
#include "Editor/Workspaces/Dialogue/HIKARI_DialogueOperations.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingOperations.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingReferenceIndex.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionOperations.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsOperations.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/HIKARI_EditorContext.h"
#include <type_traits>

namespace HIKARI::EDITOR {
namespace {
using SceneProvider = std::function<DocumentSceneBase*()>;

// 文書型ごとの処理は既存 registrar/controller を使う。非表示の ImGui
// ウィンドウは作成せず、画面を開く要求はセッション内で保持する。
template<class Controller>
class DocumentSession final : public EditorAuthoringSession {
public:
    using OpenDocument = std::function<bool(Controller&, DocumentSceneBase&, const AssetGuid&, std::string&)>;
    using DescribeDocument = std::function<nlohmann::json(const Controller&)>;
    using Registrar = std::function<void(EditorOperationRegistry&, Controller&, EditorWorkspaceHost&, SceneProvider)>;
    using RevealDocument = std::function<EditorWorkspaceOpenRequest(const Controller&)>;

    DocumentSession(SceneProvider scene, EditorWorkspaceHost& visibleHost, Registrar registrar,
        OpenDocument open, DescribeDocument describe, RevealDocument reveal)
        : scene_(std::move(scene)), visibleHost_(visibleHost), open_(std::move(open)),
          describe_(std::move(describe)), reveal_(std::move(reveal)) {
        registrar(operations_, controller_, privateHost_, scene_);
    }
    EditorOperationRegistry& Operations() override { return operations_; }
    std::any ControllerAddress() override { return &controller_; }
    void DetachVisible() override {
        if constexpr (requires(Controller& controller, EditorWorkspaceHost& host) { controller.ClosePreviewRequest(host); })
            controller_.ClosePreviewRequest(visibleHost_);
        if constexpr (std::is_same_v<Controller,CinematicsWorkspaceController>) {
            if (auto* scene=scene_()) {
                EditorContext context{};
                EditorWorkspaceActivation activation{};
                activation.previous=EditorWorkspaceId::Cinematics;
                activation.current=EditorWorkspaceId::Scene;
                controller_.ApplyWorkspaceActivation(*scene,activation,context,visibleHost_);
            }
        }
    }
    EditorOperationResult Open(const nlohmann::json& parameters) override {
        auto* scene = scene_();
        if (!scene) return {false, "NO_PROJECT", "No project is open."};
        std::string message;
        const AssetGuid guid{parameters.value("assetGuid", std::string{})};
        const bool ok = open_(controller_, *scene, guid, message);
        privateHost_.CancelPending();
        return {ok, ok ? "OK" : "OPEN_FAILED", message};
    }
    nlohmann::json Describe() const override { return describe_(controller_); }
    void Tick(uint64_t, float) override {
        privateHost_.CancelPending();
        if constexpr (requires(Controller& controller) { controller.TickBackgroundTasks(); }) controller_.TickBackgroundTasks();
    }
    EditorOperationResult Reveal() override {
        visibleHost_.RequestOpen(reveal_(controller_));
        return {true, "OK", "Workspace requested."};
    }
private:
    SceneProvider scene_;
    EditorWorkspaceHost& visibleHost_;
    Controller controller_;
    EditorWorkspaceHost privateHost_;
    EditorOperationRegistry operations_;
    OpenDocument open_;
    DescribeDocument describe_;
    RevealDocument reveal_;
};
}

void RegisterDocumentAuthoringSessions(EditorAuthoringSessionHost& host,
    EditorWorkspaceHost& workspaceHost, SceneProvider scene) {
    host.RegisterWorkspace({"animation_state_machine", "Animation State Machine", [&workspaceHost, scene] {
        using C = AnimationStateMachineWorkspaceController;
        return std::make_unique<DocumentSession<C>>(scene, workspaceHost,
            [](auto& registry, C& controller, auto&, auto provider) {
                RegisterAnimationStateMachineOperations(registry, controller, provider);
                controller.SetOperationRegistry(&registry);
            },
            [](C& controller, auto& owner, const AssetGuid& guid, auto& message) {
                return guid.IsValid() ? controller.OpenAsset(owner.GetAssetDatabase(), guid, message) : controller.NewAsset(message);
            },
            [](const C& controller) { return nlohmann::json{{"assetGuid",controller.GetDocumentAssetGuid().value},
                {"dirty",controller.IsDocumentDirty()}, {"preview",false}}; },
            [](const C& controller) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::AnimationStateMachine;
                return request; });
    }});
    host.RegisterWorkspace({"dialogue", "Dialogue", [&workspaceHost, scene] {
        using C = DialogueWorkspaceController;
        return std::make_unique<DocumentSession<C>>(scene, workspaceHost,
            [](auto& registry, C& controller, auto& privateHost, auto provider) {
                RegisterDialogueOperations(registry, controller, privateHost, provider);
            },
            [](C& controller, auto& owner, const AssetGuid& guid, auto& message) {
                return guid.IsValid() ? controller.RequestOpenAsset(owner.GetAssetDatabase(), guid, message) : controller.RequestNewAsset(message);
            },
            [](const C& controller) { return nlohmann::json{{"assetGuid",controller.GetDocumentAssetGuid().value},
                {"dirty",controller.IsDocumentDirty()}, {"preview",false}}; },
            [](const C& controller) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::Dialogue;
                return request; });
    }});
    host.RegisterWorkspace({"runtime_binding", "Runtime Bindings", [&workspaceHost, scene] {
        using C = RuntimeBindingWorkspaceController;
        return std::make_unique<DocumentSession<C>>(scene, workspaceHost,
            [](auto& registry, C& controller, auto&, auto provider) {
                RegisterRuntimeBindingOperations(registry, controller, provider);
                controller.SetReferenceProvider([provider](std::string_view id) {
                    auto* scene=provider();
                    return scene ? CollectRuntimeBindingReferences(*scene,id) : std::vector<RuntimeBindingReference>{};
                });
            },
            [](C& controller, auto& owner, const AssetGuid&, auto& message) { return controller.Activate(owner, message); },
            [](const C& controller) { return nlohmann::json{{"dirty",controller.IsDocumentDirty()}, {"preview",false}}; },
            [](const C&) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::RuntimeBinding; return request; });
    }});
    host.RegisterWorkspace({"model_collision", "Model Collision", [&workspaceHost, scene] {
        using C = ModelCollisionWorkspaceController;
        return std::make_unique<DocumentSession<C>>(scene, workspaceHost,
            [](auto& registry, C& controller, auto&, auto provider) { RegisterModelCollisionOperations(registry, controller, provider); },
            [](C& controller, auto& owner, const AssetGuid& guid, auto& message) {
                if (!guid.IsValid()) { message="A model assetGuid is required."; return false; }
                return controller.OpenModel(owner.GetAssetDatabase(), guid, message);
            },
            [](const C& controller) { return nlohmann::json{{"assetGuid",controller.GetDocumentAssetGuid().value},
                {"dirty",controller.IsDocumentDirty()}, {"preview",false}}; },
            [](const C& controller) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::ModelCollision;
                return request; });
    }});
    host.RegisterWorkspace({"cinematics", "Cinematics", [&workspaceHost, scene] {
        using C = CinematicsWorkspaceController;
        return std::make_unique<DocumentSession<C>>(scene, workspaceHost,
            [](auto& registry, C& controller, auto&, auto provider) { RegisterCinematicsOperations(registry, controller, provider); },
            [](C& controller, auto& owner, const AssetGuid& guid, auto& message) {
                return guid.IsValid() ? controller.RequestOpenSequenceAsset(owner,guid,message) : controller.RequestNewSequenceAsset(owner,message);
            },
            [](const C& controller) { return nlohmann::json{{"assetGuid",controller.GetSequenceDocument().GetAssetGuid().value},
                {"dirty",controller.IsSequenceDocumentDirty()}, {"preview",false}}; },
            [](const C& controller) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::Cinematics;
                return request; });
    }});
}
}
