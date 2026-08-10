#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <json.hpp>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"
#include "Editor/Views/Director/HIKARI_EditorDirectorCameraController.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI {
    class AssetDatabase;
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
    };

    enum class VfxEmitterInspectorPage : uint8_t {
        Common,
        Spawn,
        Position,
        Rotation,
        Scale,
        Shape,
        Render,
        Bindings,
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
        using PreviewNamespaceRetireCallback =
            std::function<void(uint64_t)>;
        using AssetRefreshCallback = std::function<bool(
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
        void DrawDockSpace(bool resetDefaultDockLayout) const;
        VfxWorkspaceResult Draw(
            AssetDatabase& assetDatabase,
            EditorViewInstance& previewView,
            uint64_t frameIndex,
            float deltaSeconds,
            EditorCommandRouter& commandRouter);
        void BindDocumentCommands(
            EditorCommandRouter& commandRouter) const;

        void NewAsset(std::string name = "New VFX");
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
            std::string& outMessage);
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
        const VFX::VfxAssetData* GetAsset() const noexcept;
        const std::vector<VFX::VfxDiagnostic>&
            GetDiagnostics() const noexcept;
        const std::string& GetStatusMessage() const noexcept;
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

    private:
        bool ApplyAsset(
            VFX::VfxAssetData asset,
            std::string& outMessage,
            uint64_t mergeGroup = 0u);
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
            std::string& outMessage);
        void ReleasePreviewNamespace();
        void PublishPreviewRenderInput();
        void NormalizePreviewTimelineRange() noexcept;
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
        void DrawEmitterBindingsInspector(
            const VFX::VfxEmitterData& emitter);
        void DrawLinkInspector(
            AssetDatabase& assetDatabase,
            const VFX::VfxEmitterData& emitter,
            const VFX::VfxInputLinkData& link);
        void DrawCurveEditor(const VFX::VfxEmitterData& emitter);
        void DrawDiagnostics();

        VfxEditorDocument document_{};
        VfxWorkspaceSelection selection_{};
        std::vector<VFX::VfxDiagnostic> diagnostics_{};
        std::string statusMessage_{};
        std::array<char, 128> newAssetName_{ "New VFX" };
        std::array<char, 160> assetSearch_{};
        VfxParticleMaterialCatalog particleMaterialCatalog_{};

        VFX::VfxPlaybackService playbackService_{};
        VFX::VfxPlaybackHandle previewHandle_{};
        std::shared_ptr<const VFX::VfxProgram> previewProgram_{};
        std::shared_ptr<const RENDER3D::PARTICLES::ParticleRenderInput>
            previewRenderInput_{};
        std::unordered_map<std::string, VFX::VfxValue>
            previewInputOverrides_{};
        uint64_t previewProgramRevision_ = 0u;
        uint64_t previewRenderRevision_ = 1u;
        uint64_t previewFrameIndex_ = 0u;
        float previewElapsedSeconds_ = 0.0f;
        float previewPlaybackSpeed_ = 1.0f;
        bool previewPlaying_ = false;
        bool previewPaused_ = false;
        bool previewRestartPending_ = false;
        bool previewLoopEnabled_ = true;
        bool previewLoopRangeInitialized_ = false;
        float previewLoopStartSeconds_ = 0.0f;
        float previewLoopEndSeconds_ = 5.0f;
        int previewTimelineDragMode_ = 0;

        EditorDirectorCameraController cameraController_{};
        uint64_t cameraRevision_ = 1u;
        bool cameraCutPending_ = true;
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
