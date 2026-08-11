#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#include <utility>

#include "Core/Text/HIKARI_Localization.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {

    VfxWorkspaceController::VfxWorkspaceController()
        : emptySceneSource_(std::make_unique<
              RENDER3D::GPUDRIVEN::GpuDrivenSceneSource>()),
          previewEnvironment_(std::make_unique<SceneEnvironment>()),
          previewLighting_(std::make_unique<
              RENDER3D::LIGHTING::SceneLightingSet>()) {

        cameraController_.ApplyViewPreset(
            EditorDirectorCameraViewPreset::Perspective);
        cameraController_.Focus({ 0.0f, 0.75f, 0.0f }, 5.0f);
        previewEnvironment_->sky.enabled = true;
        previewEnvironment_->sky.mode = SkyMode::Gradient;
        previewEnvironment_->sky.zenithColor =
            { 0.018f, 0.026f, 0.042f };
        previewEnvironment_->sky.horizonColor =
            { 0.065f, 0.082f, 0.105f };
        previewEnvironment_->sky.groundColor =
            { 0.012f, 0.016f, 0.022f };
        previewEnvironment_->sky.showSunDisk = false;
        previewEnvironment_->sky.reflectionIntensity = 0.0f;
    }

    VfxWorkspaceController::~VfxWorkspaceController() {
#if defined(HIKARI_WITH_EDITOR)
        if (activePreviewViewId_.value != 0u) {
            RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
        }
#endif
    }

    bool VfxWorkspaceController::Activate(
        AssetDatabase& database,
        const AssetGuid& requestedAssetGuid,
        std::string& outMessage) {

        if (requestedAssetGuid.IsValid()) {
            if (document_.GetAssetGuid() == requestedAssetGuid) {
                outMessage.clear();
                return true;
            }
            return OpenAsset(database, requestedAssetGuid, outMessage);
        }
        if (!document_.IsOpen()) {
            NewAsset();
            outMessage = "New transient VFX document";
        } else {
            outMessage.clear();
        }
        return true;
    }

    void VfxWorkspaceController::Deactivate(
        RENDER3D::RenderViewId previewViewId) {

#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::EDITORVIEW::ClearRequest(previewViewId);
        if (activePreviewViewId_.value != 0u &&
            activePreviewViewId_.value != previewViewId.value) {
            RENDER3D::EDITORVIEW::ClearRequest(activePreviewViewId_);
        }
#else
        (void)previewViewId;
#endif
        activePreviewViewId_ = {};
        StopPreview();
        playbackService_.Reset();
        previewRenderInput_.reset();
    }

    void VfxWorkspaceController::SetPreviewNamespaceRetireCallback(
        PreviewNamespaceRetireCallback callback) {
        previewNamespaceRetireCallback_ = std::move(callback);
    }

    void VfxWorkspaceController::SetAssetRefreshCallback(
        AssetRefreshCallback callback) {
        assetRefreshCallback_ = std::move(callback);
    }

    void VfxWorkspaceController::SetMaterialEditCallback(
        MaterialEditCallback callback) {
        materialEditCallback_ = std::move(callback);
    }

    void VfxWorkspaceController::BindDocumentCommands(
        EditorCommandRouter& commandRouter) const {

        commandRouter.BindOperation(
            EditorCommandId::SaveDocument,
            "Save VFX",
            document_.IsOpen(),
            "vfx.save");
        commandRouter.BindOperation(
            EditorCommandId::Undo,
            "Undo VFX Edit",
            document_.CanUndo(),
            "vfx.undo");
        commandRouter.BindOperation(
            EditorCommandId::Redo,
            "Redo VFX Edit",
            document_.CanRedo(),
            "vfx.redo");
    }

    void VfxWorkspaceController::NewAsset(std::string name) {
        StopPreview();
        document_.NewAsset(std::move(name));
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        selection_ = {};
        previewInputOverrides_.clear();
        OnDocumentChanged();
        statusMessage_ = "New VFX document";
    }

    bool VfxWorkspaceController::NewAssetFromPreset(
        std::string_view presetId,
        std::string& outMessage) {

        const VFX::PRESETS::VfxPresetDescriptor* preset =
            VFX::PRESETS::FindVfxPreset(presetId);
        if (preset == nullptr) {
            outMessage = "Unknown VFX preset: " + std::string(presetId);
            statusMessage_ = outMessage;
            return false;
        }
        StopPreview();
        document_.NewAsset(preset->displayName);
        std::string applyMessage{};
        if (!document_.ApplyPresetPackage(
                VFX::PRESETS::MakeVfxPresetPackage(presetId),
                applyMessage)) {
            outMessage = applyMessage.empty()
                ? "Failed to apply VFX preset"
                : applyMessage;
            statusMessage_ = outMessage;
            return false;
        }
        document_.SealMerge();
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        selection_ = {};
        previewInputOverrides_.clear();
        OnDocumentChanged();
        outMessage = "Created VFX from preset: " + preset->displayName +
            "; textured preview is session-owned until first save";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::OpenAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage,
        bool discardUnsaved) {

        if (document_.GetAssetGuid() == assetGuid &&
            document_.IsOpen()) {
            outMessage = "VFX asset is already open";
            statusMessage_ = outMessage;
            return true;
        }
        StopPreview();
        if (!document_.OpenAsset(
                database,
                assetGuid,
                outMessage,
                discardUnsaved)) {
            statusMessage_ = outMessage;
            return false;
        }
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        selection_ = {};
        previewInputOverrides_.clear();
        OnDocumentChanged();
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::Save(
        AssetDatabase& database,
        std::string& outMessage) {

        ResetAssetRefreshStatus();
        const uint64_t revisionBeforeSave = document_.GetRevision();
        const bool saved = document_.Save(database, outMessage);
        if (document_.GetRevision() != revisionBeforeSave) {
            // First-save preset authoring replaces transient material recipes
            // with durable private material GUIDs. Recompile the preview
            // program before the next frame so it cannot retain empty GUIDs.
            OnDocumentChanged();
        }
        if (!saved) {
            statusMessage_ = outMessage;
            return false;
        }
        if (assetRefreshCallback_) {
            lastAssetRefreshAttempted_ = true;
            lastAssetRefreshSucceeded_ = assetRefreshCallback_(
                document_.GetAssetGuid(),
                lastAssetRefreshMessage_);
            if (!lastAssetRefreshSucceeded_) {
                outMessage += lastAssetRefreshMessage_.empty()
                    ? "; runtime asset refresh failed"
                    : "; " + lastAssetRefreshMessage_;
                statusMessage_ = outMessage;
                // The source and cooked artifact are durable. Report this as
                // a successful save with an explicit live-refresh warning.
                return true;
            }
        }
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::ResetAssetRefreshStatus() noexcept {
        lastAssetRefreshMessage_.clear();
        lastAssetRefreshAttempted_ = false;
        lastAssetRefreshSucceeded_ = false;
    }

    bool VfxWorkspaceController::RefreshAuthoredAsset(
        const AssetGuid& assetGuid,
        std::string& outMessage) {

        ResetAssetRefreshStatus();
        if (!assetGuid.IsValid()) {
            outMessage = "Authored asset GUID is invalid";
            return false;
        }
        if (!assetRefreshCallback_) {
            outMessage =
                "Live asset refresh is unavailable until the VFX asset is saved";
            lastAssetRefreshMessage_ = outMessage;
            return false;
        }
        lastAssetRefreshAttempted_ = true;
        lastAssetRefreshSucceeded_ = assetRefreshCallback_(
            assetGuid, lastAssetRefreshMessage_);
        outMessage = lastAssetRefreshMessage_;
        if (outMessage.empty()) {
            outMessage = lastAssetRefreshSucceeded_
                ? "Authored asset refreshed"
                : "Authored asset refresh failed";
        }
        return lastAssetRefreshSucceeded_;
    }

    bool VfxWorkspaceController::Revert(
        const AssetDatabase& database,
        std::string& outMessage) {

        StopPreview();
        if (!document_.Revert(database, outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        previewInputOverrides_.clear();
        OnDocumentChanged();
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::Undo(std::string& outMessage) {
        if (!document_.Undo()) {
            outMessage = "Nothing to undo";
            statusMessage_ = outMessage;
            return false;
        }
        NormalizeSelection();
        OnDocumentChanged();
        outMessage = "VFX edit undone";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::Redo(std::string& outMessage) {
        if (!document_.Redo()) {
            outMessage = "Nothing to redo";
            statusMessage_ = outMessage;
            return false;
        }
        NormalizeSelection();
        OnDocumentChanged();
        outMessage = "VFX edit redone";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::Select(
        VfxWorkspaceSelection selection) {
        selection_ = std::move(selection);
        NormalizeSelection();
    }

    const VfxWorkspaceSelection&
        VfxWorkspaceController::GetSelection() const noexcept {
        return selection_;
    }

    const VfxEditorDocument&
        VfxWorkspaceController::GetDocument() const noexcept {
        return document_;
    }

    const VFX::VfxAssetData*
        VfxWorkspaceController::GetAsset() const noexcept {
        return document_.IsOpen() ? &document_.Asset() : nullptr;
    }

    const std::vector<VFX::VfxDiagnostic>&
        VfxWorkspaceController::GetDiagnostics() const noexcept {
        return diagnostics_;
    }

    const std::string&
        VfxWorkspaceController::GetStatusMessage() const noexcept {
        return statusMessage_;
    }

    bool VfxWorkspaceController::WasLastAssetRefreshAttempted()
        const noexcept {
        return lastAssetRefreshAttempted_;
    }

    bool VfxWorkspaceController::DidLastAssetRefreshSucceed()
        const noexcept {
        return lastAssetRefreshSucceeded_;
    }

    const std::string&
        VfxWorkspaceController::GetLastAssetRefreshMessage()
        const noexcept {
        return lastAssetRefreshMessage_;
    }

    bool VfxWorkspaceController::IsPreviewPlaying() const noexcept {
        return previewPlaying_;
    }

    bool VfxWorkspaceController::IsPreviewPaused() const noexcept {
        return previewPaused_;
    }

    float VfxWorkspaceController::GetPreviewPlaybackSpeed()
        const noexcept {
        return previewPlaybackSpeed_;
    }

    float VfxWorkspaceController::GetPreviewElapsedSeconds() const noexcept {
        return previewElapsedSeconds_;
    }

    bool VfxWorkspaceController::IsPreviewLoopEnabled() const noexcept {
        return previewLoopEnabled_;
    }

    float VfxWorkspaceController::GetPreviewLoopStartSeconds()
        const noexcept {
        return previewLoopStartSeconds_;
    }

    float VfxWorkspaceController::GetPreviewLoopEndSeconds()
        const noexcept {
        return previewLoopEndSeconds_;
    }

} // namespace HIKARI::EDITOR
