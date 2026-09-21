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
#include "Editor/Authoring/HIKARI_EditorAuthoringPreview.h"
#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include <type_traits>
#include <cmath>

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
    using PreviewDocument = std::function<EditorOperationResult(const Controller&, DocumentSceneBase&,
        const nlohmann::json&, EditorAuthoringPreview&)>;

    DocumentSession(SceneProvider scene, EditorWorkspaceHost& visibleHost, Registrar registrar,
        OpenDocument open, DescribeDocument describe, RevealDocument reveal, PreviewDocument preview = {})
        : scene_(std::move(scene)), visibleHost_(visibleHost), open_(std::move(open)),
          describe_(std::move(describe)), reveal_(std::move(reveal)), preview_(std::move(preview)) {
        registrar(operations_, controller_, privateHost_, scene_);
        if (preview_) capture_ = std::make_unique<EditorAuthoringPreview>(RENDER3D::RenderViewId{AllocateAuthoringPreviewViewId()});
    }
    bool HasInteractiveEdit() const override {
        if constexpr (requires(const Controller& c) { c.HasInteractiveEdit(); }) return controller_.HasInteractiveEdit();
        else return false;
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
    EditorOperationResult Preview(const nlohmann::json& parameters) override {
        if (!preview_) return EditorAuthoringSession::Preview(parameters);
        auto* scene = scene_();
        if (!scene) return {false,"NO_SCENE","No scene is available for this preview."};
        if (capture_->IsPending()) return {false,"CAPTURE_BUSY","A preview is already pending."};
        captureScene_ = scene;
        captureRevision_ = scene->GetSceneDocumentRevision();
        auto result = preview_(controller_,*scene,parameters,*capture_);
        if (result.ok) capture_->Tick(true);
        return result;
    }
    EditorOperationResult PreviewStatus(const nlohmann::json& parameters) override {
        if (!capture_) return EditorAuthoringSession::PreviewStatus(parameters);
        const auto* scene = scene_();
        if (scene != captureScene_ || !scene || scene->GetSceneDocumentRevision() != captureRevision_) {
            capture_->Cancel();
            return {false,"DOCUMENT_CONFLICT","The scene changed while rendering the preview; capture it again."};
        }
        return capture_->Poll();
    }
    void CancelPreview() override { if (capture_) capture_->Cancel(); }
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
    PreviewDocument preview_;
    std::unique_ptr<EditorAuthoringPreview> capture_;
    const DocumentSceneBase* captureScene_ = nullptr;
    uint64_t captureRevision_ = 0;
};

EditorOperationResult PreviewCinematics(const CinematicsWorkspaceController& controller,
    DocumentSceneBase& scene, const nlohmann::json& parameters, EditorAuthoringPreview& capture) {
    const auto* sequence = controller.GetSequenceDocument().GetAssetSequence();
    if (!sequence) return {false,"NO_SEQUENCE_ASSET","Open a Sequence asset before requesting a preview."};
    const float time = parameters.value("time",0.0f);
    if (!std::isfinite(time) || time < 0 || time > sequence->durationSeconds)
        return {false,"INVALID_PREVIEW_TIME","Preview time must lie within the sequence duration."};
    SEQUENCER::SequenceBindingContext bindings;
    RestoreSequencePreviewBindings(scene.GetSceneDocument(),controller.GetSequenceDocument().GetAssetGuid(),*sequence,bindings);
    RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request;
    request.viewId = capture.ViewId();
    request.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    request.width = std::clamp(parameters.value("width",960),128,1536);
    request.height = std::clamp(parameters.value("height",540),128,1536);
    CameraDirector resolver;
    CinematicCameraEvaluation evaluation;
    if (!SEQUENCER::EvaluateSequenceCameraView(*sequence,bindings,scene.GetWorld(),resolver,time,
            static_cast<float>(request.width)/request.height,evaluation,request.cameraFrame.camera))
        return {false,"CAMERA_EVALUATION_FAILED","No valid shot camera or required binding resolves at this time.",{{"time",time}}};
    request.cameraFrame.valid = true;
    request.cameraFrame.sourceCameraObjectId = evaluation.cameraObjectId.value;
    request.sceneRevision = scene.GetSceneDocumentRevision();
    request.contentRevision = std::hash<std::string>{}(controller.DescribeAuthoring(scene).dump() + std::to_string(time));
    request.cameraFrame.revision = request.contentRevision;
    request.visible = true;
    request.drawDebug = false;
    RENDER3D::EDITORVIEW::SubmitRequest(request);
    const auto& camera = request.cameraFrame.camera;
    const auto eye = camera.GetPosition();
    return capture.Start(scene.GetAssetDatabase().GetLibraryRoot(),
        {{"label","Sequence camera preview"},{"assetGuid",controller.GetSequenceDocument().GetAssetGuid().value},
         {"time",time},{"shotId",evaluation.shotId},{"documentRevision",request.contentRevision},
         {"sceneRevision",request.sceneRevision},{"eye",{eye.x,eye.y,eye.z}},
         {"coverage",{{"cameraAnimation",true},{"transitions",true},{"sceneSimulation",false},{"sequenceScreenEffects",false}}}});
}
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
                {"name",controller.GetSequenceDocument().GetAssetSequence() ? controller.GetSequenceDocument().GetAssetSequence()->name : std::string{}},
                {"dirty",controller.IsSequenceDocumentDirty()}, {"preview",true},
                {"previewScope","Sequence camera at requested time over the current scene; scene simulation and sequence post effects are not sampled."}}; },
            [](const C& controller) { EditorWorkspaceOpenRequest request{}; request.workspaceId=EditorWorkspaceId::Cinematics;
                return request; }, PreviewCinematics);
    }});
}
}
