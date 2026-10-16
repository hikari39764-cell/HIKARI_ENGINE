#include "Editor/Documents/HIKARI_VfxEditorDocument.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <utility>
#include <json.hpp>
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Authoring/HIKARI_VfxAssetAuthoringService.h"

namespace HIKARI::EDITOR {

nlohmann::json VfxEditorDocument::MakeRecovery() const {
    if (!IsOpen())
        return nullptr;
    std::string source, error;
    if (!VFX::SERIALIZATION::SerializeVfxSourceJson(asset_, source, error))
        throw std::runtime_error(error);
    return {{"assetGuid", assetGuid_.value},
            {"dirty", IsDirty()},
            {"sourceHash", sourceVersion_.GetFingerprint()},
            {"sourceReadable", sourceVersion_.IsReadable()},
            {"source", nlohmann::json::parse(source)}};
}

bool VfxEditorDocument::RestoreRecovery(const AssetDatabase& database, const nlohmann::json& snapshot,
                                        std::string& message) {
    VFX::VfxAssetData recovered;
    if (!VFX::SERIALIZATION::ParseVfxSourceJson(snapshot.at("source").dump(), recovered, message))
        return false;
    const AssetGuid guid{snapshot.value("assetGuid", std::string{})};
    const bool opened = guid.IsValid() && OpenAsset(database, guid, message);
    if (opened && !snapshot.value("dirty", true))
        return true;
    if (!opened)
        NewAsset(recovered.name);
    if (!ApplyAsset(std::move(recovered), message))
        return false;
    if (opened) {
        sourceVersion_.RestoreFingerprint(snapshot.at("sourceHash").get<uint64_t>(),
                                          snapshot.value("sourceReadable", false));
        // 復旧後に元ファイルが変わっていた場合は独立したコピーとして保持。
        if (!sourceVersion_.CanSave(message)) {
            source_ = VfxEditorDocumentSource::Transient;
            assetGuid_ = {};
            sourcePath_.clear();
            sourceVersion_ = {};
            asset_.name += " (Recovered)";
            displayName_ = asset_.name;
            ResetHistory(false);
        }
    }
    message = "Recovered VFX draft. Previous process undo history is not restored.";
    return true;
}

bool VfxEditorDocument::OpenAsset(const AssetDatabase& database, const AssetGuid& assetGuid, std::string& outMessage,
                                  bool discardUnsaved) {
    if (IsDirty() && !discardUnsaved) {
        outMessage = "Current VFX document has unsaved changes";
        return false;
    }
    VFX::VfxAssetData loaded{};
    if (!LoadVfxForAuthoring(database, assetGuid, loaded, outMessage)) {
        return false;
    }
    const AssetRecord* record = database.FindByGuid(assetGuid);
    if (record == nullptr || record->type != AssetType::VfxEffect) {
        outMessage = "VFX asset record disappeared while opening";
        return false;
    }
    asset_ = std::move(loaded);
    source_ = VfxEditorDocumentSource::Asset;
    assetGuid_ = record->guid;
    sourceVersion_.Accept(database, assetGuid_);
    sourcePath_ = record->sourcePath;
    displayName_ = record->displayName.empty() ? sourcePath_.stem().string() : record->displayName;
    ResetHistory(true);
    NUMERIC::AdvanceNonZero(revision_);
    outMessage = "Opened VFX asset: " + displayName_;
    return true;
}

bool VfxEditorDocument::Save(AssetDatabase& database, std::string& outMessage) {
    if (!IsOpen()) {
        outMessage = "No VFX document is open";
        return false;
    }
    if (assetGuid_.IsValid() && !sourceVersion_.CanSave(outMessage))
        return false;
    const VfxAssetAuthoringResult result = source_ == VfxEditorDocumentSource::Transient
                                               ? CreateVfxAsset(database, asset_)
                                               : UpdateVfxAsset(database, assetGuid_, asset_);
    outMessage = result.message;
    if (!result.assetGuid.IsValid()) {
        return false;
    }
    assetGuid_ = result.assetGuid;
    sourceVersion_.Accept(database, assetGuid_);
    sourcePath_ = result.sourcePath;
    source_ = VfxEditorDocumentSource::Asset;
    displayName_ = asset_.name;
    if (!result.success) {
        // Keep the source identity after an importer failure so a retry
        // updates the same file instead of creating a suffixed duplicate.
        return false;
    }
    history_.MarkSaved();
    return true;
}

bool VfxEditorDocument::Revert(const AssetDatabase& database, std::string& outMessage) {
    if (source_ != VfxEditorDocumentSource::Asset || !assetGuid_.IsValid()) {
        outMessage = "Only a saved VFX asset can be reverted";
        return false;
    }
    const AssetGuid assetGuid = assetGuid_;
    return OpenAsset(database, assetGuid, outMessage, true);
}

} // namespace HIKARI::EDITOR
