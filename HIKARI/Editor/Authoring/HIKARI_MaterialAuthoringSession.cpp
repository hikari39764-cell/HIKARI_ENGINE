#include "HIKARI_EditorAuthoringSessionHost.h"
#include "HIKARI_EditorAuthoringPreview.h"
#include "HIKARI_EditorWorkspaceSessionProviders.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperations.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
class MaterialAuthoringSession final : public EditorAuthoringSession {
public:
    MaterialAuthoringSession(EditorWorkspaceHost& workspaceHost, std::function<DocumentSceneBase*()> scene)
        : workspaceHost_(workspaceHost), scene_(std::move(scene)), previewId_(AllocateAuthoringPreviewViewId()),
          capture_({previewId_}) {
        views_.GetModelMaterialPreviewView().renderViewId = {previewId_};
        views_.GetModelMaterialCompareView().renderViewId = {AllocateAuthoringPreviewViewId()};
        RegisterModelMaterialOperations(operations_, controller_, scene_);
        controller_.SetOperationRegistry(&operations_);
    }
    EditorOperationRegistry& Operations() override { return operations_; }
    ~MaterialAuthoringSession() override { controller_.ClosePreviewRequest(views_); }
    void DetachVisible() override { controller_.ClosePreviewRequest(workspaceHost_); }
    std::any ControllerAddress() override { return &controller_; }
    EditorOperationResult Open(const nlohmann::json& parameters) override {
        auto* scene = scene_();
        if (!scene)
            return {false, "NO_PROJECT", "No project is open."};
        const auto guid = parameters.value("assetGuid", std::string{});
        if (!guid.empty())
            return operations_.Execute("material.open_asset", {{"assetGuid", guid}});
        return operations_.Execute(
            "material.create_from_template",
            {{"templateId", "StandardPbr"}, {"name", parameters.value("name", std::string("New Material"))}});
    }
    nlohmann::json Describe() const override {
        return {{"assetGuid", controller_.GetDocumentAssetGuid().value},
                {"dirty", controller_.HasUnsavedMaterialChanges()},
                {"preview", true},
                {"previewScope", {{"kind", "material_surface"}, {"timeSampling", false}}},
                {"status", controller_.GetAiMaterialStatus()}};
    }
    EditorOperationResult Reveal() override {
        EditorWorkspaceOpenRequest request{};
        request.workspaceId = EditorWorkspaceId::ModelMaterial;
        workspaceHost_.RequestOpen(request);
        return {true, "OK", "Material workspace requested."};
    }
    EditorOperationResult Preview(const nlohmann::json& parameters) override {
        if (parameters.contains("time"))
            return {false, "UNSUPPORTED_PREVIEW_TIME",
                    "Material preview captures the current surface; timeline sampling is not supported."};
        if (capture_.IsPending())
            return {false, "CAPTURE_BUSY", "A capture is already pending."};
        auto* scene = scene_();
        if (!scene)
            return {false, "NO_PROJECT", "No project is open."};
        std::string message;
        if (!controller_.PrepareBackgroundPreview(*scene, views_, std::clamp(parameters.value("width", 768), 128, 1536),
                                                  std::clamp(parameters.value("height", 768), 128, 1536), message))
            return {false, "PREVIEW_FAILED", message};
        auto result = capture_.Start(scene->GetAssetDatabase().GetLibraryRoot(),
                                     {{"label", "Material preview"},
                                      {"assetGuid", controller_.GetDocumentAssetGuid().value},
                                      {"timeSampling", false},
                                      {"scope", "material_surface"}});
        capture_.Tick(true);
        return result;
    }
    EditorOperationResult PreviewStatus(const nlohmann::json&) override { return capture_.Poll(); }
    void CancelPreview() override { capture_.Cancel(); }

private:
    EditorWorkspaceHost& workspaceHost_;
    std::function<DocumentSceneBase*()> scene_;
    ModelMaterialWorkspaceController controller_;
    EditorOperationRegistry operations_;
    EditorWorkspaceHost views_;
    uint64_t previewId_ = 0;
    EditorAuthoringPreview capture_;
};
} // namespace
void RegisterMaterialAuthoringSessions(EditorAuthoringSessionHost& host, EditorWorkspaceHost& workspaceHost,
                                       std::function<DocumentSceneBase*()> scene) {
    host.RegisterWorkspace({"model_material", "Model & Material", [&workspaceHost, scene = std::move(scene)] {
                                return std::make_unique<MaterialAuthoringSession>(workspaceHost, scene);
                            }});
}
} // namespace HIKARI::EDITOR
