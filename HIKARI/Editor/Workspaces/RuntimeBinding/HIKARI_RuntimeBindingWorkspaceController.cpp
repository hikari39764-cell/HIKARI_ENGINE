#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool RuntimeBindingWorkspaceController::Activate(DocumentSceneBase& scene, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    NormalizeSelection();
    if (selectionKind_ == SelectionKind::None) {
        if (!document_.Catalog().variables.empty()) {
            SelectItem(document_.Catalog().variables.front().id);
        } else if (!document_.Catalog().events.empty()) {
            SelectItem(document_.Catalog().events.front().id);
        }
    }
    statusMessage_ = outMessage;
    return true;
}

void RuntimeBindingWorkspaceController::BindDocumentCommands(EditorCommandRouter& commandRouter) const {
    commandRouter.BindOperation(EditorCommandId::SaveDocument, "Save Runtime Bindings", IsDocumentOpen(),
                                "binding.save");
    commandRouter.BindOperation(EditorCommandId::Undo, "Undo Runtime Binding Edit", CanUndo(), "binding.undo");
    commandRouter.BindOperation(EditorCommandId::Redo, "Redo Runtime Binding Edit", CanRedo(), "binding.redo");
}

void RuntimeBindingWorkspaceController::SetReferenceProvider(ReferenceProvider provider) {
    referenceProvider_ = std::move(provider);
}

void RuntimeBindingWorkspaceController::SetReferenceRevealCallback(ReferenceRevealCallback callback) {
    referenceRevealCallback_ = std::move(callback);
}

bool RuntimeBindingWorkspaceController::IsDocumentOpen() const noexcept { return document_.IsOpen(); }

bool RuntimeBindingWorkspaceController::IsDocumentDirty() const noexcept { return document_.IsDirty(); }

bool RuntimeBindingWorkspaceController::CanUndo() const noexcept { return document_.CanUndo(); }

bool RuntimeBindingWorkspaceController::CanRedo() const noexcept { return document_.CanRedo(); }

bool RuntimeBindingWorkspaceController::Save(DocumentSceneBase& scene, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage) || !document_.Save(outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    if (!scene.ReloadRuntimeBindingCatalog(outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    scene.GetRuntimeBindingStore().PublishSnapshot();
    statusMessage_ = "Runtime binding catalog saved";
    outMessage = statusMessage_;
    return true;
}

bool RuntimeBindingWorkspaceController::Undo(DocumentSceneBase& scene, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        return false;
    }
    if (!document_.Undo()) {
        outMessage = "Nothing to undo";
        statusMessage_ = outMessage;
        return false;
    }
    NormalizeSelection();
    InvalidateInspectorDraft();
    if (!ProjectCatalog(scene, outMessage)) {
        document_.Redo();
        statusMessage_ = outMessage;
        return false;
    }
    outMessage = "Runtime binding edit undone";
    statusMessage_ = outMessage;
    return true;
}

bool RuntimeBindingWorkspaceController::Redo(DocumentSceneBase& scene, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        return false;
    }
    if (!document_.Redo()) {
        outMessage = "Nothing to redo";
        statusMessage_ = outMessage;
        return false;
    }
    NormalizeSelection();
    InvalidateInspectorDraft();
    if (!ProjectCatalog(scene, outMessage)) {
        document_.Undo();
        statusMessage_ = outMessage;
        return false;
    }
    outMessage = "Runtime binding edit redone";
    statusMessage_ = outMessage;
    return true;
}

const BINDING::Catalog& RuntimeBindingWorkspaceController::GetCatalog() const noexcept { return document_.Catalog(); }

std::vector<BINDING::CatalogDiagnostic> RuntimeBindingWorkspaceController::GetDiagnostics() const {
    return BINDING::ValidateCatalog(document_.Catalog());
}

std::vector<RuntimeBindingReference> RuntimeBindingWorkspaceController::GetReferences(std::string_view itemId) const {
    return referenceProvider_ ? referenceProvider_(itemId) : std::vector<RuntimeBindingReference>{};
}

bool RuntimeBindingWorkspaceController::IsReferenceIndexAvailable() const noexcept {
    return static_cast<bool>(referenceProvider_);
}

const std::string& RuntimeBindingWorkspaceController::GetStatusMessage() const noexcept { return statusMessage_; }

void RuntimeBindingWorkspaceController::SelectItem(std::string itemId) {
    selectedItemId_ = std::move(itemId);
    if (document_.Catalog().FindVariable(selectedItemId_) != nullptr) {
        selectionKind_ = SelectionKind::Variable;
    } else if (document_.Catalog().FindEvent(selectedItemId_) != nullptr) {
        selectionKind_ = SelectionKind::Event;
    } else {
        selectionKind_ = SelectionKind::None;
        selectedItemId_.clear();
    }
    InvalidateInspectorDraft();
}

bool RuntimeBindingWorkspaceController::EnsureDocumentOpen(DocumentSceneBase& scene, std::string& outMessage) {
    const std::filesystem::path& projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    const bool alreadyOpen = document_.IsForProject(projectRoot);
    if (!document_.OpenProject(projectRoot, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    if (!alreadyOpen && !ProjectCatalog(scene, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    return true;
}

bool RuntimeBindingWorkspaceController::ApplyCatalog(DocumentSceneBase& scene, BINDING::Catalog catalog,
                                                     std::string& outMessage, uint64_t mergeGroup) {
    if (!document_.ApplyCatalog(std::move(catalog), outMessage, mergeGroup)) {
        statusMessage_ = outMessage;
        return false;
    }
    if (!ProjectCatalog(scene, outMessage)) {
        statusMessage_ = outMessage;
        return false;
    }
    InvalidateInspectorDraft();
    return true;
}

bool RuntimeBindingWorkspaceController::ProjectCatalog(DocumentSceneBase& scene, std::string& outMessage) {
    BINDING::RuntimeBindingStore& store = scene.GetRuntimeBindingStore();
    if (!store.ReloadCatalog(document_.Catalog())) {
        outMessage = "Runtime binding catalog projection failed";
        return false;
    }
    std::erase_if(previewVariableIds_,
                  [&store](const std::string& id) { return store.GetWriter(id) != kPreviewWriterId; });
    store.PublishSnapshot();
    outMessage.clear();
    return true;
}

std::string RuntimeBindingWorkspaceController::AllocateStableId(std::string_view prefix) const {
    for (;;) {
        const std::string candidate = std::string(prefix) + "." + GenerateAssetGuid().value;
        if (!ContainsId(candidate)) {
            return candidate;
        }
    }
}

bool RuntimeBindingWorkspaceController::ContainsId(std::string_view id) const noexcept {
    return document_.Catalog().FindVariable(id) != nullptr || document_.Catalog().FindEvent(id) != nullptr;
}

void RuntimeBindingWorkspaceController::NormalizeSelection() noexcept {
    if (ContainsId(selectedItemId_)) {
        return;
    }
    selectedItemId_.clear();
    selectionKind_ = SelectionKind::None;
    if (!document_.Catalog().variables.empty()) {
        selectedItemId_ = document_.Catalog().variables.front().id;
        selectionKind_ = SelectionKind::Variable;
    } else if (!document_.Catalog().events.empty()) {
        selectedItemId_ = document_.Catalog().events.front().id;
        selectionKind_ = SelectionKind::Event;
    }
    InvalidateInspectorDraft();
}

BINDING::VariableDefinition* RuntimeBindingWorkspaceController::SelectedVariable() noexcept {
    return selectionKind_ == SelectionKind::Variable ? document_.Catalog().FindVariable(selectedItemId_) : nullptr;
}

const BINDING::VariableDefinition* RuntimeBindingWorkspaceController::SelectedVariable() const noexcept {
    return selectionKind_ == SelectionKind::Variable ? document_.Catalog().FindVariable(selectedItemId_) : nullptr;
}

BINDING::EventDefinition* RuntimeBindingWorkspaceController::SelectedEvent() noexcept {
    return selectionKind_ == SelectionKind::Event ? document_.Catalog().FindEvent(selectedItemId_) : nullptr;
}

const BINDING::EventDefinition* RuntimeBindingWorkspaceController::SelectedEvent() const noexcept {
    return selectionKind_ == SelectionKind::Event ? document_.Catalog().FindEvent(selectedItemId_) : nullptr;
}

void RuntimeBindingWorkspaceController::InvalidateInspectorDraft() noexcept {
    inspectorDraftItemId_.clear();
    previewDraftItemId_.clear();
}

nlohmann::json RuntimeBindingWorkspaceController::DescribeDocumentState() const {
    return {{"open", IsDocumentOpen()}, {"dirty", IsDocumentDirty()}};
}

} // namespace HIKARI::EDITOR
