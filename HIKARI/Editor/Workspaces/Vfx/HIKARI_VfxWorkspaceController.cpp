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

bool VfxWorkspaceController::Activate(AssetDatabase& database, const AssetGuid& requestedAssetGuid,
                                      std::string& outMessage) {
    if (requestedAssetGuid.IsValid()) {
        if (document_.GetAssetGuid() == requestedAssetGuid) {
            if (preview_.previewPlaying_ && preview_.previewHandle_.IsValid()) {
                outMessage.clear();
                return true;
            }
            return preview_.PlayPreview(outMessage);
        }
        return RequestOpenAsset(database, requestedAssetGuid, outMessage);
    }
    if (!document_.IsOpen()) {
        NewAsset();
        outMessage = "New transient VFX document";
        return true;
    }
    if (preview_.previewPlaying_ && preview_.previewHandle_.IsValid()) {
        outMessage.clear();
        return true;
    }
    return preview_.PlayPreview(outMessage);
}

void VfxWorkspaceController::Deactivate(RENDER3D::RenderViewId previewViewId) {
    EndTimelineEdit(false);
    viewportHandle_ = {};
    layerGizmoEditing_ = false;
    preview_.Deactivate(previewViewId);
}

void VfxWorkspaceController::SetAssetRefreshCallback(AssetRefreshCallback callback) {
    assetRefreshCallback_ = std::move(callback);
}

void VfxWorkspaceController::SetMaterialEditCallback(MaterialEditCallback callback) {
    materialEditCallback_ = std::move(callback);
}

void VfxWorkspaceController::BindDocumentCommands(EditorCommandRouter& commandRouter) const {
    commandRouter.BindOperation(EditorCommandId::SaveDocument, "Save VFX", document_.IsOpen(), "vfx.save");
    commandRouter.BindOperation(EditorCommandId::Undo, "Undo VFX Edit", document_.CanUndo(), "vfx.undo");
    commandRouter.BindOperation(EditorCommandId::Redo, "Redo VFX Edit", document_.CanRedo(), "vfx.redo");
}

void VfxWorkspaceController::NewAsset(std::string name) {
    ClearPendingAssetOpen();
    preview_.StopPreview();
    document_.NewAsset(std::move(name));
    timelineEdit_ = {};
    viewportHandle_ = {};
    layerGizmoEditing_ = false;
    preview_.previewLoopRangeInitialized_ = false;
    ResetAssetRefreshStatus();
    selection_ = {};
    if (!document_.Asset().emitters.empty()) {
        const auto& first = document_.Asset().emitters.front();
        Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
    }
    selectLayersTab_ = false;
    preview_.previewInputOverrides_.clear();
    OnDocumentChanged();
    (void)preview_.FocusPreviewAsset();
    std::string previewMessage{};
    if (preview_.PlayPreview(previewMessage)) {
        statusMessage_ = "New VFX document; preview playing";
    } else {
        statusMessage_ = "New VFX document; " + previewMessage;
    }
}

bool VfxWorkspaceController::NewAssetFromPreset(std::string_view presetId, std::string& outMessage) {
    const VFX::PRESETS::VfxPresetDescriptor* preset = VFX::PRESETS::FindVfxPreset(presetId);
    if (preset == nullptr) {
        outMessage = "Unknown VFX preset: " + std::string(presetId);
        statusMessage_ = outMessage;
        return false;
    }
    ClearPendingAssetOpen();
    preview_.StopPreview();
    document_.NewAsset(preset->displayName);
    timelineEdit_ = {};
    viewportHandle_ = {};
    layerGizmoEditing_ = false;
    std::string applyMessage{};
    if (!document_.ApplyPresetAsset(VFX::PRESETS::MakeVfxPresetAsset(presetId), applyMessage)) {
        outMessage = applyMessage.empty() ? "Failed to apply VFX preset" : applyMessage;
        statusMessage_ = outMessage;
        return false;
    }
    document_.SealMerge();
    preview_.previewLoopRangeInitialized_ = false;
    ResetAssetRefreshStatus();
    selection_ = {};
    if (!document_.Asset().emitters.empty()) {
        const auto& first = document_.Asset().emitters.front();
        Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
    }
    selectLayersTab_ = true;
    preview_.previewInputOverrides_.clear();
    OnDocumentChanged();
    (void)preview_.FocusPreviewAsset();
    outMessage = "Created VFX from template: " + preset->displayName +
                 "; edit geometry in Basics and lighting/materials in Appearance";
    std::string previewMessage{};
    if (preview_.PlayPreview(previewMessage)) {
        outMessage += "; preview playing";
    } else if (!previewMessage.empty()) {
        outMessage += "; preview unavailable: " + previewMessage;
    }
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::OpenAsset(AssetDatabase& database, const AssetGuid& assetGuid, std::string& outMessage,
                                       bool discardUnsaved) {
    if (document_.GetAssetGuid() == assetGuid && document_.IsOpen()) {
        outMessage = "VFX asset is already open";
        statusMessage_ = outMessage;
        return true;
    }
    if (!document_.OpenAsset(database, assetGuid, outMessage, discardUnsaved)) {
        statusMessage_ = outMessage;
        return false;
    }
    ClearPendingAssetOpen();
    preview_.StopPreview();
    timelineEdit_ = {};
    viewportHandle_ = {};
    layerGizmoEditing_ = false;
    preview_.previewLoopRangeInitialized_ = false;
    ResetAssetRefreshStatus();
    selection_ = {};
    if (!document_.Asset().emitters.empty()) {
        const auto& first = document_.Asset().emitters.front();
        Select({VfxWorkspaceSelectionKind::Emitter, first.id, first.id});
    }
    selectLayersTab_ = true;
    preview_.previewInputOverrides_.clear();
    OnDocumentChanged();
    (void)preview_.FocusPreviewAsset();
    const std::string openMessage = outMessage;
    std::string previewMessage{};
    if (preview_.PlayPreview(previewMessage)) {
        outMessage = openMessage + "; preview playing";
    } else if (!previewMessage.empty()) {
        outMessage = openMessage + "; preview unavailable: " + previewMessage;
    }
    statusMessage_ = outMessage;
    return true;
}

bool VfxWorkspaceController::RequestOpenAsset(AssetDatabase& database, const AssetGuid& assetGuid,
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
    if (document_.IsOpen() && document_.GetAssetGuid() == assetGuid) {
        outMessage = "VFX asset is already open";
        statusMessage_ = outMessage;
        return true;
    }
    if (!document_.IsDirty()) {
        return OpenAsset(database, assetGuid, outMessage);
    }
    if (pendingAssetOpenGuid_.IsValid()) {
        if (pendingAssetOpenGuid_ == assetGuid) {
            outMessage = "Waiting for the unsaved VFX decision";
            statusMessage_ = outMessage;
            return true;
        }
        outMessage = "Finish the pending Save / Discard / Cancel decision first";
        statusMessage_ = outMessage;
        return false;
    }

    pendingAssetOpenGuid_ = assetGuid;
    pendingAssetOpenPopupRequested_ = true;
    outMessage = "Unsaved VFX changes need a Save / Discard / Cancel decision";
    statusMessage_ = outMessage;
    return true;
}

void VfxWorkspaceController::ClearPendingAssetOpen() noexcept {
    pendingAssetOpenGuid_ = {};
    pendingAssetOpenPopupRequested_ = false;
}

bool VfxWorkspaceController::Save(AssetDatabase& database, std::string& outMessage) {
    EndTimelineEdit(false);
    ResetAssetRefreshStatus();
    const bool saved = document_.Save(database, outMessage);
    if (!saved) {
        statusMessage_ = outMessage;
        return false;
    }
    if (assetRefreshCallback_) {
        lastAssetRefreshAttempted_ = true;
        lastAssetRefreshSucceeded_ = assetRefreshCallback_(document_.GetAssetGuid(), lastAssetRefreshMessage_);
        if (!lastAssetRefreshSucceeded_) {
            outMessage +=
                lastAssetRefreshMessage_.empty() ? "; runtime asset refresh failed" : "; " + lastAssetRefreshMessage_;
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

bool VfxWorkspaceController::RefreshAuthoredAsset(const AssetGuid& assetGuid, std::string& outMessage) {
    ResetAssetRefreshStatus();
    if (!assetGuid.IsValid()) {
        outMessage = "Authored asset GUID is invalid";
        return false;
    }
    if (!assetRefreshCallback_) {
        outMessage = "Live asset refresh is unavailable until the VFX asset is saved";
        lastAssetRefreshMessage_ = outMessage;
        return false;
    }
    lastAssetRefreshAttempted_ = true;
    lastAssetRefreshSucceeded_ = assetRefreshCallback_(assetGuid, lastAssetRefreshMessage_);
    outMessage = lastAssetRefreshMessage_;
    if (outMessage.empty()) {
        outMessage = lastAssetRefreshSucceeded_ ? "Authored asset refreshed" : "Authored asset refresh failed";
    }
    return lastAssetRefreshSucceeded_;
}

bool VfxWorkspaceController::Revert(const AssetDatabase& database, std::string& outMessage) {
    EndTimelineEdit(false);
    preview_.StopPreview();
    if (!document_.Revert(database, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    preview_.previewLoopRangeInitialized_ = false;
    ResetAssetRefreshStatus();
    preview_.previewInputOverrides_.clear();
    OnDocumentChanged();
    (void)preview_.FocusPreviewAsset();
    const std::string revertMessage = outMessage;
    std::string previewMessage{};
    if (preview_.PlayPreview(previewMessage)) {
        outMessage = revertMessage + "; preview playing";
    } else if (!previewMessage.empty()) {
        outMessage = revertMessage + "; preview unavailable: " + previewMessage;
    }
    statusMessage_ = outMessage;
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

void VfxWorkspaceController::Select(VfxWorkspaceSelection selection) {
    selection_ = std::move(selection);
    NormalizeSelection();
}

const VfxWorkspaceSelection& VfxWorkspaceController::GetSelection() const noexcept { return selection_; }

const VfxEditorDocument& VfxWorkspaceController::GetDocument() const noexcept { return document_; }

bool VfxWorkspaceController::RestoreRecoveryDocument(const AssetDatabase& database, const nlohmann::json& snapshot,
                                                     std::string& message) {
    if (!document_.RestoreRecovery(database, snapshot, message))
        return false;
    OnDocumentChanged();
    return true;
}

const VFX::VfxAssetData* VfxWorkspaceController::GetAsset() const noexcept {
    return document_.IsOpen() ? &document_.Asset() : nullptr;
}

const std::vector<VFX::VfxDiagnostic>& VfxWorkspaceController::GetDiagnostics() const noexcept {
    return preview_.diagnostics_;
}

const std::string& VfxWorkspaceController::GetStatusMessage() const noexcept { return statusMessage_; }

bool VfxWorkspaceController::WasLastAssetRefreshAttempted() const noexcept { return lastAssetRefreshAttempted_; }

bool VfxWorkspaceController::DidLastAssetRefreshSucceed() const noexcept { return lastAssetRefreshSucceeded_; }

const std::string& VfxWorkspaceController::GetLastAssetRefreshMessage() const noexcept {
    return lastAssetRefreshMessage_;
}

} // namespace HIKARI::EDITOR
