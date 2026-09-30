#pragma once
#include "Editor/Authoring/HIKARI_SceneComponentAuthoringSection.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Editor/Views/HIKARI_GameUiPreviewTarget.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include <functional>
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

namespace HIKARI {
class DocumentSceneBase;
struct EditorContext;
class SelectionSyncService;
} // namespace HIKARI
namespace HIKARI::EDITOR {
class EditorCommandRouter;
class EditorOperationRegistry;
class SceneObjectCommandService;
struct UiWorkspaceResult {
    std::optional<SceneObjectAuthoringHistoryRequest> history;
    bool togglePlay = false;
    INPUT::MouseCaptureRegion playViewport{};
    bool playViewportClicked = false;
};
class UiWorkspaceController {
public:
    UiWorkspaceController() = default;
    UiAnimationEditor& Animation() { return animation_; }
    const UiAnimationEditor& Animation() const { return animation_; }
    void DrawDockSpace(bool reset);
    UiWorkspaceResult Draw(DocumentSceneBase& scene, EditorContext& context, SelectionSyncService& selection,
                           SceneObjectCommandService& objects, EditorCommandRouter& commands,
                           EditorOperationRegistry& operations);
    void Deactivate(DocumentSceneBase& scene);

private:
    void DrawLibrary(DocumentSceneBase&, EditorContext&, SceneObjectCommandService&, SelectionSyncService&,
                     EditorCommandRouter&, EditorOperationRegistry&);
    void DrawPreviewData(DocumentSceneBase&, EditorContext&);
    void DrawCanvas(DocumentSceneBase&, EditorContext&, EditorCommandRouter&, EditorOperationRegistry&,
                    UiWorkspaceResult&);
    void DrawInspector(DocumentSceneBase&, EditorContext&, SelectionSyncService&, SceneObjectCommandService&,
                       EditorOperationRegistry&, UiWorkspaceResult&);
    void EditProperties(EditorOperationRegistry&, SceneObjectId, const SceneComponentData&, const nlohmann::json&);
    void FinishDrag(DocumentSceneBase&, EditorOperationRegistry*);
    const char* Prefix() const { return "UiWorkspace"; }
    std::string Window(const char* title, const char* panel) const;

    UiAnimationEditor animation_;
    bool snap_ = true;
    int device_ = 0;
    float zoom_ = 1;
    MATH::Vec2 pan_{};
    SceneObjectId canvas_{};
    char search_[128]{};
    char name_[256]{};
    SceneObjectId nameObject_{};
    const DocumentSceneBase* hierarchyScene_ = nullptr;
    uint64_t hierarchyRevision_ = ~uint64_t(0);
    std::vector<std::pair<size_t, int>> hierarchy_;
    std::string status_;
    SceneComponentAuthoringSection inspector_;
    UiSceneSystem projector_;
    BINDING::RuntimeBindingStore previewData_;
    uint64_t previewDataSource_ = 0, previewDataRevision_ = 0;
    SceneObjectId previewDataObject_{};
    RuntimeObjectHandle previewDataTarget_{};
    bool previewDataEnabled_ = false;
    GAME_UI::GameUiPresentationService content_;
    GameUiPreviewTarget target_;
    std::filesystem::path projectRoot_;
    uint64_t serial_ = 0;
    struct Drag {
        SceneObjectId object{};
        SceneComponentData before;
        nlohmann::json after;
        MATH::Vec2 mouse{};
        Affine2D axes;
        uint64_t revision = 0;
        bool animation = false;
        bool resize = false;
        bool changed = false;
        MATH::Vec2 size{}, translation{};
        std::vector<std::pair<SceneObjectId, SceneComponentData>> companions;
    };
    std::optional<Drag> drag_;
    std::optional<MATH::Vec2> marquee_;
};
} // namespace HIKARI::EDITOR
