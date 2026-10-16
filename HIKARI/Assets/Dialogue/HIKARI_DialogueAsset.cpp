#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Core/Text/HIKARI_OutMessage.h"

#include <algorithm>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {
namespace {} // namespace

bool LoadDialogueAsset(const std::filesystem::path& path, const AssetGuid& authoritativeGuid, DialogueAsset& outAsset,
                       std::string* outError) {
    nlohmann::json root{};
    std::string ioMessage{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &ioMessage)) {
        SetMessage(outError, "Unable to read dialogue asset: " + ioMessage);
        return false;
    }
    const uint32_t formatVersion = root.is_object() ? root.value("formatVersion", 0u) : 0u;
    if (!root.is_object() || formatVersion != DialogueAsset::kFormatVersion) {
        SetMessage(
            outError,
            "Dialogue v4 required: re-save content without an embedded theme and select TextStyle assets in Dialogue UI Style");
        return false;
    }
    const auto payload = root.find("dialogue");
    if (payload == root.end() || !payload->is_object()) {
        SetMessage(outError, "Dialogue asset payload is missing");
        return false;
    }

    DialogueAsset loaded{};
    loaded.guid = authoritativeGuid;
    if (!loaded.guid.IsValid()) {
        loaded.guid.value = root.value("guid", std::string{});
    }
    loaded.displayName = root.value("displayName", path.stem().string());
    if (!DIALOGUE::DeserializeDialogueJson(*payload, loaded.definition, outError)) {
        return false;
    }
    auto issues = DIALOGUE::ValidateDialogue(loaded.definition);

    const auto error = std::find_if(issues.begin(), issues.end(),
                                    [](const DIALOGUE::DialogueValidationIssue& issue) { return issue.error; });
    if (error != issues.end()) {
        SetMessage(outError, error->path + ": " + error->message);
        return false;
    }
    if (loaded.displayName.empty()) {
        loaded.displayName = loaded.definition.name;
    }
    outAsset = std::move(loaded);
    if (outError != nullptr)
        outError->clear();
    return true;
}

bool SaveDialogueAsset(const std::filesystem::path& path, const DialogueAsset& asset, std::string* outError) {
    DIALOGUE::DialogueDefinition normalized = asset.definition;
    DIALOGUE::NormalizeDialogue(normalized);
    const auto issues = DIALOGUE::ValidateDialogue(normalized);
    const auto error = std::find_if(issues.begin(), issues.end(),
                                    [](const DIALOGUE::DialogueValidationIssue& issue) { return issue.error; });
    if (error != issues.end()) {
        SetMessage(outError, error->path + ": " + error->message);
        return false;
    }
    nlohmann::json payload{};
    DIALOGUE::SerializeDialogueJson(normalized, payload);
    const nlohmann::json root{
        {"assetType", "Dialogue"},
        {"formatVersion", DialogueAsset::kFormatVersion},
        {"guid", asset.guid.value},
        {"displayName", asset.displayName.empty() ? normalized.name : asset.displayName},
        {"dialogue", std::move(payload)},
    };
    std::string ioMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(path, root, &ioMessage)) {
        SetMessage(outError, "Unable to write dialogue asset: " + ioMessage);
        return false;
    }
    if (outError != nullptr)
        outError->clear();
    return true;
}

} // namespace HIKARI
