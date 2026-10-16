#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <utility>

#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

namespace HIKARI::EDITOR {
namespace {

std::string FirstValidationError(const std::vector<VFX::VfxDiagnostic>& diagnostics) {
    const auto found = std::find_if(diagnostics.begin(), diagnostics.end(), [](const VFX::VfxDiagnostic& diagnostic) {
        return diagnostic.severity == VFX::VfxDiagnosticSeverity::Error;
    });
    if (found == diagnostics.end()) {
        return {};
    }
    std::string message = found->message;
    if (!found->subjectId.empty()) {
        message += " [" + found->subjectId + "]";
    }
    return message;
}

} // namespace

void VfxEditorDocument::NewAsset(std::string name) {
    asset_ = VFX::MakeDefaultVfxAssetData();
    auto& layer = asset_.emitters.front();
    layer.kind = VFX::VfxLayerKind::Shape;
    layer.name = "Shape";
    layer.renderShape.billboardMode = VFX::VfxBillboardMode::Fixed;
    layer.spawn.ratePerSecond = 0;
    layer.spawn.burstTimeline.clear();
    layer.renderCommon.colorTextureAssetGuid = {};
    if (!name.empty()) {
        asset_.name = std::move(name);
    }
    source_ = VfxEditorDocumentSource::Transient;
    assetGuid_ = {};
    sourcePath_.clear();
    displayName_ = asset_.name;
    ResetHistory(false);
    NUMERIC::AdvanceNonZero(revision_);
}

bool VfxEditorDocument::ApplyPresetAsset(VFX::VfxAssetData package, std::string& outMessage) {
    if (source_ != VfxEditorDocumentSource::Transient) {
        outMessage = "VFX preset packages can only be applied to a transient document";
        return false;
    }
    return ApplyAsset(std::move(package), outMessage, 0u);
}

bool VfxEditorDocument::ApplyAsset(VFX::VfxAssetData asset, std::string& outMessage, uint64_t mergeGroup,
                                   VfxDocumentImpact impact) {
    if (!IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    const std::vector<VFX::VfxDiagnostic> diagnostics = VFX::ValidateVfxAsset(asset);
    if (VFX::HasVfxErrors(diagnostics)) {
        outMessage = FirstValidationError(diagnostics);
        return false;
    }
    if (AreEquivalent(asset_, asset)) {
        outMessage = "No VFX changes";
        return true;
    }

    auto before = std::move(asset_);
    asset_ = std::move(asset);
    displayName_ = asset_.name;
    lastImpact_ = impact;
    RecordApplied(std::move(before), mergeGroup, impact);
    NUMERIC::AdvanceNonZero(revision_);
    outMessage = "VFX document updated";
    return true;
}

void VfxEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool VfxEditorDocument::Undo() {
    if (!history_.Undo(asset_, &lastImpact_))
        return false;
    displayName_ = asset_.name;
    NUMERIC::AdvanceNonZero(revision_);
    return true;
}

bool VfxEditorDocument::Redo() {
    if (!history_.Redo(asset_, &lastImpact_))
        return false;
    displayName_ = asset_.name;
    NUMERIC::AdvanceNonZero(revision_);
    return true;
}

const AssetGuid& VfxEditorDocument::GetAssetGuid() const noexcept { return assetGuid_; }

const std::filesystem::path& VfxEditorDocument::GetSourcePath() const noexcept { return sourcePath_; }

const std::string& VfxEditorDocument::GetDisplayName() const noexcept { return displayName_; }

uint64_t VfxEditorDocument::GetRevision() const noexcept { return revision_; }

VfxEditorDocumentSource VfxEditorDocument::GetSource() const noexcept { return source_; }

bool VfxEditorDocument::IsOpen() const noexcept { return source_ != VfxEditorDocumentSource::None; }

bool VfxEditorDocument::IsDirty() const noexcept { return IsOpen() && history_.IsDirty(); }

bool VfxEditorDocument::CanUndo() const noexcept { return IsOpen() && history_.CanUndo(); }

bool VfxEditorDocument::CanRedo() const noexcept { return IsOpen() && history_.CanRedo(); }

bool VfxEditorDocument::AreEquivalent(const VFX::VfxAssetData& left, const VFX::VfxAssetData& right) {
    std::string leftText{};
    std::string rightText{};
    std::string error{};
    return VFX::SERIALIZATION::SerializeVfxSourceJson(left, leftText, error) &&
           VFX::SERIALIZATION::SerializeVfxSourceJson(right, rightText, error) && leftText == rightText;
}

void VfxEditorDocument::RecordApplied(VFX::VfxAssetData before, uint64_t mergeGroup, VfxDocumentImpact impact) {
    if (history_.CanMerge(mergeGroup) && history_.PeekUndo()->metadata == VfxDocumentImpact::Runtime) {
        impact = VfxDocumentImpact::Runtime;
    }
    history_.Record(std::move(before), asset_, mergeGroup, impact, AreEquivalent);
}

void VfxEditorDocument::ResetHistory(bool saved) noexcept {
    lastImpact_ = VfxDocumentImpact::Runtime;
    history_.Reset(saved);
}

} // namespace HIKARI::EDITOR
