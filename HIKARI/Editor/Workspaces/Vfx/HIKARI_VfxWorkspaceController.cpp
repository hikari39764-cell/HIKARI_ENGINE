#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Core/Text/HIKARI_Localization.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxPreviewBounds.h"
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

        Camera3D previewCamera = cameraController_.GetCamera();
        previewCamera.SetLookAt({1.5f,2.0f,-6.0f}, {0.0f,0.75f,0.0f});
        cameraController_.ResetFromCamera(previewCamera);
        // 中性プレビューの発光基準。シーンの任意の露出設定に依存させない。
        previewEnvironment_->bloom.threshold = 1.0f;
        previewEnvironment_->bloom.intensity = 0.4f;
        previewEnvironment_->bloom.radius = 1.0f;
        previewEnvironment_->bloom.downsampleCount = 4u;
        previewEnvironment_->sky.enabled = true;
        previewEnvironment_->sky.mode = SkyMode::Gradient;
        previewEnvironment_->sky.zenithColor =
            { 0.018f, 0.026f, 0.042f };
        previewEnvironment_->sky.horizonColor =
            { 0.065f, 0.082f, 0.105f };
        previewEnvironment_->sky.groundColor =
            { 0.012f, 0.016f, 0.022f };
        previewEnvironment_->sky.showSunDisk = false;
        previewEnvironment_->sky.reflectionIntensity = 1.0f;
        RENDER3D::LIGHTING::SceneLightProvider key{};
        key.id = "vfx.preview.key";
        key.enabled = true;
        key.type = RENDER3D::LIGHTING::SceneLightType::Directional;
        key.direction = {-0.4f,-0.8f,0.4f};
        key.intensity = previewLightIntensity_;
        previewLighting_->lights.push_back(key);
    }

    VfxWorkspaceController::~VfxWorkspaceController() {
        ReleasePreviewNamespace();
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
                if (previewPlaying_ && previewHandle_.IsValid()) {
                    outMessage.clear();
                    return true;
                }
                return PlayPreview(outMessage);
            }
            return RequestOpenAsset(
                database, requestedAssetGuid, outMessage);
        }
        if (!document_.IsOpen()) {
            NewAsset();
            outMessage = "New transient VFX document";
            return true;
        }
        if (previewPlaying_ && previewHandle_.IsValid()) {
            outMessage.clear();
            return true;
        }
        return PlayPreview(outMessage);
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
        EndTimelineEdit(false);
        viewportHandle_={};layerGizmoEditing_=false;
        activePreviewViewId_ = {};
        StopPreview();
        playbackService_.Reset();
        previewRenderMailbox_.Clear();
        previewAccumulatedDeltaSeconds_ = 0.0f;
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
        ClearPendingAssetOpen();
        StopPreview();
        document_.NewAsset(std::move(name));
        timelineEdit_={};viewportHandle_={};layerGizmoEditing_=false;
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        selection_ = {};
        if (!document_.Asset().emitters.empty()) {
            const auto& first = document_.Asset().emitters.front();
            Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
        }
        selectLayersTab_ = false;
        previewInputOverrides_.clear();
        OnDocumentChanged();
        (void)FocusPreviewAsset();
        std::string previewMessage{};
        if (PlayPreview(previewMessage)) {
            statusMessage_ = "New VFX document; preview playing";
        } else {
            statusMessage_ = "New VFX document; " + previewMessage;
        }
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
        ClearPendingAssetOpen();
        StopPreview();
        document_.NewAsset(preset->displayName);
        timelineEdit_ = {};
        viewportHandle_ = {};
        layerGizmoEditing_ = false;
        std::string applyMessage{};
        if (!document_.ApplyPresetAsset(
                VFX::PRESETS::MakeVfxPresetAsset(presetId),
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
        if (!document_.Asset().emitters.empty()) {
            const auto& first = document_.Asset().emitters.front();
            Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
        }
        selectLayersTab_ = true;
        previewInputOverrides_.clear();
        OnDocumentChanged();
        (void)FocusPreviewAsset();
        outMessage = "Created VFX from template: " + preset->displayName +
            "; edit geometry in Basics and lighting/materials in Appearance";
        std::string previewMessage{};
        if (PlayPreview(previewMessage)) {
            outMessage += "; preview playing";
        } else if (!previewMessage.empty()) {
            outMessage += "; preview unavailable: " + previewMessage;
        }
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
        if (!document_.OpenAsset(
                database,
                assetGuid,
                outMessage,
                discardUnsaved)) {
            statusMessage_ = outMessage;
            return false;
        }
        ClearPendingAssetOpen();
        StopPreview();
        timelineEdit_ = {};
        viewportHandle_ = {};
        layerGizmoEditing_ = false;
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        selection_ = {};
        if (!document_.Asset().emitters.empty()) {
            const auto& first = document_.Asset().emitters.front();
            Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
        }
        selectLayersTab_ = true;
        previewInputOverrides_.clear();
        OnDocumentChanged();
        (void)FocusPreviewAsset();
        const std::string openMessage = outMessage;
        std::string previewMessage{};
        if (PlayPreview(previewMessage)) {
            outMessage = openMessage + "; preview playing";
        } else if (!previewMessage.empty()) {
            outMessage = openMessage + "; preview unavailable: " +
                previewMessage;
        }
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RequestOpenAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage) {

        if (!assetGuid.IsValid()) {
            outMessage = "VFX asset GUID is invalid";
            statusMessage_ = outMessage;
            return false;
        }
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::VfxEffect) {
            outMessage = "The requested VFX asset is unavailable";
            statusMessage_ = outMessage;
            return false;
        }
        if (document_.IsOpen() &&
            document_.GetAssetGuid() == assetGuid) {
            outMessage = "VFX asset is already open";
            statusMessage_ = outMessage;
            return true;
        }
        if (!document_.IsDirty()) {
            return OpenAsset(database, assetGuid, outMessage);
        }
        if (pendingAssetOpenGuid_.IsValid()) {
            if (pendingAssetOpenGuid_ == assetGuid) {
                outMessage =
                    "Waiting for the unsaved VFX decision";
                statusMessage_ = outMessage;
                return true;
            }
            outMessage =
                "Finish the pending Save / Discard / Cancel decision first";
            statusMessage_ = outMessage;
            return false;
        }

        pendingAssetOpenGuid_ = assetGuid;
        pendingAssetOpenPopupRequested_ = true;
        outMessage =
            "Unsaved VFX changes need a Save / Discard / Cancel decision";
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::ClearPendingAssetOpen() noexcept {
        pendingAssetOpenGuid_ = {};
        pendingAssetOpenPopupRequested_ = false;
    }

    bool VfxWorkspaceController::Save(
        AssetDatabase& database,
        std::string& outMessage) {

        EndTimelineEdit(false);
        ResetAssetRefreshStatus();
        const bool saved = document_.Save(database, outMessage);
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

        EndTimelineEdit(false);
        StopPreview();
        if (!document_.Revert(database, outMessage)) {
            statusMessage_ = outMessage;
            return false;
        }
        previewLoopRangeInitialized_ = false;
        ResetAssetRefreshStatus();
        previewInputOverrides_.clear();
        OnDocumentChanged();
        (void)FocusPreviewAsset();
        const std::string revertMessage = outMessage;
        std::string previewMessage{};
        if (PlayPreview(previewMessage)) {
            outMessage = revertMessage + "; preview playing";
        } else if (!previewMessage.empty()) {
            outMessage = revertMessage + "; preview unavailable: " +
                previewMessage;
        }
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::FocusPreviewAsset(
        std::string_view emitterId) {

        if (!document_.IsOpen()) return false;
        const VfxPreviewBounds bounds = EstimateVfxPreviewBounds(
            document_.Asset(), emitterId, VfxPreviewBoundsPurpose::Framing);
        if (!bounds.valid) return false;

        const Camera3D& currentCamera = cameraController_.GetCamera();
        const float halfFov = std::clamp(
            currentCamera.GetFovYRad() * 0.5f,
            0.1f,
            1.4f);
        const auto forward = MATH::Normalize(currentCamera.GetTarget() - currentCamera.GetPosition());
        const auto right = MATH::Normalize(MATH::Cross(currentCamera.GetUp(), forward));
        const auto up = MATH::Normalize(MATH::Cross(forward, right));
        const float tanY = std::tan(halfFov);
        const float tanX = tanY * (std::max)(currentCamera.GetAspect(), 0.1f);
        float fitDistance = 1.5f;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const MATH::Vec3 point{corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                corner & 4 ? bounds.maximum.z : bounds.minimum.z};
            const auto offset = point - bounds.center;
            const float depth = MATH::Dot(offset, forward);
            fitDistance = (std::max)({fitDistance,
                std::abs(MATH::Dot(offset, right)) * 1.12f / tanX - depth,
                std::abs(MATH::Dot(offset, up)) * 1.12f / tanY - depth,
                currentCamera.GetNearZ() - depth});
        }
        const float preferredDistance = fitDistance;
        const float farPlane = (std::max)(
            100.0f,
            preferredDistance + bounds.radius * 2.0f + 10.0f);
        cameraController_.GetCamera().SetPerspective(
            currentCamera.GetFovYRad(),
            currentCamera.GetAspect(),
            currentCamera.GetNearZ(),
            farPlane);
        cameraController_.Focus(bounds.center, preferredDistance);
        ++cameraRevision_;
        cameraCutPending_ = true;
        return true;
    }

    bool VfxWorkspaceController::Undo(std::string& outMessage) {
        EndTimelineEdit(false);
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
        EndTimelineEdit(false);
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

    bool VfxWorkspaceController::RestoreRecoveryDocument(const AssetDatabase& database,
        const nlohmann::json& snapshot, std::string& message) {
        if (!document_.RestoreRecovery(database, snapshot, message)) return false;
        OnDocumentChanged();
        return true;
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
