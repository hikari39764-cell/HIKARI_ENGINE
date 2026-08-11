#include "Editor/Documents/HIKARI_VfxEditorDocument.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_VfxAssetAuthoringService.h"

namespace HIKARI::EDITOR {
    namespace {

        void MaterializePendingPresetState(
            VFX::VfxAssetData& targetAsset,
            std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>&
                pendingMaterials,
            const VFX::VfxAssetData& authored) {

            for (const VFX::PRESETS::VfxPresetMaterialRecipe& recipe :
                 pendingMaterials) {
                const auto emitter = std::find_if(
                    targetAsset.emitters.begin(),
                    targetAsset.emitters.end(),
                    [&recipe](const VFX::VfxEmitterData& candidate) {
                        return candidate.id == recipe.emitterId;
                    });
                const auto source = std::find_if(
                    authored.emitters.begin(),
                    authored.emitters.end(),
                    [&recipe](const VFX::VfxEmitterData& candidate) {
                        return candidate.id == recipe.emitterId;
                    });
                if (emitter != targetAsset.emitters.end() &&
                    source != authored.emitters.end() &&
                    source->renderCommon.materialAssetGuid.IsValid()) {
                    emitter->renderCommon.materialAssetGuid =
                        source->renderCommon.materialAssetGuid;
                }
            }
            pendingMaterials.clear();
        }

    } // namespace

    bool VfxEditorDocument::OpenAsset(
        const AssetDatabase& database,
        const AssetGuid& assetGuid,
        std::string& outMessage,
        bool discardUnsaved) {

        if (IsDirty() && !discardUnsaved) {
            outMessage = "Current VFX document has unsaved changes";
            return false;
        }
        VFX::VfxAssetData loaded{};
        if (!LoadVfxForAuthoring(
                database, assetGuid, loaded, outMessage)) {
            return false;
        }
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::VfxEffect) {
            outMessage = "VFX asset record disappeared while opening";
            return false;
        }
        asset_ = std::move(loaded);
        pendingPresetMaterials_.clear();
        source_ = VfxEditorDocumentSource::Asset;
        assetGuid_ = record->guid;
        sourcePath_ = record->sourcePath;
        displayName_ = record->displayName.empty()
            ? sourcePath_.stem().string()
            : record->displayName;
        ResetHistory(true);
        AdvanceRevision();
        outMessage = "Opened VFX asset: " + displayName_;
        return true;
    }

    bool VfxEditorDocument::Save(
        AssetDatabase& database,
        std::string& outMessage) {

        if (!IsOpen()) {
            outMessage = "No VFX document is open";
            return false;
        }
        const VfxAssetAuthoringResult result =
            source_ == VfxEditorDocumentSource::Transient
                ? (pendingPresetMaterials_.empty()
                    ? CreateVfxAsset(database, asset_)
                    : CreateVfxPresetAsset(
                        database, asset_, pendingPresetMaterials_))
                : UpdateVfxAsset(database, assetGuid_, asset_);
        outMessage = result.message;
        if (!result.assetGuid.IsValid()) {
            return false;
        }
        assetGuid_ = result.assetGuid;
        sourcePath_ = result.sourcePath;
        source_ = VfxEditorDocumentSource::Asset;
        if (result.authoredAsset) {
            for (HistoryEntry& entry : history_) {
                MaterializePendingPresetState(
                    entry.before.asset,
                    entry.before.pendingPresetMaterials,
                    *result.authoredAsset);
                MaterializePendingPresetState(
                    entry.after.asset,
                    entry.after.pendingPresetMaterials,
                    *result.authoredAsset);
            }
            asset_ = *result.authoredAsset;
            AdvanceRevision();
        }
        pendingPresetMaterials_.clear();
        displayName_ = asset_.name;
        if (!result.success) {
            // Keep the source identity after an importer failure so a retry
            // updates the same file instead of creating a suffixed duplicate.
            return false;
        }
        savedHistoryCursor_ = historyCursor_;
        return true;
    }

    bool VfxEditorDocument::Revert(
        const AssetDatabase& database,
        std::string& outMessage) {

        if (source_ != VfxEditorDocumentSource::Asset ||
            !assetGuid_.IsValid()) {
            outMessage = "Only a saved VFX asset can be reverted";
            return false;
        }
        const AssetGuid assetGuid = assetGuid_;
        return OpenAsset(database, assetGuid, outMessage, true);
    }


} // namespace HIKARI::EDITOR
