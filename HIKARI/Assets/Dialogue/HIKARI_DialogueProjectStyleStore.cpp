#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Core/Text/HIKARI_OutMessage.h"

#include <algorithm>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::DIALOGUE {
namespace {
using json = nlohmann::json;

std::string ToAssetRelativePath(const AssetDatabase& database, const AssetRecord& record) {
    std::filesystem::path source = record.sourcePath;
    if (!source.is_absolute()) {
        source = (database.GetProjectRoot() / source).lexically_normal();
    }
    std::error_code error{};
    const std::filesystem::path relative = std::filesystem::relative(source, database.GetAssetsRoot(), error);
    if (error || relative.empty() || relative.has_root_path()) {
        return {};
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..")
            return {};
    }
    return relative.generic_string();
}
} // namespace

void ResolveDialogueProjectStyleAssets(DialogueProjectStyle& style, const AssetDatabase& database) {
    for (DialogueSpeakerProfile& speaker : style.speakers) {
        speaker.resolvedPortraitAssetPath.clear();
        if (speaker.portraitAssetGuid.empty())
            continue;
        const AssetRecord* record = database.FindByGuid(AssetGuid{speaker.portraitAssetGuid});
        if (record == nullptr || record->type != AssetType::Texture || !record->sourceExists) {
            continue;
        }
        speaker.resolvedPortraitAssetPath = ToAssetRelativePath(database, *record);
    }
}

bool LoadDialogueProjectStyle(const std::filesystem::path& projectRoot, DialogueProjectStyle& outStyle,
                              std::string* outMessage) {
    const std::filesystem::path path = projectRoot / std::string(kDialogueProjectStylePath);
    std::error_code existsError{};
    if (!std::filesystem::is_regular_file(path, existsError)) {
        outStyle = MakeDefaultDialogueProjectStyle();
        SetMessage(outMessage, "New project dialogue style");
        return true;
    }
    json root{};
    std::string ioMessage{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &ioMessage)) {
        SetMessage(outMessage, "Unable to read dialogue UI style: " + ioMessage);
        return false;
    }
    if (!DeserializeDialogueProjectStyleJson(root, outStyle, outMessage)) {
        return false;
    }
    SetMessage(outMessage, "Dialogue UI style loaded");
    return true;
}

bool SaveDialogueProjectStyle(const std::filesystem::path& projectRoot, const DialogueProjectStyle& style,
                              std::string* outMessage) {
    DialogueProjectStyle normalized = style;
    NormalizeDialogueProjectStyle(normalized);
    const auto issues = ValidateDialogueProjectStyle(normalized);
    const auto error =
        std::find_if(issues.begin(), issues.end(), [](const DialogueValidationIssue& issue) { return issue.error; });
    if (error != issues.end()) {
        SetMessage(outMessage, error->path + ": " + error->message);
        return false;
    }
    json root{};
    SerializeDialogueProjectStyleJson(normalized, root);
    const std::filesystem::path path = projectRoot / std::string(kDialogueProjectStylePath);
    std::error_code directoryError{};
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        SetMessage(outMessage, "Unable to create ProjectSettings");
        return false;
    }
    std::string ioMessage{};
    if (!SERIALIZATION::JSON::WriteJsonFile(path, root, &ioMessage)) {
        SetMessage(outMessage, "Unable to save dialogue UI style: " + ioMessage);
        return false;
    }
    SetMessage(outMessage, "Dialogue UI style saved");
    return true;
}

} // namespace HIKARI::DIALOGUE
