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
  ModelMaterialBindingSource source =
      ModelMaterialBindingSource::ImportedFallback;
  std::string diagnostic{};
};

enum class ModelMaterialBindingCommandType : uint8_t {
  BindMaterialAsset,
  RestoreImportedFallback,
};

// UI gesture から workspace history と独立 binding asset へ接続する
// stable-slot command。Imported HMODEL は read-only のまま保持する。
struct ModelMaterialBindingCommand {
  ModelMaterialBindingCommandType type =
      ModelMaterialBindingCommandType::BindMaterialAsset;
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

struct ModelMaterialWorkspaceResult {
  bool exitToSceneRequested = false;
  std::string statusMessage{};
};

class ModelMaterialWorkspaceController {
public:
  void ApplyWorkspaceActivation(DocumentSceneBase &scene,
                                const EditorWorkspaceActivation &activation,
                                EditorWorkspaceHost &workspaceHost);

  void DrawDockSpace(bool resetDefaultDockLayout) const;
  ModelMaterialWorkspaceResult Draw(DocumentSceneBase &scene,
                                    EditorWorkspaceHost &workspaceHost,
                                    EditorCommandRouter &commandRouter);
  void BindWorkspaceCommands(EditorCommandRouter &commandRouter,
                             DocumentSceneBase &scene);

  bool CanUndo() const noexcept;
  bool CanRedo() const noexcept;
  bool Undo(DocumentSceneBase &scene, std::string &outMessage);
  bool Redo(DocumentSceneBase &scene, std::string &outMessage);

  ModelMaterialBindingCommandResult ExecuteBindingCommand(
      const ModelMaterialBindingCommand &command,
      const AssetDatabase &assetDatabase,
      const AssetRegistry &assetRegistry);

  bool IsEditingModel() const noexcept;
  const AssetGuid &GetModelGuid() const noexcept;
  const std::vector<ModelMaterialSlotView> &GetSlotViews() const noexcept;
  const ModelMaterialSlotView *GetSelectedSlot() const noexcept;
  const ModelMaterialWorkspaceStats &GetStats() const noexcept;
  bool SelectSlot(uint32_t materialIndex) noexcept;

  enum class PreviewDebugMode : uint8_t {
    Lit,
    Neutral,
    Clay,
    Unlit,
  };

  enum class PreviewEnvironmentMode : uint8_t {
    RuntimeMatch,
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

  struct MaterialEditGesture {
    MaterialAssetData before{};
    std::string label{};
  };

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

  bool OpenModel(DocumentSceneBase &scene, const AssetGuid &guid,
                 std::string &outMessage);
  bool SwitchAuthoringMode(DocumentSceneBase &scene, AuthoringMode mode,
                           std::string &outMessage);
  bool EnsureMaterialSpherePreview(DocumentSceneBase &scene,
                                   std::string &outMessage);
  bool IsPreviewReady() const noexcept;
  ModelPreviewScene &GetActivePreviewScene() noexcept;
  const ModelPreviewScene &GetActivePreviewScene() const noexcept;
  bool SelectSlotForEditing(DocumentSceneBase &scene,
                            uint32_t materialIndex,
                            std::string &outMessage);
  bool OpenImportedMaterialDraft(DocumentSceneBase &scene,
                                 uint32_t materialIndex,
                                 std::string &outMessage);
  bool RestoreSelectedImportedMaterial(DocumentSceneBase &scene,
                                       std::string &outMessage);
  void RefreshSelectedSlotPresentation();
  void SaveActiveCameraState();
  void ClosePreviewRequest(EditorWorkspaceHost &workspaceHost) const;
  void FitPreviewCamera();
  void FocusSelectedSlot();
  void ResetPreviewCamera();
  void ResetPreviewKeyLight();
  void InitializePreviewLighting();
  void AddPreviewLight(RENDER3D::LIGHTING::SceneLightType type);
  void DeleteSelectedPreviewLight();
  void EnsureBuiltInRampPresets(DocumentSceneBase &scene);
  bool BindBuiltInRampPresets(MaterialAssetData &source) const;
  void RebuildSlotState(const AssetDatabase &assetDatabase,
                        const AssetRegistry &assetRegistry);
  std::shared_ptr<Material> BuildPreviewMaterial(
      const AssetGuid &materialGuid, const AssetDatabase &assetDatabase,
      const AssetRegistry &assetRegistry, std::string &outMessage);
  bool RebuildPreviewOverride(SlotState &slot,
                              const AssetDatabase &assetDatabase,
                              const AssetRegistry &assetRegistry,
                              std::string &outMessage);
  bool RebuildAuthoredPreview(AssetDatabase &assetDatabase,
                              const AssetRegistry &assetRegistry,
                              std::string &outMessage);
  bool RefreshMaterialPreviews(DocumentSceneBase &scene,
                               std::string &outMessage);
  void FlushPendingMaterialPreview(DocumentSceneBase &scene);
  void RebuildAllBindingPreviews(const AssetDatabase &assetDatabase,
                                 const AssetRegistry &assetRegistry);
  bool OpenMaterialAsset(DocumentSceneBase &scene, const AssetGuid &guid,
                         std::string &outMessage);
  bool LoadOrCreateBindings(DocumentSceneBase &scene,
                            std::string &outMessage);
  bool OpenMaterialSet(DocumentSceneBase &scene, const AssetGuid &guid,
                       std::string &outMessage);
  bool CreateMaterialSet(DocumentSceneBase &scene,
                         std::string &outMessage);
  bool CreateMaterial(DocumentSceneBase &scene, MaterialTemplateId templateId,
                       bool instance, std::string &outMessage);
  bool CreateAndBindRenderableMaterial(
      DocumentSceneBase &scene, MaterialAssetData source,
      std::string &outMessage);
  bool CreateAuxiliaryMaterialAsset(DocumentSceneBase &scene,
                                    MaterialAssetKind kind,
                                    std::string &outMessage);
  bool SaveMaterial(DocumentSceneBase &scene, bool saveAs,
                    std::string &outMessage);
  bool SaveBindings(DocumentSceneBase &scene, bool saveAs,
                    std::string &outMessage);
  void BeginMaterialGesture(std::string label);
  void PreviewMaterialGesture(DocumentSceneBase &scene);
  void CommitMaterialGesture(DocumentSceneBase &scene);
  void CancelMaterialGesture(DocumentSceneBase &scene);
  void CommitMaterialMutation(MaterialAssetData before, std::string label,
                              DocumentSceneBase &scene);
  bool ApplyHistoryProjection(ModelMaterialWorkspaceDocument::ChangedDomain domain,
                              DocumentSceneBase &scene,
                              std::string &outMessage);
  SlotState *FindSlot(MaterialSlotId slotId) noexcept;
  const SlotState *FindSlot(MaterialSlotId slotId) const noexcept;
  SlotState *FindSlotByMaterialIndex(uint32_t materialIndex) noexcept;
  const SlotState *FindSlotByMaterialIndex(
      uint32_t materialIndex) const noexcept;
  void RefreshAssetChoices(const AssetDatabase &assetDatabase);
  const AssetChoice *FindMaterialChoice(const AssetGuid &guid) const noexcept;
  const AssetChoice *FindResourceChoice(const AssetGuid &guid) const noexcept;
  void UpdatePublicSlotViews();

  void DrawPreviewWindow(DocumentSceneBase &scene,
                         EditorWorkspaceHost &workspaceHost,
                         ModelMaterialWorkspaceResult &result,
                         EditorCommandRouter &commandRouter);
  void DrawPreviewToolbar(ModelMaterialWorkspaceResult &result,
                          EditorCommandRouter &commandRouter);
  void DrawPreviewCameraControls();
  void DrawPreviewEnvironmentControls();
  void DrawPreviewLightingPanel(DocumentSceneBase &scene);
  void DrawModelAndSlotsWindow(DocumentSceneBase &scene);
  void DrawMaterialWindow(DocumentSceneBase &scene);
  void DrawPreviewLightingProperties();
  PreviewLightViewportInteraction DrawPreviewLightViewportInteraction(
      float originX, float originY, float width, float height);
  void DrawMaterialFeatureControls(DocumentSceneBase &scene,
                                   MaterialAssetData &source);
  void DrawInstanceWindow(DocumentSceneBase &scene);
  void DrawGraphWindow(DocumentSceneBase &scene);
  void DrawMaterialGraphNodePalette(DocumentSceneBase &scene,
                                    MaterialAssetData &source,
                                    MaterialGraphData &graph,
                                    MATH::Vec2 position);
  bool DrawMaterialGraphToolbar(DocumentSceneBase &scene,
                                MaterialAssetData &source,
                                MaterialGraphData &graph,
                                bool &requestFrameAll,
                                bool &requestFocusSelection,
                                bool &requestDeleteNode,
                                bool &requestDeleteEdge);
  void FinishMaterialGraphCanvasInteraction(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, std::string completedTargetNode,
      std::string completedTargetPin, bool linkReleased,
      bool mouseInsideCanvas, bool pointerOverNode,
      const std::string &hoveredNodeId,
      const std::string &hoveredEdgeId, MATH::Vec2 canvasOrigin,
      MATH::Vec2 pointer, bool requestDeleteNode,
      bool requestDeleteEdge);
  bool DrawMaterialGraphInspector(DocumentSceneBase &scene,
                                  MaterialAssetData &source,
                                  MaterialGraphData &graph);
  bool DrawMaterialGraphNodeInlineControls(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, MaterialGraphNodeData &node,
      float x, float y, float width);
  bool DrawMaterialGraphRampNodeInlineControls(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, MaterialGraphNodeData &node,
      float width);
  bool DrawFaceRampNodeControl(DocumentSceneBase &scene,
                               MaterialAssetData &source,
                               MaterialGraphData &graph,
                               float width);
  bool DrawEyeLightingNodeInlineControls(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, float width);
  bool DrawCorneaHighlightNodeInlineControls(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, float width);
  bool BeginInlineRampEdit(DocumentSceneBase &scene,
                           const AssetGuid &rampGuid,
                           std::string &outMessage);
  bool CreateRampPresetAndBind(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, MaterialGraphNodeData &node,
      ASSETS::MATERIAL::MaterialRampPresetId presetId,
      std::string &outMessage);
  void DrawInlineRampEditorPopup(DocumentSceneBase &scene);
  void DrawMaterialGraphResponseResourceInspector(
      DocumentSceneBase &scene, MaterialAssetData &source,
      MaterialGraphData &graph, MaterialGraphNodeData &node);
  void DrawRampWindow(DocumentSceneBase &scene);
  void DrawDiagnosticsWindow(const DocumentSceneBase &scene);
  void DrawStatsWindow() const;
  bool QueuePendingTransition(PendingTransition transition,
                              std::string &outMessage);
  bool ExecutePendingTransition(DocumentSceneBase &scene);
  bool SaveForPendingTransition(DocumentSceneBase &scene);
  void DiscardForPendingTransition(DocumentSceneBase &scene);
  void DrawPendingTransitionModal(DocumentSceneBase &scene);

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
  ToolPreviewCameraStateStore cameraStateStore_{};
  EditorViewInputRouter inputRouter_{};
  uint64_t cameraRevision_ = 1u;
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
  PreviewEnvironmentMode previewEnvironmentMode_ =
      PreviewEnvironmentMode::StudioInspect;
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
  std::optional<size_t> selectedPreviewLightIndex_{};
  EditorTransformGizmo previewLightGizmo_{};
  EditorTransformGizmoState previewLightGizmoState_{};
  bool previewLightRangeDragActive_ = false;
  uint64_t previewLightIdCounter_ = 1u;

  std::optional<MaterialEditGesture> materialGesture_{};
  bool materialPreviewRefreshPending_ = false;
  AssetGuid instanceParentCacheGuid_{};
  std::optional<MaterialAssetData> instanceParentCache_{};
  uint64_t instanceParentCacheRevision_ = 0u;
  std::string selectedGraphNodeId_{};
  std::string selectedGraphEdgeId_{};
  std::unordered_set<std::string> collapsedGraphNodeIds_{};
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
  PendingTransition pendingTransition_{};
  bool pendingTransitionPopupRequested_ = false;
  bool executingPendingTransition_ = false;
};

} // namespace HIKARI::EDITOR
