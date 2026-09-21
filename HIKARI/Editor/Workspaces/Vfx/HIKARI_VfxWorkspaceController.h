#pragma once
#include "Editor/Workspaces/Vfx/HIKARI_VfxPathEditing.h"

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
}

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
        VfxWorkspaceSelectionKind kind =
            VfxWorkspaceSelectionKind::Asset;
        std::string targetId{ "asset" };
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
        bool HasInteractiveEdit() const { return layerGizmoEditing_ || timelineEdit_.baseline.has_value() || !viewportHandle_.id.empty(); }
        bool EditLayerTiming(std::string_view layerId, VfxLayerTimeEdit mode, float deltaSeconds, std::string& message);
        bool MoveLayerKeys(std::span<const std::string> ids, float deltaSeconds, std::string& message);
        bool SetLayerParameterKey(std::string_view layerId, std::string_view targetId,
            std::string_view propertyId, float seconds, float value, std::string& message, uint64_t mergeGroup=0);
        bool RemoveLayerParameterTrack(std::string_view layerId, std::string_view trackId, std::string& message);
        using PreviewNamespaceRetireCallback =
            std::function<void(uint64_t)>;
        using AssetRefreshCallback = std::function<bool(
            const AssetGuid&,
            std::string&)>;
        using MaterialEditCallback = std::function<bool(
            const AssetGuid&,
            std::string&)>;

        VfxWorkspaceController();
        ~VfxWorkspaceController();

        bool Activate(
            AssetDatabase& assetDatabase,
            const AssetGuid& requestedAssetGuid,
            std::string& outMessage);
        void Deactivate(RENDER3D::RenderViewId previewViewId);
        void SetPreviewNamespaceRetireCallback(
            PreviewNamespaceRetireCallback callback);
        void SetAssetRefreshCallback(AssetRefreshCallback callback);
        void SetMaterialEditCallback(MaterialEditCallback callback);
        void DrawDockSpace(bool resetDefaultDockLayout) const;
        VfxWorkspaceResult Draw(
            AssetDatabase& assetDatabase,
            const AssetRegistry& assetRegistry,
            EditorViewInstance& previewView,
            uint64_t frameIndex,
            float deltaSeconds,
            EditorCommandRouter& commandRouter);
        // 表示ウィンドウを作らず、同じ再生・描画経路を更新する。
        bool PrepareBackgroundPreview(EditorViewInstance& view, float timeSeconds,
            std::string& outMessage);
        void TickBackgroundPreview(EditorViewInstance& view, uint64_t frameIndex);
        bool IsBackgroundPreviewSettled() const noexcept;
        bool CopyPreviewDocument(const VfxWorkspaceController& source, std::string& outMessage);
        nlohmann::json DescribeBackgroundPreview() const;
        float GetRequestedPreviewSeconds() const noexcept { return previewSeekTarget_.value_or(previewElapsedSeconds_); }
        RENDER3D::RenderViewId GetActivePreviewViewId() const noexcept { return activePreviewViewId_; }
        void BindDocumentCommands(
            EditorCommandRouter& commandRouter) const;

        void NewAsset(std::string name = "New VFX");
        bool NewAssetFromPreset(
            std::string_view presetId,
            std::string& outMessage);
        bool ApplyGeometryPreset(
            std::string_view layerId,
            std::string_view presetId,
            std::string& outMessage);
        bool OpenAsset(
            AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            std::string& outMessage,
            bool discardUnsaved = false);
        bool Save(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool Revert(
            const AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);
        bool RefreshAuthoredAsset(
            const AssetGuid& assetGuid,
            std::string& outMessage);

        bool AddEmitter(
            std::string_view parentEmitterId,
            std::string_view displayName,
            std::string_view requestedId,
            std::string& outId,
            std::string& outMessage,
            VFX::VfxLayerKind kind = VFX::VfxLayerKind::Shape);
        bool AddPresetLayers(std::string_view presetId, std::string& outMessage);
        bool SetContentLibraryRoot(AssetDatabase&, const std::filesystem::path&, std::string&);
        bool ImportLibraryPackage(AssetDatabase&, const std::filesystem::path&,
            ASSETS::LIBRARY::ContentImportResult&, std::string&);
        bool PublishToLibrary(AssetDatabase&, const ASSETS::LIBRARY::ContentPackageInfo&, std::string&);
        const std::vector<ASSETS::LIBRARY::ContentPackageInfo>& GetLibraryPackages() const { return libraryPackages_; }
        bool DuplicateEmitter(
            std::string_view emitterId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveEmitter(
            std::string_view emitterId,
            std::string& outMessage);
        bool AddInput(
            VFX::VfxValueType type,
            std::string_view displayName,
            std::string_view requestedId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveInput(
            std::string_view inputId,
            std::string& outMessage);
        bool AddInputLink(
            std::string_view emitterId,
            std::string_view inputId,
            std::string_view moduleId,
            std::string_view propertyId,
            std::string_view requestedId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveInputLink(
            std::string_view linkId,
            std::string& outMessage);
        bool AddCurveKey(
            std::string_view emitterId,
            std::string_view moduleId,
            std::string_view propertyId,
            std::string_view channel,
            std::string& outId,
            std::string& outMessage);
        bool SetLayerColorKey(std::string_view layerId, float seconds, const MATH::Vec4& value, std::string& message, uint64_t mergeGroup=0);
        bool SetLayerTransformKey(std::string_view layerId, std::string_view property,
            float seconds, const MATH::Vec3& value, std::string& outMessage,
            uint64_t mergeGroup = 0);
        void DrawLayerAnimation(AssetDatabase&, const VFX::VfxEmitterData&);
        void DrawLayerPath(const VFX::VfxEmitterData&);
        bool SetLayerBezierPath(std::string_view layerId, std::span<const VfxPathKnot> knots,
            std::string& message, uint64_t mergeGroup = 0);
        bool SetLayerPathControl(std::string_view layerId, float seconds, bool incoming,
            const MATH::Vec3& point, std::string& message, uint64_t mergeGroup = 0);
        bool AddLayerEffect(std::string_view layerId, MaterialEffectKind, std::string&);
        bool CopyLayerEffects(std::string_view layerId);
        bool PasteLayerEffects(std::string_view layerId, std::string& message);
        bool RemoveLayerEffect(std::string_view layerId, std::string_view effectId, std::string&);
        bool MoveLayerEffect(std::string_view layerId, std::string_view effectId, int direction, std::string&);
        // 効果の多段グラデーション編集。ストップは安定 ID で指す。
        bool AddLayerEffectColorStop(
            std::string_view layerId,
            std::string_view effectId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveLayerEffectColorStop(
            std::string_view layerId,
            std::string_view effectId,
            std::string_view stopId,
            std::string& outMessage);
        bool SetLayerEffectColorStop(
            std::string_view layerId,
            std::string_view effectId,
            std::string_view stopId,
            float time,
            const MATH::Vec4& color,
            std::string& outMessage,
            uint64_t mergeGroup = 0u);
        // 既存のストップ／キー時刻から最も広い空き区間の中央を返す。
        // 曲線と効果グラデーションの両方が同じ規則で時刻を選ぶ。
        static float FindAvailableCurveTime(std::vector<float> times);
        void DrawLayerEffects(AssetDatabase&, const VFX::VfxEmitterData&);
        void DrawEffectBlend(AssetDatabase&, const VFX::VfxEmitterData&, const MaterialEffectStep&);
        void DrawLayerMatte(AssetDatabase&, const VFX::VfxEmitterData&);
        bool ResetLayerEffect(std::string_view layerId, std::string_view effectId, std::string&);
        bool RemoveCurveKey(
            std::string_view emitterId,
            std::string_view moduleId,
            std::string_view propertyId,
            std::string_view channel,
            std::string_view keyId,
            std::string& outMessage);
        bool AddColorStop(
            std::string_view emitterId,
            std::string_view moduleId,
            std::string_view propertyId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveColorStop(
            std::string_view emitterId,
            std::string_view moduleId,
            std::string_view propertyId,
            std::string_view stopId,
            std::string& outMessage);
        bool AddBurstKey(
            std::string_view emitterId,
            float time,
            uint32_t count,
            std::string_view requestedId,
            std::string& outId,
            std::string& outMessage);
        bool SetBurstKey(
            std::string_view keyId,
            float time,
            uint32_t count,
            std::string& outMessage,
            uint64_t mergeGroup = 0u);
        bool RemoveBurstKey(
            std::string_view keyId,
            std::string& outMessage);
        bool AddSpecialOutput(
            std::string_view emitterId,
            VFX::VfxSpecialOutputType type,
            std::string_view requestedId,
            std::string& outId,
            std::string& outMessage);
        bool RemoveSpecialOutput(
            std::string_view outputId,
            std::string& outMessage);
        bool SetProperty(
            std::string_view targetId,
            std::string_view propertyId,
            const nlohmann::json& value,
            std::string& outMessage,
            uint64_t mergeGroup = 0u,
            AssetDatabase* assetDatabase = nullptr);
        void SealPropertyEdit() noexcept {
            document_.SealMerge();
        }

        bool PlayPreview(std::string& outMessage);
        void PausePreview(bool paused);
        void StopPreview();
        bool RestartPreview(std::string& outMessage);
        bool SeekPreview(float seconds, std::string& outMessage);
        void SetPreviewPlaybackSpeed(float speed) noexcept;
        void SetPreviewLoopEnabled(bool enabled) noexcept;
        void SetPreviewLoopRange(
            float startSeconds,
            float endSeconds) noexcept;
        bool SetPreviewInput(
            std::string_view inputId,
            const VFX::VfxValue& value,
            std::string& outMessage);
        void ResetPreviewInputs();

        bool ReparentEmitter(
            std::string_view emitterId,
            std::string_view newParentEmitterId,
            std::string& outMessage);
        bool ReorderEmitter(
            std::string_view emitterId,
            int direction,
            std::string& outMessage);
        void SetEmitterHidden(
            std::string_view emitterId,
            bool hidden) noexcept;
        bool IsEmitterHidden(
            std::string_view emitterId) const noexcept;
        void SetSoloEmitter(std::string_view emitterId) noexcept;
        void ClearSoloEmitter() noexcept;
        const std::string& GetSoloEmitterId() const noexcept;
        bool IsEmitterPreviewVisible(
            std::string_view emitterId) const noexcept;
        bool FocusSelectedEmitter(std::string& outMessage);
        bool StepPreviewFrame(
            int frameCount,
            std::string& outMessage);
        void NotifyEmitterStructureChanged() noexcept;
        void NotifyEmitterVisibilityChanged() noexcept;

        void Select(VfxWorkspaceSelection selection);
        const VfxWorkspaceSelection& GetSelection() const noexcept;
        const VfxEditorDocument& GetDocument() const noexcept;
        bool RestoreRecoveryDocument(const AssetDatabase& database, const nlohmann::json& snapshot, std::string& message);
        const VFX::VfxAssetData* GetAsset() const noexcept;
        const std::vector<VFX::VfxDiagnostic>&
            GetDiagnostics() const noexcept;
        const std::string& GetStatusMessage() const noexcept;
        // AI 消費者向けの現在状態の要約（開いている資産・層・効果・選択・再生位置）。
        std::string GetAiVfxStatus() const;
        bool WasLastAssetRefreshAttempted() const noexcept;
        bool DidLastAssetRefreshSucceed() const noexcept;
        const std::string& GetLastAssetRefreshMessage() const noexcept;
        bool IsPreviewPlaying() const noexcept;
        bool IsPreviewPaused() const noexcept;
        float GetPreviewElapsedSeconds() const noexcept;
        float GetPreviewPlaybackSpeed() const noexcept;
        bool IsPreviewLoopEnabled() const noexcept;
        float GetPreviewLoopStartSeconds() const noexcept;
        float GetPreviewLoopEndSeconds() const noexcept;

        const VFX::VfxEmitterData* FindEmitter(
            std::string_view emitterId) const noexcept;
        const VFX::VfxExposedInputData* FindInput(
            std::string_view inputId) const noexcept;
        const VFX::VfxInputLinkData* FindInputLink(
            std::string_view linkId,
            const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;
        const VFX::VfxCurveKeyData* FindCurveKey(
            std::string_view keyId,
            const VFX::VfxEmitterData** outEmitter = nullptr,
            VfxCurveAddress* outAddress = nullptr) const noexcept;
        const VFX::VfxColorStopData* FindColorStop(
            std::string_view stopId,
            const VFX::VfxEmitterData** outEmitter = nullptr,
            std::string* outModuleId = nullptr,
            std::string* outPropertyId = nullptr) const noexcept;
        const VFX::VfxBurstKeyData* FindBurstKey(
            std::string_view keyId,
            const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;
        const VFX::VfxSpecialOutputData* FindSpecialOutput(
            std::string_view outputId,
            const VFX::VfxEmitterData** outEmitter = nullptr) const noexcept;

    private:
        bool RequestOpenAsset(
            AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            std::string& outMessage);
        bool SetLayerValueKey(std::string_view layerId, std::string_view property, float seconds, const MATH::Vec4& value, std::string& message, uint64_t mergeGroup);
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
        bool ApplyAsset(
            VFX::VfxAssetData asset,
            std::string& outMessage,
            uint64_t mergeGroup = 0u, VfxDocumentImpact impact = VfxDocumentImpact::Runtime);
        void ResetAssetRefreshStatus() noexcept;
        void OnDocumentChanged();
        void NormalizeSelection();
        std::string AllocateStableId(std::string_view prefix) const;
        bool ContainsStableId(std::string_view id) const noexcept;
        void RegenerateEmitterIds(VFX::VfxEmitterData& emitter) const;
        VFX::VfxEmitterData* FindEmitterMutable(
            VFX::VfxAssetData& asset,
            std::string_view emitterId) const noexcept;

        bool RebuildPreviewProgram(std::string& outMessage);
        bool SeekActivePreview(
            float seconds,
            std::string& outMessage,
            bool supersedePending = false);
        void ReleasePreviewNamespace();
        bool PublishPreviewRenderInput(
            VFX::VfxRenderInputPublishMode mode =
                VFX::VfxRenderInputPublishMode::RequireAcknowledged);
        void AcknowledgePreviewRenderInput();
        float GetPreviewTimelineDuration() const noexcept;
        void NormalizePreviewTimelineRange() noexcept;
        bool FocusPreviewAsset(
            std::string_view emitterId = {});
        void UpdatePreview(uint64_t frameIndex, float deltaSeconds);
        void SubmitPreviewRequest(
            EditorViewInstance& previewView,
            bool canvasVisible);
        std::vector<VFX::VfxParameterOverride>
            BuildPreviewParameters() const;

        void DrawToolbarWindow(
            AssetDatabase& assetDatabase,
            VfxWorkspaceResult& result,
            EditorCommandRouter& commandRouter);
        void DrawPendingAssetOpenModal(
            AssetDatabase& assetDatabase);
        void DrawHierarchyWindow(AssetDatabase& assetDatabase);
        void DrawPreviewWindow(
            EditorViewInstance& previewView,
            float deltaSeconds);
        void DrawInspectorWindow(AssetDatabase& assetDatabase);
        void DrawCurveDiagnosticsWindow();
        void DrawTimeline();
        void DrawEmitterTree(
            const VFX::VfxAssetData& asset,
            std::string_view parentId);
        void DrawInputInspector(
            AssetDatabase& assetDatabase,
            const VFX::VfxExposedInputData& input);
        void DrawEmitterInspector(
            AssetDatabase& assetDatabase,
            const VFX::VfxEmitterData& emitter);
        void DrawEmitterAppearance(AssetDatabase&, const VFX::VfxEmitterData&);
        void DrawEmitterAdvanced(AssetDatabase&, const VFX::VfxEmitterData&);
        void DrawTemplateLibrary();
        void DrawContentLibrary(AssetDatabase&);
        void DrawEmitterBindingsInspector(
            const VFX::VfxEmitterData& emitter);
        void DrawLinkInspector(
            AssetDatabase& assetDatabase,
            const VFX::VfxEmitterData& emitter,
            const VFX::VfxInputLinkData& link);
        void DrawCurveEditor(const VFX::VfxEmitterData& emitter);
        void DrawDiagnostics();

        // 現在描画中のフレームのレジストリ。貼図プレビューの解決に使う。
        const AssetRegistry* frameAssetRegistry_ = nullptr;
        VfxEditorDocument document_{};
        VfxWorkspaceSelection selection_{};
        std::vector<VFX::VfxDiagnostic> diagnostics_{};
        std::string statusMessage_{};
        AssetGuid pendingAssetOpenGuid_{};
        bool pendingAssetOpenPopupRequested_ = false;
        std::array<char, 128> newAssetName_{ "New VFX" };
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
        float previewLightYaw_ = -35.0f;
        float previewLightIntensity_ = 2.0f;
        VfxParticleMaterialCatalog particleMaterialCatalog_{};
        VfxParticleTextureCatalog particleTextureCatalog_{};

        VFX::VfxPlaybackService playbackService_{};
        VFX::VfxPlaybackHandle previewHandle_{};
        std::shared_ptr<const VFX::VfxProgram> previewProgram_{};
        VFX::VfxRenderInputMailbox previewRenderMailbox_{};
        std::unordered_map<std::string, VFX::VfxValue>
            previewInputOverrides_{};
        uint64_t previewProgramRevision_ = 0u;
        uint64_t previewFrameIndex_ = 0u;
        float previewAccumulatedDeltaSeconds_ = 0.0f;
        float previewElapsedSeconds_ = 0.0f;
        std::optional<float> previewSeekTarget_{};
        float previewPlaybackSpeed_ = 1.0f;
        EditorTransformGizmoState layerGizmoState_{};
        bool layerGizmoEditing_ = false;
        TransformData layerGizmoTransform_{};
        MATH::Mat4 layerGizmoFrame_{};
        MATH::Mat4 layerGizmoAnimation_{};
        VfxTimelineEditState timelineEdit_{};
        VfxViewportHandleState viewportHandle_{};
        bool previewPlaying_ = false;
        bool previewPaused_ = false;
        // 停止中に同じ内容を毎フレーム描き直すと bloom がわずかに揺れる。
        // 変化があったフレームだけ再描画するためのマーカー。
        bool previewNeedsRedraw_ = true;
        bool backgroundFrozenPrepared_ = false;
        uint64_t previewContentRevision_ = 0u;
        bool previewRestartPending_ = false;
        float previewRestartDelaySeconds_ = 0.0f;
        bool previewLoopEnabled_ = true;
        bool previewLoopRangeInitialized_ = false;
        float previewLoopStartSeconds_ = 0.0f;
        float previewLoopEndSeconds_ = 5.0f;
        int previewTimelineDragMode_ = 0;

        EditorDirectorCameraController cameraController_{};
        uint64_t cameraRevision_ = 1u;
        bool cameraCutPending_ = true;
        bool previewCleanView_ = false;
        bool showPreviewHud_ = true;
        bool previewBypassAdjustments_ = false;
        std::vector<MaterialEffectStep> effectClipboard_;
        std::vector<VFX::VfxParameterTrackData> effectTrackClipboard_;
        char effectSearch_[128]{};
        bool showPreviewGrid_ = true;
        bool showPreviewAxes_ = true;
        RENDER3D::RenderViewId activePreviewViewId_{};
        std::unique_ptr<RENDER3D::GPUDRIVEN::GpuDrivenSceneSource>
            emptySceneSource_{};
        std::unique_ptr<SceneEnvironment> previewEnvironment_{};
        std::unique_ptr<RENDER3D::LIGHTING::SceneLightingSet>
            previewLighting_{};
        PreviewNamespaceRetireCallback
            previewNamespaceRetireCallback_{};
        AssetRefreshCallback assetRefreshCallback_{};
        MaterialEditCallback materialEditCallback_{};
        std::string lastAssetRefreshMessage_{};
        bool lastAssetRefreshAttempted_ = false;
        bool lastAssetRefreshSucceeded_ = false;

        std::unordered_set<std::string> hiddenEmitterIds_{};
        std::string soloEmitterId_{};
        std::string selectedCurveKeyId_{};
        std::string selectedColorStopId_{};
        std::string curveEditorModuleId_{};
        std::string curveEditorPropertyId_{};
        std::string curveEditorChannel_{ "x" };
        VfxEmitterInspectorPage emitterInspectorPage_ =
            VfxEmitterInspectorPage::Common;
        std::string emitterInspectorEmitterId_{};
        bool requestDeleteEmitter_ = false;
        bool requestDeleteInput_ = false;
    };

} // namespace HIKARI::EDITOR
