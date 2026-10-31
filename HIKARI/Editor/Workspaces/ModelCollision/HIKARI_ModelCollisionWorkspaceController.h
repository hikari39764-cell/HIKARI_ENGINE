#pragma once
#include <json.hpp>
#include "Editor/Documents/HIKARI_EditorSourceVersion.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "Assets/Collision/HIKARI_ModelCollisionGenerator.h"
#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Core/HIKARI_AssetGuid.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Views/HIKARI_EditorViewInputRouter.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionHistory.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionSourceOutline.h"

namespace HIKARI {
class AssetDatabase;
class DocumentSceneBase;
} // namespace HIKARI

namespace HIKARI::EDITOR {

class EditorWorkspaceHost;

enum class ModelCollisionSelectionMode : uint8_t {
    CollisionShapes,
    SourceNodes,
};

enum class ModelCollisionPreviewVisibility : uint8_t {
    All,
    SelectedWithContext,
    SelectedOnly,
    Hidden,
};

struct ModelCollisionWorkspaceResult {
    bool exitToSceneRequested = false;
    std::string statusMessage{};
};

class ModelCollisionWorkspaceController {
public:
    bool HasInteractiveEdit() const { return gizmoEditActive_; }
    void ApplyWorkspaceActivation(DocumentSceneBase& scene, const EditorWorkspaceActivation& activation,
                                  EditorWorkspaceHost& workspaceHost);

    void DrawDockSpace(bool resetDefaultDockLayout) const;
    ModelCollisionWorkspaceResult Draw(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost,
                                       EditorCommandRouter& commandRouter);

    bool IsEditingModel() const noexcept;
    const AssetGuid& GetDocumentAssetGuid() const noexcept { return modelGuid_; }
    bool IsDocumentDirty() const noexcept;
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    // 表示中の文書があるか。DescribeDocumentState の open と同じ判定。
    bool IsDocumentOpen() const noexcept;
    nlohmann::json DescribeDocumentState() const;
    bool OpenModel(AssetDatabase& assetDatabase, const AssetGuid& guid, std::string& outMessage);
    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    bool SaveDocument(DocumentSceneBase& scene, std::string& outMessage);
    bool Undo(std::string& outMessage);
    bool Redo(std::string& outMessage);
    void BindDocumentCommands(EditorCommandRouter& commandRouter, DocumentSceneBase& scene);

    // Authoring actions exposed to the unified editor operation bus so human
    // UI, AI actions and the local bridge share the same implementation path.
    void AddShape(ASSETS::COLLISION::CollisionGeometryShapeType type);
    void DuplicateSelectedShapes();
    void DeleteSelectedShapes();
    void SetSelectedShapesEnabled(bool enabled);
    void SetSelectedShapesHidden(bool hidden) noexcept;
    void SetSelectedShapesLocked(bool locked) noexcept;
    void ShowAllShapes() noexcept;
    void HideUnselectedShapes() noexcept;
    void GenerateShapes();
    void TickBackgroundTasks() { PollGenerationTask(); }
    void ApplyGenerationDraft();
    void DiscardGenerationDraft() noexcept;

    const std::string& GetStatusMessage() const noexcept;
    bool HasGenerationDraft() const noexcept;
    size_t GetSelectedShapeCount() const noexcept;

private:
    EditorSourceVersion sourceVersion_;
    struct CollisionGenerationTaskOutput {
        ASSETS::COLLISION::ModelCollisionGenerationResult result{};
        ASSETS::COLLISION::ModelCollisionSetup setup{};
    };

    void ClosePreviewRequest(EditorWorkspaceHost& workspaceHost) const;
    void FitPreviewCamera();
    void FocusPreviewBounds(const Bounds& bounds);
    void FocusSelection();
    void CommitEdit(std::string label);
    void NormalizeShapeSelection() noexcept;
    void SelectFirstShape() noexcept;
    void SelectShape(uint64_t shapeId, bool additive) noexcept;
    void SelectSourceNode(int32_t nodeIndex, bool additive) noexcept;
    bool IsShapeSelected(uint64_t shapeId) const noexcept;
    bool IsShapeHidden(uint64_t shapeId) const noexcept;
    bool IsShapeLocked(uint64_t shapeId) const noexcept;
    bool AreAllSelectedShapesHidden() const noexcept;
    bool AreAllSelectedShapesLocked() const noexcept;
    bool ShouldDrawShape(uint64_t shapeId) const noexcept;
    float ShapeOverlayOpacity(uint64_t shapeId) const noexcept;

    ASSETS::COLLISION::ModelCollisionShape* SelectedShape() noexcept;
    const ASSETS::COLLISION::ModelCollisionShape* SelectedShape() const noexcept;
    void PollGenerationTask();

    void DrawPreviewWindow(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost,
                           ModelCollisionWorkspaceResult& result, EditorCommandRouter& commandRouter);
    void DrawPreviewToolbar(DocumentSceneBase& scene, ModelCollisionWorkspaceResult& result,
                            EditorCommandRouter& commandRouter);
    void BindSelectionCommands(EditorCommandRouter& commandRouter);
    void DrawShapeListWindow();
    void DrawShapeDetailsWindow();
    void DrawSourceModelWindow();
    void DrawAutoGenerateWindow();
    void DrawPendingModelOpenModal(DocumentSceneBase& scene, ModelCollisionWorkspaceResult& result);
    void DrawPendingCloseModal(DocumentSceneBase& scene, ModelCollisionWorkspaceResult& result);

    AssetGuid modelGuid_{};
    AssetGuid pendingModelGuid_{};
    std::string modelDisplayName_{};
    std::filesystem::path setupPath_{};
    ASSETS::COLLISION::ModelCollisionSetup setup_{};
    ModelCollisionHistory history_{};
    ModelPreviewScene previewScene_{};
    ModelCollisionSourceOutlineCache sourceOutlineCache_{};

    EditorDirectorCameraController cameraController_{};
    EditorViewInputRouter inputRouter_{};
    EditorTransformGizmo transformGizmo_{};
    EditorTransformGizmoState gizmoState_{};
    uint64_t cameraRevision_ = 1u;
    uint64_t selectedShapeId_ = 0u;
    uint64_t hoveredShapeId_ = 0u;
    int32_t primarySourceNodeIndex_ = -1;
    int32_t hoveredSourceNodeIndex_ = -1;
    int32_t sourceNodeScrollRequest_ = -1;
    ModelCollisionSelectionMode selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
    std::unordered_set<uint64_t> selectedShapeIds_{};
    std::unordered_set<uint64_t> hiddenShapeIds_{};
    std::unordered_set<uint64_t> lockedShapeIds_{};
    bool cameraCutPending_ = true;
    bool showModel_ = true;
    ModelCollisionPreviewVisibility collisionVisibility_ = ModelCollisionPreviewVisibility::SelectedOnly;
    bool showGeneratedOnly_ = false;
    bool gizmoEditActive_ = false;
    bool gizmoEditChanged_ = false;
    bool detailsEditPending_ = false;
    bool closeRequested_ = false;

    ASSETS::COLLISION::ModelCollisionGenerationTarget generationTarget_ =
        ASSETS::COLLISION::ModelCollisionGenerationTarget::WholeModel;
    ASSETS::COLLISION::ModelCollisionGenerationMethod generationMethod_ =
        ASSETS::COLLISION::ModelCollisionGenerationMethod::Box;
    bool replaceGeneratedShapes_ = true;
    bool generationPreserveGaps_ = true;
    float generationMergeDistance_ = 0.10f;
    float generationAccuracy_ = 0.05f;
    int generationBudget_ = 512;
    int generationHullVertexBudget_ = 128;
    int generationTriangleBudget_ = 1000000;
    AssetTaskService* assetTaskService_ = nullptr;
    AssetTaskId generationTaskId_ = 0u;
    std::shared_ptr<CollisionGenerationTaskOutput> generationTaskOutput_{};
    std::optional<CollisionGenerationTaskOutput> generationDraft_{};
    uint64_t editRevision_ = 1u;
    uint64_t generationStartRevision_ = 0u;
    bool generationPending_ = false;
    std::unordered_set<int32_t> selectedSourceNodes_{};
    std::array<char, 128> sourceSearch_{};
    std::string statusMessage_{};
};

} // namespace HIKARI::EDITOR
