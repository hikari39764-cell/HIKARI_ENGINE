#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
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

private:
  struct SlotState {
    ModelMaterialSlotView view{};
    std::shared_ptr<Material> previewMaterial{};
  };

  struct AssetChoice {
    AssetGuid guid{};
    std::string label{};
    bool ready = false;
    bool textureAsset = false;
    MaterialAssetKind materialKind = MaterialAssetKind::Material;
  };

  struct MaterialEditGesture {
    MaterialAssetData before{};
    std::string label{};
  };

  bool OpenModel(DocumentSceneBase &scene, const AssetGuid &guid,
                 std::string &outMessage);
  void SaveActiveCameraState();
  void ClosePreviewRequest(EditorWorkspaceHost &workspaceHost) const;
  void FitPreviewCamera();
  void FocusSelectedSlot();
  void ResetPreviewCamera();
  void InitializePreviewLighting();
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
  void RebuildAllBindingPreviews(const AssetDatabase &assetDatabase,
                                 const AssetRegistry &assetRegistry);
  bool OpenMaterialAsset(DocumentSceneBase &scene, const AssetGuid &guid,
                         std::string &outMessage);
  bool LoadOrCreateBindings(DocumentSceneBase &scene,
                            std::string &outMessage);
  bool CreateMaterial(DocumentSceneBase &scene, MaterialTemplateId templateId,
                       bool instance, std::string &outMessage);
  bool CreateAuxiliaryMaterialAsset(DocumentSceneBase &scene,
                                    MaterialAssetKind kind,
                                    std::string &outMessage);
  bool CreateCharacterMaterialSet(DocumentSceneBase &scene,
                                  std::string &outMessage);
  bool SaveMaterial(DocumentSceneBase &scene, bool saveAs,
                    std::string &outMessage);
  bool SaveBindings(DocumentSceneBase &scene, bool saveAs,
                    std::string &outMessage);
  void BeginMaterialGesture(std::string label);
  void CommitMaterialGesture(DocumentSceneBase &scene);
  void CancelMaterialGesture(DocumentSceneBase &scene);
  void CommitMaterialMutation(MaterialAssetData before, std::string label,
                              DocumentSceneBase &scene);
  bool ApplyHistoryProjection(ModelMaterialWorkspaceDocument::ChangedDomain domain,
                              AssetDatabase &assetDatabase,
                              const AssetRegistry &assetRegistry,
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
  void DrawModelAndSlotsWindow(DocumentSceneBase &scene);
  void DrawBindingWindow(DocumentSceneBase &scene);
  void DrawMaterialWindow(DocumentSceneBase &scene);
  void DrawMaterialFeatureControls(DocumentSceneBase &scene,
                                   MaterialAssetData &source);
  void DrawInstanceWindow(DocumentSceneBase &scene);
  void DrawGraphWindow(DocumentSceneBase &scene);
  void DrawMaterialGraphNodePalette(DocumentSceneBase &scene,
                                    MaterialAssetData &source,
                                    MaterialGraphData &graph,
                                    MATH::Vec2 position);
  bool DrawMaterialGraphInspector(DocumentSceneBase &scene,
                                  MaterialAssetData &source,
                                  MaterialGraphData &graph);
  void DrawRampWindow(DocumentSceneBase &scene);
  void DrawDiagnosticsWindow(const DocumentSceneBase &scene);
  void DrawStatsWindow() const;

  AssetGuid modelGuid_{};
  AssetGuid candidateMaterialGuid_{};
  std::string modelDisplayName_{};
  ModelPreviewScene previewScene_{};
  std::vector<SlotState> slots_{};
  std::vector<ModelMaterialSlotView> publicSlotViews_{};
  std::optional<uint32_t> selectedMaterialIndex_{};
  ModelMaterialWorkspaceStats stats_{};

  ModelMaterialWorkspaceDocument authoringDocument_{};
  std::vector<AssetChoice> modelChoices_{};
  std::vector<AssetChoice> materialChoices_{};
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
  bool previewLightingInitialized_ = false;
  bool previewSkyEnabled_ = true;

  std::optional<MaterialEditGesture> materialGesture_{};
  AssetGuid instanceParentCacheGuid_{};
  std::optional<MaterialAssetData> instanceParentCache_{};
  uint64_t instanceParentCacheRevision_ = 0u;
  std::string selectedGraphNodeId_{};
  std::string selectedGraphEdgeId_{};
  std::string selectedRampPointId_{};
  std::string edgeSourceNodeId_{};
  std::string edgeSourcePin_{};
  std::string edgeTargetNodeId_{};
  std::string edgeTargetPin_{};
  bool graphLinkDragActive_ = false;
  float graphPanX_ = 20.0f;
  float graphPanY_ = 20.0f;
  float graphZoom_ = 1.0f;
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
};

} // namespace HIKARI::EDITOR
