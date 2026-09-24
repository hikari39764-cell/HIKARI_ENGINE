#include "Editor/Documents/HIKARI_RuntimeBindingEditorDocument.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "Runtime/Binding/HIKARI_RuntimeBindingCatalogJson.h"

namespace HIKARI::EDITOR {
namespace {

bool AreVariablesEquivalent(const BINDING::VariableDefinition& left,
                            const BINDING::VariableDefinition& right) noexcept {
    return left.id == right.id && left.name == right.name && left.description == right.description &&
           left.defaultValue == right.defaultValue && left.hasRange == right.hasRange &&
           left.minimum == right.minimum && left.maximum == right.maximum && left.step == right.step;
}

bool AreEventsEquivalent(const BINDING::EventDefinition& left, const BINDING::EventDefinition& right) noexcept {
    return left.id == right.id && left.name == right.name && left.description == right.description &&
           left.hasPayload == right.hasPayload && left.payloadType == right.payloadType;
}

} // namespace

bool RuntimeBindingEditorDocument::OpenProject(const std::filesystem::path& projectRoot, std::string& outMessage) {
    if (projectRoot.empty()) {
        outMessage = "Project root is empty";
        return false;
    }

    const std::filesystem::path normalizedRoot = projectRoot.lexically_normal();
    if (IsForProject(normalizedRoot)) {
        outMessage.clear();
        return true;
    }
    if (IsDirty()) {
        outMessage = "Current runtime binding catalog has unsaved changes";
        return false;
    }

    const std::filesystem::path path = BINDING::GetCatalogPath(normalizedRoot);
    BINDING::Catalog loaded{};
    if (std::filesystem::exists(path)) {
        if (!BINDING::LoadCatalog(path, loaded, outMessage)) {
            return false;
        }
        outMessage = "Runtime binding catalog opened";
    } else {
        loaded = {};
        outMessage = "New runtime binding catalog";
    }

    projectRoot_ = normalizedRoot;
    catalogPath_ = path;
    sourceVersion_.Accept(path);
    catalog_ = std::move(loaded);
    open_ = true;
    ResetHistory();
    return true;
}

bool RuntimeBindingEditorDocument::Save(std::string& outMessage) {
    if (!open_ || catalogPath_.empty()) {
        outMessage = "Runtime binding catalog is not open";
        return false;
    }
    if (!sourceVersion_.CanSave(outMessage))
        return false;
    if (!BINDING::SaveCatalog(catalogPath_, catalog_, outMessage)) {
        return false;
    }
    sourceVersion_.Accept(catalogPath_);
    history_.MarkSaved();
    outMessage = "Runtime binding catalog saved";
    return true;
}

bool RuntimeBindingEditorDocument::ApplyCatalog(BINDING::Catalog catalog, std::string& outMessage,
                                                uint64_t mergeGroup) {
    if (!open_) {
        outMessage = "Runtime binding catalog is not open";
        return false;
    }
    const std::vector<BINDING::CatalogDiagnostic> diagnostics = BINDING::ValidateCatalog(catalog);
    if (BINDING::HasCatalogErrors(diagnostics)) {
        outMessage = diagnostics.front().message;
        if (!diagnostics.front().subjectId.empty()) {
            outMessage += " [" + diagnostics.front().subjectId + "]";
        }
        return false;
    }
    if (AreEquivalent(catalog_, catalog)) {
        outMessage = "No runtime binding changes";
        return true;
    }

    BINDING::Catalog before = catalog_;
    catalog_ = std::move(catalog);
    RecordApplied(std::move(before), mergeGroup);
    outMessage = "Runtime binding catalog updated";
    return true;
}

void RuntimeBindingEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool RuntimeBindingEditorDocument::Undo() { return open_ && history_.Undo(catalog_); }

bool RuntimeBindingEditorDocument::Redo() { return open_ && history_.Redo(catalog_); }

BINDING::Catalog& RuntimeBindingEditorDocument::Catalog() noexcept { return catalog_; }

const BINDING::Catalog& RuntimeBindingEditorDocument::Catalog() const noexcept { return catalog_; }

const std::filesystem::path& RuntimeBindingEditorDocument::GetProjectRoot() const noexcept { return projectRoot_; }

const std::filesystem::path& RuntimeBindingEditorDocument::GetCatalogPath() const noexcept { return catalogPath_; }

bool RuntimeBindingEditorDocument::IsOpen() const noexcept { return open_; }

bool RuntimeBindingEditorDocument::IsForProject(const std::filesystem::path& projectRoot) const noexcept {
    return open_ && projectRoot_ == projectRoot.lexically_normal();
}

bool RuntimeBindingEditorDocument::IsDirty() const noexcept { return open_ && history_.IsDirty(); }

bool RuntimeBindingEditorDocument::CanUndo() const noexcept { return open_ && history_.CanUndo(); }

bool RuntimeBindingEditorDocument::CanRedo() const noexcept { return open_ && history_.CanRedo(); }

bool RuntimeBindingEditorDocument::AreEquivalent(const BINDING::Catalog& left, const BINDING::Catalog& right) noexcept {
    return left.version == right.version && left.variables.size() == right.variables.size() &&
           left.events.size() == right.events.size() &&
           std::equal(left.variables.begin(), left.variables.end(), right.variables.begin(), AreVariablesEquivalent) &&
           std::equal(left.events.begin(), left.events.end(), right.events.begin(), AreEventsEquivalent);
}

void RuntimeBindingEditorDocument::RecordApplied(BINDING::Catalog before, uint64_t mergeGroup) {
    history_.Record(std::move(before), catalog_, mergeGroup);
}

void RuntimeBindingEditorDocument::ResetHistory() noexcept { history_.Reset(true); }

} // namespace HIKARI::EDITOR
