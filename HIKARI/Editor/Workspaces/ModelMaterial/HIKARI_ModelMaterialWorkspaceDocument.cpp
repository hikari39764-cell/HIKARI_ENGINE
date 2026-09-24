#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceDocument::OpenBindings(const AssetGuid& modelGuid, const AssetGuid& assetGuid,
                                                  std::filesystem::path sourcePath, BindingMap bindings, bool saved) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    modelGuid_ = modelGuid;
    bindingAssetGuid_ = assetGuid;
    bindingSourcePath_ = std::move(sourcePath);
    bindingSourceVersion_.Accept(bindingSourcePath_);
    bindings_ = std::move(bindings);
    savedBindings_ = bindings_;
    bindingsOpen_ = modelGuid_.IsValid();
    bindingHistory_.Reset(saved);
    activeHistoryDomain_ = ChangedDomain::Binding;
}

void ModelMaterialWorkspaceDocument::OpenMaterial(const AssetGuid& assetGuid, std::filesystem::path sourcePath,
                                                  MaterialAssetData source, const MaterialCompileResult& compile,
                                                  bool saved) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    materialAssetGuid_ = assetGuid;
    materialSourcePath_ = std::move(sourcePath);
    materialSourceVersion_.Accept(materialSourcePath_);
    material_ = std::move(source);
    savedMaterial_ = material_;
    compile_ = compile;
    materialOpen_ = true;
    materialRevision_ = 1u;
    compileRevision_ = 1u;
    materialPreviewPending_ = true;
    previewEdit_.reset();
    materialHistory_.Reset(saved);
    activeHistoryDomain_ = ChangedDomain::Material;
}

void ModelMaterialWorkspaceDocument::SetMaterialCompileOptions(MaterialCompileOptions options) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    compileOptions_ = std::move(options);
    if (materialOpen_) {
        RefreshCompile();
    }
}

void ModelMaterialWorkspaceDocument::CloseMaterial() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    materialAssetGuid_ = {};
    materialSourcePath_.clear();
    material_ = {};
    savedMaterial_ = {};
    compile_ = {};
    previewEdit_.reset();
    materialOpen_ = false;
    materialRevision_ = 0u;
    compileRevision_ = 0u;
    materialPreviewPending_ = false;
    materialHistory_.Reset();
    activeHistoryDomain_ = bindingsOpen_ ? ChangedDomain::Binding : ChangedDomain::None;
}

bool ModelMaterialWorkspaceDocument::HasBindings() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindingsOpen_;
}

bool ModelMaterialWorkspaceDocument::HasMaterial() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialOpen_;
}

const AssetGuid& ModelMaterialWorkspaceDocument::GetModelGuid() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return modelGuid_;
}

const AssetGuid& ModelMaterialWorkspaceDocument::GetBindingAssetGuid() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindingAssetGuid_;
}

const AssetGuid& ModelMaterialWorkspaceDocument::GetMaterialAssetGuid() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialAssetGuid_;
}

const std::filesystem::path& ModelMaterialWorkspaceDocument::GetBindingSourcePath() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindingSourcePath_;
}

const std::filesystem::path& ModelMaterialWorkspaceDocument::GetMaterialSourcePath() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialSourcePath_;
}

ModelMaterialWorkspaceDocument::BindingMap& ModelMaterialWorkspaceDocument::EditBindings() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindings_;
}

ModelMaterialWorkspaceDocument::BindingMap ModelMaterialWorkspaceDocument::GetBindings() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindings_;
}

MaterialAssetData& ModelMaterialWorkspaceDocument::EditMaterial() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return material_;
}

MaterialAssetData ModelMaterialWorkspaceDocument::GetMaterial() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return material_;
}

MaterialCompileResult ModelMaterialWorkspaceDocument::GetCompileResult() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return compile_;
}

uint64_t ModelMaterialWorkspaceDocument::GetMaterialRevision() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialRevision_;
}

uint64_t ModelMaterialWorkspaceDocument::GetCompileRevision() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return compileRevision_;
}

bool ModelMaterialWorkspaceDocument::IsCompileCurrent() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialOpen_ && compileRevision_ == materialRevision_;
}

bool ModelMaterialWorkspaceDocument::CommitBindings(BindingMap before, std::string label) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bindingsOpen_ || AreBindingsEquivalent(before, bindings_))
        return false;
    bindingHistory_.Record(std::move(before), bindings_, 0, {ChangedDomain::Binding, std::move(label)});
    activeHistoryDomain_ = ChangedDomain::Binding;
    return true;
}

bool ModelMaterialWorkspaceDocument::CommitMaterial(MaterialAssetData before, std::string label) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (HasMaterialGesture()) {
        RecordMaterialChange(previewEdit_->before, before, std::move(previewEdit_->label));
        previewEdit_.reset();
    }
    return CommitMaterialLocked(std::move(before), std::move(label));
}

void ModelMaterialWorkspaceDocument::RestoreMaterial(MaterialAssetData source) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!materialOpen_) {
        return;
    }
    MaterialAssetData before = std::move(material_);
    material_ = std::move(source);
    RefreshAfterMaterialChange(!AreMaterialContentsEquivalent(before, material_));
}

bool ModelMaterialWorkspaceDocument::CanUndo() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (previewEdit_)
        return false;
    if (activeHistoryDomain_ == ChangedDomain::Binding) {
        return bindingHistory_.CanUndo();
    }
    return activeHistoryDomain_ == ChangedDomain::Material && materialHistory_.CanUndo();
}

bool ModelMaterialWorkspaceDocument::CanRedo() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (previewEdit_)
        return false;
    if (activeHistoryDomain_ == ChangedDomain::Binding) {
        return bindingHistory_.CanRedo();
    }
    return activeHistoryDomain_ == ChangedDomain::Material && materialHistory_.CanRedo();
}

bool ModelMaterialWorkspaceDocument::Undo(ChangedDomain& outDomain, std::string& outLabel) {
    return TravelHistory(false, outDomain, outLabel);
}

bool ModelMaterialWorkspaceDocument::Redo(ChangedDomain& outDomain, std::string& outLabel) {
    return TravelHistory(true, outDomain, outLabel);
}

bool ModelMaterialWorkspaceDocument::TravelHistory(bool redo, ChangedDomain& domain, std::string& label) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (redo ? !CanRedo() : !CanUndo())
        return false;
    HistoryMetadata metadata;
    if (activeHistoryDomain_ == ChangedDomain::Binding) {
        if (redo)
            bindingHistory_.Redo(bindings_, &metadata);
        else
            bindingHistory_.Undo(bindings_, &metadata);
    } else {
        auto before = material_;
        if (redo)
            materialHistory_.Redo(material_, &metadata);
        else
            materialHistory_.Undo(material_, &metadata);
        RefreshAfterMaterialChange(!AreMaterialContentsEquivalent(before, material_));
    }
    domain = metadata.domain;
    label = std::move(metadata.label);
    return true;
}

const std::string* ModelMaterialWorkspaceDocument::GetUndoLabel() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!CanUndo()) {
        return nullptr;
    }
    return activeHistoryDomain_ == ChangedDomain::Binding ? &bindingHistory_.PeekUndo()->metadata.label
                                                          : &materialHistory_.PeekUndo()->metadata.label;
}

const std::string* ModelMaterialWorkspaceDocument::GetRedoLabel() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!CanRedo()) {
        return nullptr;
    }
    return activeHistoryDomain_ == ChangedDomain::Binding ? &bindingHistory_.PeekRedo()->metadata.label
                                                          : &materialHistory_.PeekRedo()->metadata.label;
}

bool ModelMaterialWorkspaceDocument::IsBindingDirty() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bindingsOpen_ && bindingHistory_.IsDirty();
}

bool ModelMaterialWorkspaceDocument::IsMaterialDirty() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return materialOpen_ && materialHistory_.IsDirty();
}

bool ModelMaterialWorkspaceDocument::DiscardBindingChanges() noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!bindingsOpen_ || !IsBindingDirty()) {
        return false;
    }
    bindings_ = savedBindings_;
    bindingHistory_.Reset();
    activeHistoryDomain_ = ChangedDomain::Binding;
    return true;
}

bool ModelMaterialWorkspaceDocument::DiscardMaterialChanges() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!materialOpen_ || (!IsMaterialDirty() && !previewEdit_)) {
        return false;
    }
    MaterialAssetData before = std::move(material_);
    material_ = savedMaterial_;
    previewEdit_.reset();
    materialHistory_.Reset();
    activeHistoryDomain_ = ChangedDomain::Material;
    RefreshAfterMaterialChange(!AreMaterialContentsEquivalent(before, material_));
    return true;
}

void ModelMaterialWorkspaceDocument::MarkBindingsSaved(const AssetGuid& assetGuid,
                                                       std::filesystem::path sourcePath) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    bindingAssetGuid_ = assetGuid;
    bindingSourcePath_ = std::move(sourcePath);
    bindingSourceVersion_.Accept(bindingSourcePath_);
    bindingHistory_.MarkSaved();
    savedBindings_ = bindings_;
}

void ModelMaterialWorkspaceDocument::MarkMaterialSaved(const AssetGuid& assetGuid,
                                                       std::filesystem::path sourcePath) noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    materialAssetGuid_ = assetGuid;
    materialSourcePath_ = std::move(sourcePath);
    materialSourceVersion_.Accept(materialSourcePath_);
    materialHistory_.MarkSaved();
    savedMaterial_ = material_;
}

bool ModelMaterialWorkspaceDocument::BeginAbPreview(MaterialAssetData candidate, std::string& outMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!materialOpen_) {
        outMessage = "open or create a material before starting A/B preview";
        return false;
    }
    if (previewEdit_) {
        outMessage = "finish the active A/B preview first";
        return false;
    }
    previewEdit_ = PreviewEdit{material_, "Keep A/B material", true};
    material_ = std::move(candidate);
    RefreshAfterMaterialChange(!AreMaterialContentsEquivalent(previewEdit_->before, material_));
    outMessage = "B variant is preview-only; Keep commits one undo step";
    return true;
}

bool ModelMaterialWorkspaceDocument::IsAbPreviewActive() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return previewEdit_ && previewEdit_->audition;
}

bool ModelMaterialWorkspaceDocument::KeepAbPreview(std::string& outMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!IsAbPreviewActive()) {
        outMessage = "no A/B preview is active";
        return false;
    }
    MaterialAssetData before = std::move(previewEdit_->before);
    previewEdit_.reset();
    const bool changed = CommitMaterial(std::move(before), "Keep A/B material");
    outMessage = changed ? "kept B variant as one material edit" : "B variant matches the authored material";
    return changed;
}

bool ModelMaterialWorkspaceDocument::DiscardAbPreview(std::string& outMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!IsAbPreviewActive()) {
        outMessage = "no A/B preview is active";
        return false;
    }
    MaterialAssetData before = std::move(material_);
    material_ = std::move(previewEdit_->before);
    previewEdit_.reset();
    RefreshAfterMaterialChange(!AreMaterialContentsEquivalent(before, material_));
    outMessage = "discarded B variant without changing the asset";
    return true;
}

bool ModelMaterialWorkspaceDocument::AreBindingsEquivalent(const BindingMap& left, const BindingMap& right) noexcept {
    return left == right;
}

bool ModelMaterialWorkspaceDocument::AreMaterialsEquivalent(const MaterialAssetData& left,
                                                            const MaterialAssetData& right) {
    std::string leftText{};
    std::string rightText{};
    std::string error{};
    return MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(left, leftText, error) &&
           MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(right, rightText, error) && leftText == rightText;
}

void ModelMaterialWorkspaceDocument::RefreshCompile() {
    // Material v6 authoring is graph-authoritative. A failed edit must remain a
    // failed current graph; projecting any stale program here would make the
    // viewport look live while it is actually rendering an older topology.
    materialPreviewPending_ = true;
    compile_ = CompileMaterialAsset(material_, compileOptions_);
    compileRevision_ = materialRevision_;
}

} // namespace HIKARI::EDITOR
