#include "HIKARI_EditorAuthoringSessionHost.h"
#include "HIKARI_EditorAuthoringPreview.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "HIKARI_EditorWorkspaceSessionProviders.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperations.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include <algorithm>
#include <atomic>
#include <filesystem>

namespace HIKARI::EDITOR {
namespace {
class VfxAuthoringSession final : public EditorAuthoringSession {
public:
    VfxAuthoringSession(EditorWorkspaceHost& workspaceHost,
        std::function<DocumentSceneBase*()> scene)
        : workspaceHost_(workspaceHost), scene_(std::move(scene)),
          previewId_(AllocateAuthoringPreviewViewId()), capture_({previewId_}) {
        view_.renderViewId = {previewId_};
        controller_.SetPreviewNamespaceRetireCallback([](uint64_t id) { MESHRENDERER::ReleaseParticleNamespace(id); });
        controller_.SetAssetRefreshCallback([this](const AssetGuid&, std::string& message) {
            auto* scene = scene_();
            if (scene && scene->ReloadAssets()) return true;
            message = "Source saved; runtime asset refresh failed."; return false;
        });
        view_.extent = {768, 768};
        RegisterVfxOperations(operations_, controller_, [this]() -> AssetDatabase* {
            auto* scene = scene_();
            return scene ? &scene->GetAssetDatabase() : nullptr;
        });
    }
    ~VfxAuthoringSession() override {
        capture_.Cancel();
        controller_.Deactivate(view_.renderViewId);
    }
    bool HasInteractiveEdit() const override { return controller_.HasInteractiveEdit(); }
    EditorOperationRegistry& Operations() override { return operations_; }
    std::any ControllerAddress() override { return &controller_; }
    nlohmann::json Checkpoint() const override { return controller_.GetDocument().MakeRecovery(); }
    EditorOperationResult Restore(const nlohmann::json& snapshot) override {
        auto* scene = scene_();
        if (!scene) return {false, "NO_PROJECT", "No project is open."};
        std::string message;
        const bool ok = controller_.RestoreRecoveryDocument(scene->GetAssetDatabase(), snapshot, message);
        return {ok, ok ? "OK" : "RECOVERY_FAILED", message};
    }
    void CancelPreview() override { capture_.Cancel(); captureController_.reset(); }
    void DetachVisible() override { controller_.Deactivate(workspaceHost_.GetVfxPreviewView().renderViewId); }
    EditorOperationResult Open(const nlohmann::json& parameters) override {
        auto* scene = scene_();
        if (!scene) return {false, "NO_PROJECT", "No project is open."};
        const AssetGuid guid{parameters.value("assetGuid", std::string{})};
        std::string message;
        if (guid.IsValid()) {
            if (!controller_.OpenAsset(scene->GetAssetDatabase(), guid, message))
                return {false, "OPEN_FAILED", message};
        } else controller_.NewAsset(parameters.value("name", std::string("New VFX")));
        return {true, "OK", "Background VFX document is ready."};
    }
    nlohmann::json Describe() const override {
        const auto& document = controller_.GetDocument();
        return {{"name", document.IsOpen() ? document.Asset().name : std::string{}},
            {"assetGuid", document.GetAssetGuid().value}, {"dirty", document.IsDirty()},
            {"revision", document.GetRevision()}, {"recoverySupported", true},
            {"preview", true}, {"capturePending", capture_.IsPending()}};
    }
    EditorOperationResult Reveal() override {
        EditorWorkspaceOpenRequest request{};
        request.workspaceId = EditorWorkspaceId::Vfx;
        workspaceHost_.RequestOpen(request);
        return {true, "OK", "VFX workspace requested."};
    }
    EditorOperationResult Preview(const nlohmann::json& parameters) override {
        if (parameters.value("frameCount",1) != 1) return {false,"USE_TIMED_PREVIEWS",
            "Use authoring.preview at explicit time values to capture a sequence."};
        if (parameters.contains("path") && !parameters.value("path",std::string{}).empty())
            return {false,"UNSUPPORTED_PREVIEW_PATH","Background previews are saved in the project Library/EditorAI/Captures; the result returns the actual path."};
        if (capture_.IsPending()) return {false, "CAPTURE_BUSY", "A capture is already pending for this document."};
        auto* scene = scene_();
        if (!scene) return {false, "NO_PROJECT", "No project is open."};
        view_.extent.width = std::clamp(parameters.value("width", 768), 128, 1536);
        view_.extent.height = std::clamp(parameters.value("height", 768), 128, 1536);
        time_ = parameters.value("time", controller_.GetRequestedPreviewSeconds());
        std::string message;
        // 撮影は不変な文書スナップショットを独立 namespace で再生する。
        // 表示中の再生・GPU mailbox・カメラを奪わず、停止後も再撮影できる。
        captureController_ = std::make_unique<VfxWorkspaceController>();
        captureController_->SetPreviewNamespaceRetireCallback([](uint64_t id) { MESHRENDERER::ReleaseParticleNamespace(id); });
        if (!captureController_->CopyPreviewDocument(controller_, message) ||
            !captureController_->PrepareBackgroundPreview(view_, time_, message))
            return {false, "PREVIEW_FAILED", message};
        capturedRevision_ = controller_.GetDocument().GetRevision();
        return capture_.Start(scene->GetAssetDatabase().GetLibraryRoot(),
            {{"label", "VFX · " + std::to_string(time_) + " s"}, {"time",time_},
             {"documentRevision",controller_.GetDocument().GetRevision()}});
    }
    void Tick(uint64_t frameIndex, float) override {
        if (!capture_.IsPending()) return;
        if (controller_.GetDocument().GetRevision() != capturedRevision_) {
            capture_.Fail("Document changed during capture; request a fresh preview.");
            return;
        }
        captureController_->TickBackgroundPreview(view_, frameIndex);
        capture_.Tick(captureController_->IsBackgroundPreviewSettled(), captureController_->DescribeBackgroundPreview());
    }
    EditorOperationResult PreviewStatus(const nlohmann::json&) override {
        auto result = capture_.Poll();
        if (!capture_.IsPending()) captureController_.reset();
        return result;
    }
private:
    EditorWorkspaceHost& workspaceHost_;
    std::function<DocumentSceneBase*()> scene_;
    VfxWorkspaceController controller_;
    std::unique_ptr<VfxWorkspaceController> captureController_;
    uint64_t capturedRevision_ = 0;
    EditorOperationRegistry operations_;
    EditorViewInstance view_;
    uint64_t previewId_;
    EditorAuthoringPreview capture_;
    float time_ = 0;

};
}

void RegisterVfxAuthoringSessions(EditorAuthoringSessionHost& host,
    EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> scene) {
    host.RegisterWorkspace({"vfx", "VFX", [&workspaceHost, scene = std::move(scene)] {
        return std::make_unique<VfxAuthoringSession>(workspaceHost, scene);
    }});
}
}
