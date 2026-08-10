#include "Editor/Documents/HIKARI_VfxEditorDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

namespace HIKARI::EDITOR {
    namespace {

        std::string FirstValidationError(
            const std::vector<VFX::VfxDiagnostic>& diagnostics) {

            const auto found = std::find_if(
                diagnostics.begin(), diagnostics.end(),
                [](const VFX::VfxDiagnostic& diagnostic) {
                    return diagnostic.severity ==
                        VFX::VfxDiagnosticSeverity::Error;
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
        if (!name.empty()) {
            asset_.name = std::move(name);
        }
        source_ = VfxEditorDocumentSource::Transient;
        assetGuid_ = {};
        sourcePath_.clear();
        displayName_ = asset_.name;
        ResetHistory(false);
        AdvanceRevision();
    }

    bool VfxEditorDocument::ApplyAsset(
        VFX::VfxAssetData asset,
        std::string& outMessage,
        uint64_t mergeGroup) {

        if (!IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        const std::vector<VFX::VfxDiagnostic> diagnostics =
            VFX::ValidateVfxAsset(asset);
        if (VFX::HasVfxErrors(diagnostics)) {
            outMessage = FirstValidationError(diagnostics);
            return false;
        }
        if (AreEquivalent(asset_, asset)) {
            outMessage = "No VFX changes";
            return true;
        }

        VFX::VfxAssetData before = asset_;
        asset_ = std::move(asset);
        displayName_ = asset_.name;
        RecordApplied(std::move(before), mergeGroup);
        AdvanceRevision();
        outMessage = "VFX document updated";
        return true;
    }

    void VfxEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0u;
    }

    bool VfxEditorDocument::Undo() {
        if (!CanUndo()) {
            return false;
        }
        --historyCursor_;
        asset_ = history_[historyCursor_].before;
        displayName_ = asset_.name;
        openMergeGroup_ = 0u;
        AdvanceRevision();
        return true;
    }

    bool VfxEditorDocument::Redo() {
        if (!CanRedo()) {
            return false;
        }
        asset_ = history_[historyCursor_].after;
        ++historyCursor_;
        displayName_ = asset_.name;
        openMergeGroup_ = 0u;
        AdvanceRevision();
        return true;
    }

    VFX::VfxAssetData& VfxEditorDocument::Asset() noexcept {
        return asset_;
    }

    const VFX::VfxAssetData& VfxEditorDocument::Asset() const noexcept {
        return asset_;
    }

    const AssetGuid& VfxEditorDocument::GetAssetGuid() const noexcept {
        return assetGuid_;
    }

    const std::filesystem::path&
        VfxEditorDocument::GetSourcePath() const noexcept {
        return sourcePath_;
    }

    const std::string& VfxEditorDocument::GetDisplayName() const noexcept {
        return displayName_;
    }

    uint64_t VfxEditorDocument::GetRevision() const noexcept {
        return revision_;
    }

    VfxEditorDocumentSource
        VfxEditorDocument::GetSource() const noexcept {
        return source_;
    }

    bool VfxEditorDocument::IsOpen() const noexcept {
        return source_ != VfxEditorDocumentSource::None;
    }

    bool VfxEditorDocument::IsDirty() const noexcept {
        return IsOpen() && (!savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_);
    }

    bool VfxEditorDocument::CanUndo() const noexcept {
        return IsOpen() && historyCursor_ > 0u;
    }

    bool VfxEditorDocument::CanRedo() const noexcept {
        return IsOpen() && historyCursor_ < history_.size();
    }

    bool VfxEditorDocument::AreEquivalent(
        const VFX::VfxAssetData& left,
        const VFX::VfxAssetData& right) {

        std::string leftText{};
        std::string rightText{};
        std::string error{};
        return VFX::SERIALIZATION::SerializeVfxSourceJson(
                left, leftText, error) &&
            VFX::SERIALIZATION::SerializeVfxSourceJson(
                right, rightText, error) &&
            leftText == rightText;
    }

    void VfxEditorDocument::RecordApplied(
        VFX::VfxAssetData before,
        uint64_t mergeGroup) {

        if (historyCursor_ < history_.size()) {
            if (savedHistoryCursor_ &&
                *savedHistoryCursor_ > historyCursor_) {
                savedHistoryCursor_.reset();
            }
            history_.erase(
                history_.begin() + historyCursor_,
                history_.end());
        }
        const bool merge = mergeGroup != 0u &&
            historyCursor_ > 0u &&
            historyCursor_ == history_.size() &&
            openMergeGroup_ == mergeGroup &&
            (!savedHistoryCursor_ ||
                *savedHistoryCursor_ != historyCursor_);
        if (merge) {
            history_.back().after = asset_;
            if (AreEquivalent(
                    history_.back().before,
                    history_.back().after)) {
                history_.pop_back();
                historyCursor_ = history_.size();
                openMergeGroup_ = 0u;
            }
            return;
        }
        history_.push_back({
            std::move(before), asset_, mergeGroup
        });
        historyCursor_ = history_.size();
        openMergeGroup_ = mergeGroup;
    }

    void VfxEditorDocument::ResetHistory(bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0u;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0u }
            : std::nullopt;
        openMergeGroup_ = 0u;
    }

    void VfxEditorDocument::AdvanceRevision() noexcept {
        ++revision_;
        if (revision_ == 0u) {
            revision_ = 1u;
        }
    }

} // namespace HIKARI::EDITOR
