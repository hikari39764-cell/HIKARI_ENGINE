#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"
#include "Editor/Preview/HIKARI_ToolPreviewCameraStateStore.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Views/HIKARI_EditorViewInputRouter.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspace.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
class DocumentSceneBase;
class Material;
} // namespace HIKARI

namespace HIKARI::ASSETS::MATERIAL {
enum class MaterialRampPresetId : uint8_t;
}

namespace HIKARI::EDITOR {

class EditorWorkspaceHost;
class AiAssistantPanel;
class EditorOperationRegistry;

enum class ModelMaterialBindingSource : uint8_t {
    ImportedFallback,
    AuthoredOverride,
    OverrideUnavailable,
};

struct ModelMaterialSlotView {
    uint32_t materialIndex = 0u;
    MaterialSlotId slotId{};
    std::string displayName{};
    uint32_t surfaceCount = 0u;
    AssetGuid overrideMaterialGuid{};
    ModelMaterialBindingSource source = ModelMaterialBindingSource::ImportedFallback;
    std::string diagnostic{};
};

enum class ModelMaterialBindingCommandType : uint8_t {
    BindMaterialAsset,
    RestoreImportedFallback,
};

// UI gesture から workspace history と独立 binding asset へ接続する
// stable-slot command。Imported HMODEL は read-only のまま保持する。
struct ModelMaterialBindingCommand {
    ModelMaterialBindingCommandType type = ModelMaterialBindingCommandType::BindMaterialAsset;
    MaterialSlotId slotId{};
    uint32_t materialIndexHint = 0u;
    AssetGuid materialAssetGuid{};
};

struct ModelMaterialBindingCommandResult {
    bool accepted = false;
    bool changed = false;
    AssetGuid beforeMaterialGuid{};
    AssetGuid afterMaterialGuid{};
    std::string message{};
};

struct ModelMaterialWorkspaceStats {
    uint32_t nodeCount = 0u;
    uint32_t meshCount = 0u;
    uint32_t surfaceCount = 0u;
    uint32_t materialSlotCount = 0u;
    uint32_t invalidSlotIdCount = 0u;
    uint32_t invalidSurfaceMaterialCount = 0u;
    uint64_t vertexCount = 0u;
    uint64_t triangleCount = 0u;
};

struct AiMaterialGraphEditRequest {
    // add_node | remove_node | connect | disconnect | set_node
    std::string operation{};
    // Stable node type name for add_node (for example "node.additive").
    std::string nodeType{};
    // Optional display name for add_node, or node id/name for remove_node.
    std::string name{};
    float positionX = 0.0f;
    float positionY = 0.0f;
    // "manual" keeps the supplied position; anything else auto-lays out.
    bool manualPosition = false;
    std::string sourceNode{};
    std::string sourcePin{};
    std::string targetNode{};
    std::string targetPin{};
    // Optional edge id for disconnect.
    std::string edgeId{};
    // Optional typed value for add_node/set_node.
    std::string valueType{};
    float valueX = 0.0f;
    float valueY = 0.0f;
    float valueZ = 0.0f;
    float valueW = 0.0f;
    bool valueSet = false;
    // set_node: value | valueType | displayName | resource
    std::string property{};
    // set_node: resource id for TextureSample nodes.
    std::string resource{};
    // Stable external/runtime reference used by source nodes such as Variable.
    std::string referenceId{};
    // set_node: string-valued property (displayName).
    std::string stringValue{};
};

// Unified node creation request shared by the human palette, AI actions and
// the operation registry. FunctionCall nodes require a real function asset
// GUID, which only the human Material Functions menu can supply.
struct MaterialGraphNodeCreateRequest {
    std::string nodeType{};
    std::string displayName{};
    std::string connectSourceNodeId{}, connectSourcePin{};
    MATH::Vec2 position{};
    bool manualPosition = false;
    std::string valueType{};
    bool valueSet = false;
    float valueX = 0.0f;
    float valueY = 0.0f;
    float valueZ = 0.0f;
    float valueW = 0.0f;
    std::string parameterIdHint{};
    std::string parameterIdPrefix{};
    std::string parameterDisplayName{};
    std::string referenceId{};
    AssetGuid functionAssetGuid{};
    std::vector<MaterialGraphParameterData> functionInputs{};
    std::vector<MaterialGraphParameterData> functionOutputs{};
    bool allowFunctionCall = false;
};

struct ModelMaterialWorkspaceResult {
    bool exitToSceneRequested = false;
    std::string statusMessage{};
};

class ModelMaterialWorkspaceController {
public:
    void ApplyWorkspaceActivation(DocumentSceneBase& scene, const EditorWorkspaceActivation& activation,
                                  EditorWorkspaceHost& workspaceHost);

    void SetOperationRegistry(const EditorOperationRegistry* registry) noexcept;

    void DrawDockSpace(bool resetDefaultDockLayout) const;
    ModelMaterialWorkspaceResult Draw(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost,
                                      EditorCommandRouter& commandRouter);
    void BindWorkspaceCommands(EditorCommandRouter& commandRouter, DocumentSceneBase& scene);

    bool CanUndo() const noexcept;
    bool CanRedo() const noexcept;
    bool Undo(DocumentSceneBase& scene, std::string& outMessage);
    bool Redo(DocumentSceneBase& scene, std::string& outMessage);
    bool SaveMaterialDocument(DocumentSceneBase& scene, std::string& outMessage);
    bool EditMaterialParameter(DocumentSceneBase& scene, const std::string& parameterId, const MaterialValueData& value,
                               std::string& outMessage, std::string& outCode);
    bool ApplyAiMaterialGraphEdit(DocumentSceneBase& scene, const AiMaterialGraphEditRequest& request,
                                  std::string& outMessage, std::vector<std::string>& outDetails, std::string& outCode);
    bool AddMaterialGraphNode(DocumentSceneBase& scene, const MaterialGraphNodeCreateRequest& request,
                              std::string& outMessage, std::vector<std::string>& outDetails, std::string& outCode);
    bool DuplicateSelectedGraphNodes(DocumentSceneBase& scene, std::string& outMessage);
    bool DeleteSelectedGraphNodes(DocumentSceneBase& scene, std::string& outMessage);
    bool DeleteSelectedGraphEdge(DocumentSceneBase& scene, std::string& outMessage);
    bool CreateGraphGroupFromSelection(DocumentSceneBase& scene, std::string& outMessage);
    bool UngroupSelectedGraphNodes(DocumentSceneBase& scene, std::string& outMessage);
    bool InsertMaterialModule(DocumentSceneBase& scene, std::string_view id, MATH::Vec2 position, std::string& message);
    bool ExtractSelectedMaterialFunction(DocumentSceneBase& scene, std::string_view name, std::string& message);
    bool PasteMaterialGraphFragment(MaterialAssetData& source, MaterialGraphData& graph,
                                    const MaterialGraphData& fragment, MATH::Vec2 offset, bool arrange,
                                    std::string label);
    bool ArrangeSelectedGraphNodes(DocumentSceneBase& scene, std::string& outMessage);
    void FrameMaterialGraph(bool selection, const MATH::Vec2& viewportSize) noexcept;
    void FramePreviewCamera() noexcept;
    void SetSelectedGraphNodesCollapsed(bool collapsed) noexcept;
    void SetAllGraphNodesCollapsed(bool collapsed) noexcept;
    bool HasMaterialDocument() const noexcept;
    const AssetGuid& GetDocumentAssetGuid() const noexcept { return authoringDocument_.GetMaterialAssetGuid(); }
    bool PrepareBackgroundPreview(DocumentSceneBase& scene, EditorWorkspaceHost& views, uint32_t width, uint32_t height,
                                  std::string& message);
    void ClosePreviewRequest(EditorWorkspaceHost& workspaceHost) const;
    bool HasUnsavedMaterialChanges() const noexcept;
    bool IsGraphNodeSelected(const std::string& id) const;
    void SetGraphNodeSelection(const std::string& id, bool additive);
    void ClearGraphNodeSelection();
    bool CopySelectedGraphNodes(MaterialAssetData& source, MaterialGraphData& graph);
    bool PasteGraphNodes(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph,
                         const MATH::Vec2& offset);
    bool DuplicateSelectedGraphNodes(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph);

    bool CreateGraphGroupFromSelection(MaterialAssetData& source, MaterialGraphData& graph);
    bool UngroupSelectedGraphNodes(MaterialAssetData& source, MaterialGraphData& graph);
    bool QueryAiMaterialGraph(DocumentSceneBase& scene, const std::string& nameFilter, std::string& outMessage,
                              std::vector<std::string>& outDetails, std::string& outCode);
    bool LayoutMaterialGraph(DocumentSceneBase& scene, std::string& outMessage, std::string& outCode,
                             bool selectionOnly = false);
    void AppendAiCompileDiagnostics(std::vector<std::string>& outDetails) const;
    std::string GetAiMaterialStatus() const;

    ModelMaterialBindingCommandResult ExecuteBindingCommand(const ModelMaterialBindingCommand& command,
                                                            const AssetDatabase& assetDatabase,
                                                            const AssetRegistry& assetRegistry);

    bool IsEditingModel() const noexcept;
    bool HasPendingTransition() const noexcept;
    bool OpenModel(DocumentSceneBase& scene, const AssetGuid& guid, std::string& outMessage);
    bool OpenMaterialAsset(DocumentSceneBase& scene, const AssetGuid& guid, std::string& outMessage);
    const AssetGuid& GetModelGuid() const noexcept;
    const std::vector<ModelMaterialSlotView>& GetSlotViews() const noexcept;
    const ModelMaterialSlotView* GetSelectedSlot() const noexcept;
    const ModelMaterialWorkspaceStats& GetStats() const noexcept;

    enum class PreviewDebugMode : uint8_t {
        Lit,
        Neutral,
        Clay,
        Unlit,
    };

    enum class PreviewEnvironmentMode : uint8_t {
        RuntimeMatch,
        SceneMatch,
        StudioInspect,
    };

    enum class AuthoringMode : uint8_t {
        ModelMaterials,
        MaterialAsset,
    };

private:
    struct SlotState {
        ModelMaterialSlotView view{};
        std::shared_ptr<Material> previewMaterial{};
    };

    struct AssetChoice {
        AssetGuid guid{};
        std::string label{};
        std::filesystem::path sourcePath{};
        bool ready = false;
        bool textureAsset = false;
        MaterialAssetKind materialKind = MaterialAssetKind::Material;
        std::vector<MaterialGraphParameterData> functionInputs{};
        std::vector<MaterialGraphParameterData> functionOutputs{};
        AssetGuid ownerModelGuid{};
        std::optional<MaterialRampData> rampPreview{};
    };

    using GraphClipboard = MaterialGraphData;

    struct PreviewLightViewportInteraction {
        bool glyphHovered = false;
        bool gizmoInteracting = false;
    };

    enum class PendingTransitionKind : uint8_t {
        None,
        SwitchAuthoringMode,
        OpenModel,
        OpenMaterial,
        SelectMaterialSlot,
        CreateMaterial,
        CreateAuxiliaryMaterial,
        OpenMaterialSet,
        CreateMaterialSet,
    };

    struct PendingTransition {
        PendingTransitionKind kind = PendingTransitionKind::None;
        AuthoringMode mode = AuthoringMode::ModelMaterials;
        AssetGuid assetGuid{};
        uint32_t materialIndex = 0u;
        MaterialTemplateId templateId = MaterialTemplateId::StandardPbr;
        MaterialAssetKind materialKind = MaterialAssetKind::Material;
        bool instance = false;
    };

    void RefreshAuthoringIdCounter();
    bool SwitchAuthoringMode(DocumentSceneBase& scene, AuthoringMode mode, std::string& outMessage);
    bool EnsureMaterialSpherePreview(DocumentSceneBase& scene, std::string& outMessage);
    bool IsPreviewReady() const noexcept;
    ModelPreviewScene& GetActivePreviewScene() noexcept;
    const ModelPreviewScene& GetActivePreviewScene() const noexcept;
    bool SelectSlotForEditing(DocumentSceneBase& scene, uint32_t materialIndex, std::string& outMessage);
    bool OpenImportedMaterialDraft(DocumentSceneBase& scene, uint32_t materialIndex, std::string& outMessage);
    bool RestoreSelectedImportedMaterial(DocumentSceneBase& scene, std::string& outMessage);
    void RefreshSelectedSlotPresentation();
    void SaveActiveCameraState();
    void FitPreviewCamera();
    void FocusSelectedSlot();
    void ResetPreviewCamera();
    // Preview-only lighting and environment edits own a revision separate from
    // the camera receipt consumed by the interactive-view request.
    void MarkPreviewLightingChanged() noexcept {
        ++previewContentRevision_;
        if (previewContentRevision_ == 0u) {
            previewContentRevision_ = 1u;
        }
    }
    void ResetPreviewKeyLight();
    void InitializePreviewLighting();
    void AddPreviewLight(RENDER3D::LIGHTING::SceneLightType type);
    void DeleteSelectedPreviewLight();
    void EnsureBuiltInRampPresets(DocumentSceneBase& scene);
    bool BindBuiltInRampPresets(MaterialAssetData& source) const;
    void RebuildSlotState(const AssetDatabase& assetDatabase, const AssetRegistry& assetRegistry);
    std::shared_ptr<Material> BuildPreviewMaterial(const AssetGuid& materialGuid, const AssetDatabase& assetDatabase,
                                                   const AssetRegistry& assetRegistry, std::string& outMessage);
    bool RebuildPreviewOverride(SlotState& slot, const AssetDatabase& assetDatabase, const AssetRegistry& assetRegistry,
                                std::string& outMessage);
    static bool IsRenderableMaterialAsset(MaterialAssetKind kind) noexcept;
    bool RebuildAuthoredPreview(AssetDatabase& assetDatabase, const AssetRegistry& assetRegistry,
                                std::string& outMessage);
    bool RefreshMaterialPreviews(DocumentSceneBase& scene, std::string& outMessage);
    bool FlushMaterialPreview(DocumentSceneBase& scene, std::string& outMessage);
    void RebuildAllBindingPreviews(const AssetDatabase& assetDatabase, const AssetRegistry& assetRegistry);
    bool LoadOrCreateBindings(DocumentSceneBase& scene, std::string& outMessage);
    bool OpenMaterialSet(DocumentSceneBase& scene, const AssetGuid& guid, std::string& outMessage);
    bool CreateMaterialSet(DocumentSceneBase& scene, std::string& outMessage);
    bool CreateMaterial(DocumentSceneBase& scene, MaterialTemplateId templateId, bool instance,
                        std::string& outMessage);
    bool CreateAndBindRenderableMaterial(DocumentSceneBase& scene, MaterialAssetData source, std::string& outMessage);
    bool CreateAuxiliaryMaterialAsset(DocumentSceneBase& scene, MaterialAssetKind kind, std::string& outMessage);
    bool SaveMaterial(DocumentSceneBase& scene, bool saveAs, std::string& outMessage);
    bool SaveBindings(DocumentSceneBase& scene, bool saveAs, std::string& outMessage);
    void TrackMaterialItem(MaterialAssetData before, std::string label, bool changed);
    bool DrawGraphParameter(MaterialAssetData& source, MaterialGraphData& graph, const MaterialGraphNodeData* node,
                            std::string_view id, const char* label, float width, std::string_view pin = {},
                            const MaterialValueData* fallback = nullptr);
    bool ApplyHistoryProjection(ModelMaterialWorkspaceDocument::ChangedDomain domain, DocumentSceneBase& scene,
                                std::string& outMessage);
    SlotState* FindSlot(MaterialSlotId slotId) noexcept;
    const SlotState* FindSlot(MaterialSlotId slotId) const noexcept;
    SlotState* FindSlotByMaterialIndex(uint32_t materialIndex) noexcept;
    const SlotState* FindSlotByMaterialIndex(uint32_t materialIndex) const noexcept;
    void RefreshAssetChoices(const AssetDatabase& assetDatabase);
    const AssetChoice* FindMaterialChoice(const AssetGuid& guid) const noexcept;
    void UpdatePublicSlotViews();

    void DrawPreviewWindow(DocumentSceneBase& scene, EditorWorkspaceHost& workspaceHost,
                           ModelMaterialWorkspaceResult& result, EditorCommandRouter& commandRouter);
    void DrawPreviewToolbar(ModelMaterialWorkspaceResult& result, EditorCommandRouter& commandRouter);
    void DrawPreviewCameraControls();
    void DrawPreviewEnvironmentControls();
    void DrawPreviewLightingPanel(DocumentSceneBase& scene);
    void DrawOutlineThicknessPresets(MaterialAssetData& source, MaterialGraphData& graph);
    void SubmitPreviewRequests(EditorWorkspaceHost& workspaceHost, const ModelPreviewScene& activePreview,
                               bool canvasVisible);
    void DrawModelAndSlotsWindow(DocumentSceneBase& scene);
    void DrawMaterialWindow(DocumentSceneBase& scene);
    void DrawPreviewLightingProperties();
    PreviewLightViewportInteraction DrawPreviewLightViewportInteraction(float originX, float originY, float width,
                                                                        float height);
    void DrawInstanceWindow(DocumentSceneBase& scene);
    void DrawGraphWindow(DocumentSceneBase& scene);
    void DrawMaterialGraphNodePalette(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph,
                                      MATH::Vec2 position);
    bool DrawMaterialGraphToolbar(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph,
                                  bool& requestFrameAll, bool& requestFocusSelection, bool& requestDeleteNode,
                                  bool& requestDeleteEdge);
    void FinishMaterialGraphCanvasInteraction(DocumentSceneBase& scene, MaterialAssetData& source,
                                              MaterialGraphData& graph, std::string completedTargetNode,
                                              std::string completedTargetPin, bool linkReleased, bool mouseInsideCanvas,
                                              bool pointerOverNode, const std::string& hoveredNodeId,
                                              const std::string& hoveredEdgeId, MATH::Vec2 canvasOrigin,
                                              MATH::Vec2 pointer, bool requestDeleteNode, bool requestDeleteEdge);
    bool DrawMaterialGraphInspector(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph);
    bool DrawMaterialGraphNodeInlineControls(DocumentSceneBase& scene, MaterialAssetData& source,
                                             MaterialGraphData& graph, MaterialGraphNodeData& node, float x, float y,
                                             float width);
    bool DrawMaterialGraphVariableInlineControl(DocumentSceneBase& scene, MaterialAssetData& source,
                                                MaterialGraphNodeData& node);
    bool DrawMaterialGraphRampNodeInlineControls(DocumentSceneBase& scene, MaterialAssetData& source,
                                                 MaterialGraphData& graph, MaterialGraphNodeData& node, float width);
    bool DrawFaceRampNodeControl(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph,
                                 float width);
    bool DrawEyeLightingNodeInlineControls(MaterialAssetData& source, MaterialGraphData& graph, float width);
    bool DrawCorneaHighlightNodeInlineControls(MaterialAssetData& source, MaterialGraphData& graph, float width);
    bool BeginInlineRampEdit(DocumentSceneBase& scene, const AssetGuid& rampGuid, std::string& outMessage);
    bool CreateRampPresetAndBind(DocumentSceneBase& scene, MaterialAssetData& source, MaterialGraphData& graph,
                                 MaterialGraphNodeData& node, ASSETS::MATERIAL::MaterialRampPresetId presetId,
                                 std::string& outMessage);
    void DrawInlineRampEditorPopup(DocumentSceneBase& scene);
    void DrawRampWindow(DocumentSceneBase& scene);
    void DrawDiagnosticsWindow(const DocumentSceneBase& scene);
    bool QueuePendingTransition(PendingTransition transition, std::string& outMessage);
    bool ExecutePendingTransition(DocumentSceneBase& scene);
    bool SaveForPendingTransition(DocumentSceneBase& scene);
    void DiscardForPendingTransition(DocumentSceneBase& scene);
    void DrawPendingTransitionModal(DocumentSceneBase& scene);

    AssetGuid modelGuid_{};
    AssetGuid candidateMaterialGuid_{};
    std::string modelDisplayName_{};
    ModelPreviewScene previewScene_{};
    ModelPreviewScene materialSphereScene_{};
    std::shared_ptr<Material> standalonePreviewMaterial_{};
    std::vector<SlotState> slots_{};
    std::vector<ModelMaterialSlotView> publicSlotViews_{};
    std::optional<uint32_t> selectedMaterialIndex_{};
    ModelMaterialWorkspaceStats stats_{};

    ModelMaterialWorkspaceDocument authoringDocument_{};
    std::vector<AssetChoice> modelChoices_{};
    std::vector<AssetChoice> materialChoices_{};
    std::vector<AssetChoice> materialSetChoices_{};
    std::vector<AssetChoice> textureChoices_{};
    uint64_t assetChoiceRevision_ = 0u;

    EditorDirectorCameraController cameraController_{};
    std::optional<Camera3D> materialWorkspaceEnterCamera_{};
    ToolPreviewCameraStateStore cameraStateStore_{};
    EditorViewInputRouter inputRouter_{};
    uint64_t cameraRevision_ = 1u;
    uint64_t previewContentRevision_ = 1u;
    bool cameraCutPending_ = true;
    bool previewRequestSubmitted_ = false;
    bool previewOutputReady_ = false;
    uint32_t previewOutputWidth_ = 0u;
    uint32_t previewOutputHeight_ = 0u;
    uint64_t previewOutputRevision_ = 0u;

    RENDER3D::MATERIAL::MaterialProgramRegistry materialProgramRegistry_{};
    SceneEnvironment previewEnvironment_{};
    RENDER3D::LIGHTING::SceneLightingSet previewLighting_{};
    PreviewDebugMode previewDebugMode_ = PreviewDebugMode::Lit;
    PreviewEnvironmentMode previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
    AuthoringMode authoringMode_ = AuthoringMode::ModelMaterials;
    bool previewLightingInitialized_ = false;
    bool previewSkyEnabled_ = true;
    bool previewSpatialAaEnabled_ = true;
    float previewCameraSpeedScale_ = 1.0f;
    float previewCameraBaseMoveSpeed_ = 0.0f;
    float previewCameraWheelStepScale_ = 1.0f;
    float previewCameraBaseWheelStep_ = 0.0f;
    float previewCameraFovYRad_ = 0.0f;
    bool previewLightingPanelOpen_ = false;
    bool previewCompareEnabled_ = false;
    std::optional<size_t> selectedPreviewLightIndex_{};
    EditorTransformGizmo previewLightGizmo_{};
    EditorTransformGizmoState previewLightGizmoState_{};
    bool previewLightRangeDragActive_ = false;
    uint64_t previewLightIdCounter_ = 1u;

    uint32_t materialActiveItem_ = 0;
    AssetGuid instanceParentCacheGuid_{};
    const MaterialAssetData* LoadInstanceParent(AssetDatabase& database, std::string& outMessage);
    std::optional<MaterialAssetData> instanceParentCache_{};
    uint64_t instanceParentCacheRevision_ = 0u;
    std::string selectedGraphNodeId_{};
    std::vector<std::string> selectedGraphNodeIds_{};
    std::string selectedGraphEdgeId_{};
    GraphClipboard graphClipboard_{};
    std::unordered_set<std::string> collapsedGraphNodeIds_{};
    bool graphFrameRequested_ = false;
    bool graphLinkCreatePending_ = false;
    bool graphFocusRequested_ = false;
    std::string nodePreviewId_{}, nodePreviewPin_{};
    int nodePreviewChannel_ = -1;
    bool nodePreviewRefreshPending_ = false;
    MATH::Vec2 graphContextPosition_{};
    std::array<char, 128> graphSearch_{};
    std::array<char, 128> functionName_{"Material Function"};
    std::vector<std::string> recentGraphNodes_{};
    std::string selectedRampPointId_{};
    std::string edgeSourceNodeId_{};
    std::string edgeSourcePin_{};
    std::string edgeTargetNodeId_{};
    std::string edgeTargetPin_{};
    std::string rewiredGraphEdgeId_{};
    bool graphLinkDragActive_ = false;
    std::string draggedGraphNodeId_{};
    bool graphNodeDragStarted_ = false;
    NodeGraphCanvasState graphCanvas_{};
    std::string graphCanvasDocumentKey_{};
    std::filesystem::path rampPresetProjectRoot_{};
    ModelMaterialWorkspaceDocument inlineRampDocument_{};
    std::string inlineRampPointId_{};
    std::optional<MaterialAssetData> inlineRampGestureBefore_{};
    std::string inlineRampGestureLabel_{};
    bool inlineRampPopupRequested_ = false;
    uint64_t authoringIdCounter_ = 1u;
    AssetGuid candidateAuthoringAssetGuid_{};
    std::array<char, 128> newMaterialName_{};
    std::array<char, 260> saveAsPath_{};
    std::array<char, 260> bindingSaveAsPath_{};
    MaterialTemplateId selectedTemplate_ = MaterialTemplateId::GenericToon;

    std::array<char, 128> modelSearch_{};
    std::array<char, 128> slotSearch_{};
    std::array<char, 128> materialSearch_{};
    std::array<char, 128> resourceSearch_{};
    std::string statusMessage_{};
    std::string materialPreviewExpressionStatus_{};
    const EditorOperationRegistry* operationRegistry_ = nullptr;
    PendingTransition pendingTransition_{};
    bool pendingTransitionPopupRequested_ = false;
    bool executingPendingTransition_ = false;
};

} // namespace HIKARI::EDITOR
