#include "HIKARI_BoundSceneAuthoringSession.h"
#include "HIKARI_EditorAuthoringPreview.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
class BoundSceneSession final : public EditorAuthoringSession {
public:
    BoundSceneSession(EditorOperationRegistry& source, EditorWorkspaceHost& host, BoundSceneAuthoringContext context,
                      EditorWorkspaceId workspace)
        : host_(host), context_(std::move(context)), workspace_(workspace),
          previewId_{AllocateAuthoringPreviewViewId()}, capture_(previewId_) {
        for (const auto& id : source.RegisteredIds()) {
            const auto* definition = source.Find(id);
            if (definition->workspace != "scene" && definition->workspace != "terrain_surface")
                continue;
            if (!definition->requiresDocumentSession || id == "scene.open" || id == "scene.new" || id == "scene.load")
                continue;
            auto local = *definition;
            local.execute = [this, definition = *definition](const nlohmann::json& parameters) {
                if (!Matches())
                    return EditorOperationResult{
                        false, "DOCUMENT_CONFLICT",
                        "场景已切换或被其他编辑操作修改。当前动作未执行；请重新打开场景会话并查询状态。"};
                const bool implicitTarget = std::any_of(definition.parameters.begin(), definition.parameters.end(),
                                                        [](const auto& p) { return p.name == "objectId"; });
                if (definition.kind == EditorOperationKind::Mutation && implicitTarget &&
                    !parameters.contains("objectId") && parameters.value("name", std::string{}).empty())
                    return EditorOperationResult{false, "EXPLICIT_TARGET_REQUIRED",
                                                 "后台操作需要明确的 objectId 或唯一名称。"};
                auto result = context_.execute(definition, parameters);
                revision_ = context_.editRevision();
                documentRevision_ = scene_->GetSceneDocumentRevision();
                return result;
            };
            operations_.Register(std::move(local));
        }
    }
    EditorOperationRegistry& Operations() override { return operations_; }
    std::any ControllerAddress() override { return {}; }
    EditorOperationResult Open(const nlohmann::json& parameters) override {
        scene_ = context_.scene();
        if (!scene_)
            return {false, "NO_SCENE", "No scene document is open."};
        const auto guid = parameters.value("assetGuid", std::string{});
        if (!guid.empty() && guid != scene_->GetCurrentSceneAssetGuid().value)
            return {false, "SCENE_NOT_LOADED",
                    "This provider binds the loaded scene. It never switches the user's scene."};
        sceneIdentity_ = scene_->GetSceneId() + "|" + scene_->GetScenePath();
        documentRevision_ = scene_->GetSceneDocumentRevision();
        revision_ = context_.editRevision();
        return {true, "OK", "Scene authoring session bound without changing the workspace."};
    }
    nlohmann::json Describe() const override {
        return {{"name", sceneIdentity_},
                {"editRevision", revision_},
                {"mode", "shared_scene_with_revision_guard"},
                {"stale", !Matches()},
                {"dirty", false},
                {"preview", true}};
    }
    EditorOperationResult Reveal() override {
        if (!Matches())
            return {false, "DOCUMENT_CONFLICT", "The scene session is stale."};
        EditorWorkspaceOpenRequest request{};
        request.workspaceId = workspace_;
        host_.RequestOpen(request);
        return {true, "OK", "Workspace requested."};
    }
    EditorOperationResult Preview(const nlohmann::json& parameters) override {
        if (!Matches())
            return {false, "DOCUMENT_CONFLICT", "The scene session is stale."};
        if (parameters.contains("time"))
            return {
                false,
                "UNSUPPORTED_PREVIEW_TIME",
                "Scene preview captures the current scene. Request time sampling from the document that owns the timeline.",
                {{"timeSampling", false}, {"requestedTime", parameters["time"]}}};
        if (capture_.IsPending())
            return {false, "CAPTURE_BUSY", "A capture is already pending."};
        RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
        request.viewId = previewId_;
        request.cameraFrame = scene_->GetResolvedCameraFrame();
        request.width = std::clamp(parameters.value("width", 768), 128, 1536);
        request.height = std::clamp(parameters.value("height", 432), 128, 1536);
        request.cameraFrame.camera.SetAspect(static_cast<float>(request.width) / request.height);
        request.sceneRevision = scene_->GetSceneDocumentRevision();
        request.visible = true;
        request.drawDebug = false;
        RENDER3D::EDITORVIEW::SubmitRequest(request);
        auto result = capture_.Start(scene_->GetAssetDatabase().GetLibraryRoot(),
                                     {{"label", "Scene inspection preview (separate render, not game output)"},
                                      {"source", "scene_inspection"},
                                      {"gameOutputAction", "capture.preview"},
                                      {"editRevision", revision_},
                                      {"sceneRevision", documentRevision_},
                                      {"assetGuid", scene_->GetCurrentSceneAssetGuid().value},
                                      {"timeSampling", false}});
        capture_.Tick(true);
        return result;
    }
    EditorOperationResult PreviewStatus(const nlohmann::json&) override {
        if (!Matches()) {
            capture_.Cancel();
            return {false, "DOCUMENT_CONFLICT", "Scene changed before capture completed."};
        }
        return capture_.Poll();
    }
    void CancelPreview() override { capture_.Cancel(); }

private:
    bool Matches() const {
        return scene_ && context_.scene() == scene_ &&
               sceneIdentity_ == scene_->GetSceneId() + "|" + scene_->GetScenePath() &&
               documentRevision_ == scene_->GetSceneDocumentRevision() && revision_ == context_.editRevision();
    }
    EditorWorkspaceHost& host_;
    BoundSceneAuthoringContext context_;
    EditorWorkspaceId workspace_;
    EditorOperationRegistry operations_;
    DocumentSceneBase* scene_ = nullptr;
    std::string sceneIdentity_;
    uint64_t revision_ = 0, documentRevision_ = 0;
    RENDER3D::RenderViewId previewId_;
    EditorAuthoringPreview capture_;
};
} // namespace
void RegisterBoundSceneAuthoringSessions(EditorAuthoringSessionHost& host, EditorOperationRegistry& operations,
                                         EditorWorkspaceHost& workspaceHost, BoundSceneAuthoringContext context) {
    for (const auto& [name, id] : {std::pair{"scene", EditorWorkspaceId::Scene},
                                   std::pair{"terrain_surface", EditorWorkspaceId::TerrainSurface}}) {
        host.RegisterWorkspace({name, name, [&operations, &workspaceHost, context, id] {
                                    return std::make_unique<BoundSceneSession>(operations, workspaceHost, context, id);
                                }});
    }
}
} // namespace HIKARI::EDITOR
