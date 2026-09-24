#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Gameplay/Dialogue/HIKARI_DialogueTheme.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::DIALOGUE {

inline constexpr std::string_view kDialogueProjectStylePath = "ProjectSettings/DialogueUiStyle.json";

struct DialoguePortraitSlotStyle {
    float x = 96.0f;
    float y = 96.0f;
    float width = 680.0f;
    float height = 900.0f;
    float rotationDegrees = 0.0f;
};

struct DialoguePortraitTransitionStyle {
    DialogueColor speakingTint{1.0f, 1.0f, 1.0f, 1.0f};
    DialogueColor listeningTint{0.30f, 0.33f, 0.42f, 0.82f};
    DialogueColor edgeLightColor{1.0f, 1.0f, 1.0f, 1.0f};
    float speakingScale = 1.0f;
    float listeningScale = 0.965f;
    float focusSeconds = 0.18f;
    float enterSeconds = 0.24f;
    float exitSeconds = 0.18f;
    float slideDistance = 44.0f;
    float edgeLightIntensity = 0.34f;
};

struct DialogueSpeakerProfile {
    std::string id{};
    LocalizedText displayName{};
    std::string portraitAssetGuid{};

    // Runtime/editor resolved value. The project style persists the GUID;
    // this project-relative path is refreshed from AssetDatabase.
    std::string resolvedPortraitAssetPath{};

    DialoguePortraitSlot preferredSlot = DialoguePortraitSlot::Auto;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
    float rotationDegrees = 0.0f;
    bool mirror = false;
};

struct DialogueProjectStyle {
    std::string name{"Project Dialogue Style"};
    std::string fallbackLocale{"ja-JP"};
    DialogueTheme theme{};
    DialoguePortraitSlotStyle leftPortrait{};
    DialoguePortraitSlotStyle rightPortrait{1144.0f, 96.0f, 680.0f, 900.0f, 0.0f};
    DialoguePortraitTransitionStyle portraitTransition{};
    std::vector<DialogueSpeakerProfile> speakers{};
};

DialogueProjectStyle MakeDefaultDialogueProjectStyle();
void NormalizeDialogueProjectStyle(DialogueProjectStyle& style);
std::vector<DialogueValidationIssue> ValidateDialogueProjectStyle(const DialogueProjectStyle& style);
const DialogueSpeakerProfile* FindDialogueSpeaker(const DialogueProjectStyle& style,
                                                  std::string_view speakerId) noexcept;
DialogueSpeakerProfile* FindDialogueSpeaker(DialogueProjectStyle& style, std::string_view speakerId) noexcept;

void ResolveDialogueProjectStyleAssets(DialogueProjectStyle& style, const AssetDatabase& database);
void SerializeDialogueProjectStyleJson(const DialogueProjectStyle& style, nlohmann::json& outJson);
bool DeserializeDialogueProjectStyleJson(const nlohmann::json& json, DialogueProjectStyle& outStyle,
                                         std::string* outError = nullptr);
bool LoadDialogueProjectStyle(const std::filesystem::path& projectRoot, DialogueProjectStyle& outStyle,
                              std::string* outMessage = nullptr);
bool SaveDialogueProjectStyle(const std::filesystem::path& projectRoot, const DialogueProjectStyle& style,
                              std::string* outMessage = nullptr);

} // namespace HIKARI::DIALOGUE
