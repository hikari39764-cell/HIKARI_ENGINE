#pragma once
#include <utility>
#include "Editor/Workspaces/Vfx/HIKARI_VfxPathEditing.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewSession.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/ContentLibrary/HIKARI_ContentLibrary.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxTimelineEditing.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxViewportEditing.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxRenderInputMailbox.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
struct SceneEnvironment;
} // namespace HIKARI

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::PARTICLES {
struct ParticleRenderInput;
}

namespace HIKARI::RENDER3D::LIGHTING {
struct SceneLightingSet;
}

namespace HIKARI::EDITOR {

class EditorCommandRouter;
struct EditorViewInstance;
struct VfxCurveAddress;

enum class VfxWorkspaceSelectionKind : uint8_t {
    Asset,
    Inputs,
    Input,
    Emitter,
    Bindings,
    Link,
    CurveKey,
    ColorStop,
    BurstKey,
    SpecialOutput,
};

enum class VfxEmitterInspectorPage : uint8_t {
    Common,
    Spawn,
    Position,
    Render,
    Bindings,
    Advanced,
};

struct VfxWorkspaceSelection {
    VfxWorkspaceSelectionKind kind = VfxWorkspaceSelectionKind::Asset;
    std::string targetId{"asset"};
    std::string emitterId{};
    std::string moduleId{};
    std::string propertyId{};
    std::string channel{};
};

struct VfxWorkspaceResult {
    bool exitToSceneRequested = false;
    std::string statusMessage{};
};

class VfxWorkspaceController final {
public:
    bool HasInteractiveEdit() const {
        return layerGizmoEditing_ || timelineEdit_.baseline.has_value() || !viewportHandle_.id.empty();
    }
    bool EditLayerTiming(std::string_view layerId, VfxLayerTimeEdit mode, float deltaSeconds, std::string& message);
    bool MoveLayerKeys(std::span<const std::string> ids, float deltaSeconds, std::string& message);
    bool SetLayerParameterKey(std::string_view layerId, std::string_view targetId, std::string_view propertyId,
                              float seconds, float value, std::string& message, uint64_t mergeGroup = 0);
    bool RemoveLayerParameterTrack(std::string_view layerId, std::string_view trackId, std::string& message);
    using AssetRefreshCallback = std::function<bool(const AssetGuid&, std::string&)>;
    using MaterialEditCallback = std::function<bool(const AssetGuid&, std::string&)>;

    VfxWorkspaceController() = default;
    ~VfxWorkspaceController() = default;
    VfxPreviewSession& Preview() { return preview_; }
    const VfxPreviewSession& Preview() const { return preview_; }

    bool Activate(AssetDatabase& assetDatabase, const AssetGuid& requestedAssetGuid, std::string& outMessage);
    void Deactivate(RENDER3D::RenderViewId previewViewId);
    void SetAssetRefreshCallback(AssetRefreshCallback callback);
    void SetMaterialEditCallback(MaterialEditCallback callback);
    void DrawDockSpace(bool resetDefaultDockLayout) const;
    VfxWorkspaceResult Draw(AssetDatabase& assetDatabase, const AssetRegistry& assetRegistry,
                            EditorViewInstance& previewView, uint64_t frameIndex, float deltaSeconds,
                            EditorCommandRouter& commandRouter);
    void BindDocumentCommands(EditorCommandRouter& commandRouter) const;

    void NewAsset(std::string name = "New VFX");
    bool NewAssetFromPreset(std::string_view presetId, std::string& outMessage);
    bool ApplyGeometryPreset(std::string_view layerId, std::string_view presetId, std::string& outMessage);
    bool OpenAsset(AssetDatabase& assetDatabase, const AssetGuid& assetGuid, std::string& outMessage,
                   bool discardUnsaved = false);
    bool Save(AssetDatabase& assetDatabase, std::string& outMessage);
    bool Revert(const AssetDatabase& assetDatabase, std::string& outMessage);
    bool Undo(std::string& outMessage);
    bool Redo(std::string& outMessage);
    bool RefreshAuthoredAsset(const AssetGuid& assetGuid, std::string& outMessage);

    bool AddEmitter(std::string_view parentEmitterId, std::string_view displayName, std::string_view requestedId,
                    std::string& outId, std::string& outMessage, VFX::VfxLayerKind kind = VFX::VfxLayerKind::Shape);
    bool AddPresetLayers(std::string_view presetId, std::string& outMessage);
    bool SetContentLibraryRoot(AssetDatabase&, const std::filesystem::path&, std::string&);
    bool ImportLibraryPackage(AssetDatabase&, const std::filesystem::path&, ASSETS::LIBRARY::ContentImportResult&,
                              std::string&);
    bool PublishToLibrary(AssetDatabase&, const ASSETS::LIBRARY::ContentPackageInfo&, std::string&);
    const std::vector<ASSETS::LIBRARY::ContentPackageInfo>& GetLibraryPackages() const { return libraryPackages_; }
    bool DuplicateEmitter(std::string_view emitterId, std::string& outId, std::string& outMessage);
    bool RemoveEmitter(std::string_view emitterId, std::string& outMessage);
    bool AddInput(VFX::VfxValueType type, std::string_view displayName, std::string_view requestedId,
                  std::string& outId, std::string& outMessage);
    bool RemoveInput(std::string_view inputId, std::string& outMessage);
    bool AddInputLink(std::string_view emitterId, std::string_view inputId, std::string_view moduleId,
                      std::string_view propertyId, std::string_view requestedId, std::string& outId,
                      std::string& outMessage);
    bool RemoveInputLink(std::string_view linkId, std::string& outMessage);
    bool AddCurveKey(std::string_view emitterId, std::string_view moduleId, std::string_view propertyId,
                     std::string_view channel, std::string& outId, std::string& outMessage);
    bool SetLayerColorKey(std::string_view layerId, float seconds, const MATH::Vec4& value, std::string& message,
                          uint64_t mergeGroup = 0);
    bool SetLayerTransformKey(std::string_view layerId, std::string_view property, float seconds,
                              const MATH::Vec3& value, std::string& outMessage, uint64_t mergeGroup = 0);
    void DrawLayerAnimation(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawLayerPath(const VFX::VfxEmitterData&);
    bool SetLayerBezierPath(std::string_view layerId, std::span<const VfxPathKnot> knots, std::string& message,
                            uint64_t mergeGroup = 0);
    bool SetLayerPathControl(std::string_view layerId, float seconds, bool incoming, const MATH::Vec3& point,
                             std::string& message, uint64_t mergeGroup = 0);
    bool AddLayerEffect(std::string_view layerId, MaterialEffectKind, std::string&);
    bool CopyLayerEffects(std::string_view layerId);
    bool PasteLayerEffects(std::string_view layerId, std::string& message);
    bool RemoveLayerEffect(std::string_view layerId, std::string_view effectId, std::string&);
    bool MoveLayerEffect(std::string_view layerId, std::string_view effectId, int direction, std::string&);
    // 効果の多段グラデーション編集。ストップは安定 ID で指す。
    bool AddLayerEffectColorStop(std::string_view layerId, std::string_view effectId, std::string& outId,
                                 std::string& outMessage);
    bool RemoveLayerEffectColorStop(std::string_view layerId, std::string_view effectId, std::string_view stopId,
                                    std::string& outMessage);
    bool SetLayerEffectColorStop(std::string_view layerId, std::string_view effectId, std::string_view stopId,
                                 float time, const MATH::Vec4& color, std::string& outMessage,
                                 uint64_t mergeGroup = 0u);
    // 既存のストップ／キー時刻から最も広い空き区間の中央を返す。
    // 曲線と効果グラデーションの両方が同じ規則で時刻を選ぶ。
    static float FindAvailableCurveTime(std::vector<float> times);
    void DrawLayerEffects(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEffectBlend(AssetDatabase&, const VFX::VfxEmitterData&, const MaterialEffectStep&);
    void DrawLayerMatte(AssetDatabase&, const VFX::VfxEmitterData&);
    bool ResetLayerEffect(std::string_view layerId, std::string_view effectId, std::string&);
    bool RemoveCurveKey(std::string_view emitterId, std::string_view moduleId, std::string_view propertyId,
                        std::string_view channel, std::string_view keyId, std::string& outMessage);
    bool AddColorStop(std::string_view emitterId, std::string_view moduleId, std::string_view propertyId,
                      std::string& outId, std::string& outMessage);
    bool RemoveColorStop(std::string_view emitterId, std::string_view moduleId, std::string_view propertyId,
                         std::string_view stopId, std::string& outMessage);
    bool AddBurstKey(std::string_view emitterId, float time, uint32_t count, std::string_view requestedId,
                     std::string& outId, std::string& outMessage);
    bool SetBurstKey(std::string_view keyId, float time, uint32_t count, std::string& outMessage,
                     uint64_t mergeGroup = 0u);
    bool RemoveBurstKey(std::string_view keyId, std::string& outMessage);
    bool AddSpecialOutput(std::string_view emitterId, VFX::VfxSpecialOutputType type, std::string_view requestedId,
                          std::string& outId, std::string& outMessage);
    bool RemoveSpecialOutput(std::string_view outputId, std::string& outMessage);
    bool SetProperty(std::string_view targetId, std::string_view propertyId, const nlohmann::json& value,
                     std::string& outMessage, uint64_t mergeGroup = 0u, AssetDatabase* assetDatabase = nullptr);
    void SealPropertyEdit() noexcept { document_.SealMerge(); }

    bool ReparentEmitter(std::string_view emitterId, std::string_view newParentEmitterId, std::string& outMessage);
    bool ReorderEmitter(std::string_view emitterId, int direction, std::string& outMessage);
    bool FocusSelectedEmitter(std::string& outMessage);

    void Select(VfxWorkspaceSelection selection);
    const VfxWorkspaceSelection& GetSelection() const noexcept;
    const VfxEditorDocument& GetDocument() const noexcept;
    bool RestoreRecoveryDocument(const AssetDatabase& database, const nlohmann::json& snapshot, std::string& message);
    const VFX::VfxAssetData* GetAsset() const noexcept;
    const std::vector<VFX::VfxDiagnostic>& GetDiagnostics() const noexcept;
    const std::string& GetStatusMessage() const noexcept;
    // AI 消費者向けの現在状態の要約（開いている資産・層・効果・選択・再生位置）。
    // 文書の状態。シェル、AI の文脈、バックグラウンドのセッションで共有する。
    nlohmann::json DescribeDocumentState() const;
    bool WasLastAssetRefreshAttempted() const noexcept;
    bool DidLastAssetRefreshSucceed() const noexcept;
    const std::string& GetLastAssetRefreshMessage() const noexcept;

    const VFX::VfxEmitterData* FindEmitter(std::string_view emitterId) const noexcept;
    const VFX::VfxExposedInputData* FindInput(std::string_view inputId) const noexcept;
    const VFX::VfxInputLinkData* FindInputLink(std::string_view linkId,
                                               const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;
    const VFX::VfxCurveKeyData* FindCurveKey(std::string_view keyId, const VFX::VfxEmitterData** outEmitter = nullptr,
                                             VfxCurveAddress* outAddress = nullptr) const noexcept;
    const VFX::VfxColorStopData* FindColorStop(std::string_view stopId,
                                               const VFX::VfxEmitterData** outEmitter = nullptr,
                                               std::string* outModuleId = nullptr,
                                               std::string* outPropertyId = nullptr) const noexcept;
    const VFX::VfxBurstKeyData* FindBurstKey(std::string_view keyId,
                                             const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;
    const VFX::VfxSpecialOutputData* FindSpecialOutput(std::string_view outputId,
                                                       const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;

private:
    bool RequestOpenAsset(AssetDatabase& assetDatabase, const AssetGuid& assetGuid, std::string& outMessage);
    bool SetLayerValueKey(std::string_view layerId, std::string_view property, float seconds, const MATH::Vec4& value,
                          std::string& message, uint64_t mergeGroup);
    bool DrawLayerParameter(const VFX::VfxEmitterData&, std::string_view targetId, std::string_view propertyId);
    bool DrawLayerGizmo(const EditorViewportRect& rect);
    bool DrawLayerHandles(const EditorViewportRect& rect, bool hovered, bool gizmoInteracting,
                          std::string& pickedLayer);
    void DrawTimelineLayers(float originX, float width, float labelWidth, float duration);
    void BeginTimelineEdit();
    void EndTimelineEdit(bool cancel);
    void CopyTimelineSelection();
    bool PasteTimelineSelection(float seconds, bool merge = false);
    bool MoveTimelineSelection(float seconds, bool fromBaseline = false);
    bool DeleteTimelineSelection();
    void ClearPendingAssetOpen() noexcept;
    bool ApplyAsset(VFX::VfxAssetData asset, std::string& outMessage, uint64_t mergeGroup = 0u,
                    VfxDocumentImpact impact = VfxDocumentImpact::Runtime);
    void ResetAssetRefreshStatus() noexcept;
    void OnDocumentChanged();
    void NormalizeSelection();
    std::string AllocateStableId(std::string_view prefix) const;
    bool ContainsStableId(std::string_view id) const noexcept;
    void RegenerateEmitterIds(VFX::VfxEmitterData& emitter) const;
    VFX::VfxEmitterData* FindEmitterMutable(VFX::VfxAssetData& asset, std::string_view emitterId) const noexcept;

    void UpdatePreview(uint64_t frameIndex, float deltaSeconds) {
        preview_.UpdatePreview(frameIndex, deltaSeconds);
        if (!preview_.statusMessage_.empty())
            statusMessage_ = std::exchange(preview_.statusMessage_, {});
    }

    void DrawToolbarWindow(AssetDatabase& assetDatabase, VfxWorkspaceResult& result,
                           EditorCommandRouter& commandRouter);
    void DrawPendingAssetOpenModal(AssetDatabase& assetDatabase);
    void DrawHierarchyWindow(AssetDatabase& assetDatabase);
    void DrawPreviewWindow(EditorViewInstance& previewView, float deltaSeconds);
    void DrawInspectorWindow(AssetDatabase& assetDatabase);
    void DrawCurveDiagnosticsWindow();
    void DrawTimeline();
    void DrawEmitterTree(const VFX::VfxAssetData& asset, std::string_view parentId);
    void DrawInputInspector(AssetDatabase& assetDatabase, const VFX::VfxExposedInputData& input);
    void DrawEmitterInspector(AssetDatabase& assetDatabase, const VFX::VfxEmitterData& emitter);
    void DrawEmitterAppearance(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEmitterAdvanced(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawAdjustmentLayerInspector(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEmitterGeometry(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawGeometryPresetPicker(const VFX::VfxEmitterData&);
    void DrawEmitterPlacement(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEmitterTiming(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEmitterSpawnPage(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawEmitterMotionPage(AssetDatabase&, const VFX::VfxEmitterData&);
    void DrawTemplateLibrary();
    void DrawContentLibrary(AssetDatabase&);
    void DrawEmitterBindingsInspector(const VFX::VfxEmitterData& emitter);
    void DrawLinkInspector(AssetDatabase& assetDatabase, const VFX::VfxEmitterData& emitter,
                           const VFX::VfxInputLinkData& link);
    void DrawCurveEditor(const VFX::VfxEmitterData& emitter);
    void DrawDiagnostics();

    // 現在描画中のフレームのレジストリ。貼図プレビューの解決に使う。
    const AssetRegistry* frameAssetRegistry_ = nullptr;
    VfxEditorDocument document_{};
    VfxPreviewSession preview_;

    VfxWorkspaceSelection selection_{};
    std::string statusMessage_{};
    AssetGuid pendingAssetOpenGuid_{};
    bool pendingAssetOpenPopupRequested_ = false;
    std::array<char, 128> newAssetName_{"New VFX"};
    std::array<char, 160> assetSearch_{};
    std::array<char, 160> templateSearch_{};
    bool selectLayersTab_ = false;
    std::string newTemplateId_{};
    std::filesystem::path libraryProjectRoot_;
    std::filesystem::path contentLibraryRoot_;
    std::vector<ASSETS::LIBRARY::ContentPackageInfo> libraryPackages_;
    std::array<char, 81> libraryPackageId_{"my-effect"};
    std::array<char, 81> libraryPackageRevision_{"1"};
    std::array<char, 96> libraryCategory_{"Team"};
    std::array<char, 256> libraryLicense_{};
    std::array<char, 160> librarySearch_{};
    VfxParticleMaterialCatalog particleMaterialCatalog_{};
    VfxParticleTextureCatalog particleTextureCatalog_{};

    EditorTransformGizmoState layerGizmoState_{};
    bool layerGizmoEditing_ = false;
    TransformData layerGizmoTransform_{};
    MATH::Mat4 layerGizmoFrame_{};
    MATH::Mat4 layerGizmoAnimation_{};
    VfxTimelineEditState timelineEdit_{};
    VfxViewportHandleState viewportHandle_{};
    int previewTimelineDragMode_ = 0;

    bool showPreviewHud_ = true;
    std::vector<MaterialEffectStep> effectClipboard_;
    std::vector<VFX::VfxParameterTrackData> effectTrackClipboard_;
    char effectSearch_[128]{};
    AssetRefreshCallback assetRefreshCallback_{};
    MaterialEditCallback materialEditCallback_{};
    std::string lastAssetRefreshMessage_{};
    bool lastAssetRefreshAttempted_ = false;
    bool lastAssetRefreshSucceeded_ = false;

    std::string selectedCurveKeyId_{};
    std::string selectedColorStopId_{};
    std::string curveEditorModuleId_{};
    std::string curveEditorPropertyId_{};
    std::string curveEditorChannel_{"x"};
    VfxEmitterInspectorPage emitterInspectorPage_ = VfxEmitterInspectorPage::Common;
    std::string emitterInspectorEmitterId_{};
    bool requestDeleteEmitter_ = false;
    bool requestDeleteInput_ = false;
};

} // namespace HIKARI::EDITOR
