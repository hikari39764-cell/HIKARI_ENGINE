#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::DIALOGUE {

enum class DialogueNodeType : uint8_t {
    Line = 0u,
    Choice = 1u,
    Event = 2u,
    End = 3u,
};

enum class DialoguePortraitSlot : uint8_t {
    Auto = 0u,
    Left = 1u,
    Right = 2u,
};

enum class DialoguePortraitAction : uint8_t {
    Auto = 0u,
    Show = 1u,
    Keep = 2u,
    Hide = 3u,
};

inline constexpr TEXT::EnumName<DialogueNodeType> kDialogueNodeTypeNames[] = {
    {DialogueNodeType::Line, "Line"},
    {DialogueNodeType::Choice, "Choice"},
    {DialogueNodeType::Event, "Event"},
    {DialogueNodeType::End, "End"},
};

inline constexpr TEXT::EnumName<DialoguePortraitSlot> kDialoguePortraitSlotNames[] = {
    {DialoguePortraitSlot::Auto, "Auto"},
    {DialoguePortraitSlot::Left, "Left"},
    {DialoguePortraitSlot::Right, "Right"},
};

inline constexpr TEXT::EnumName<DialoguePortraitAction> kDialoguePortraitActionNames[] = {
    {DialoguePortraitAction::Auto, "Auto"},
    {DialoguePortraitAction::Show, "Show"},
    {DialoguePortraitAction::Keep, "Keep"},
    {DialoguePortraitAction::Hide, "Hide"},
};

struct LocalizedText {
    std::map<std::string, std::string, std::less<>> values{};
};

struct DialogueChoice {
    std::string id{};
    LocalizedText text{};
    std::string nextNodeId{};
};

struct DialogueNode {
    std::string id{};
    DialogueNodeType type = DialogueNodeType::Line;
    // speakerId addresses the project dialogue style's character list.
    // speaker provides an optional per-line localized display name.
    std::string speakerId{};
    LocalizedText speaker{};
    LocalizedText text{};
    std::string nextNodeId{};
    std::string eventId{};
    std::vector<DialogueChoice> choices{};
    float autoAdvanceSeconds = 0.0f;
    bool skippable = true;
    DialoguePortraitSlot portraitSlot = DialoguePortraitSlot::Auto;
    DialoguePortraitAction portraitAction = DialoguePortraitAction::Auto;
};

struct DialogueDefinition {
    std::string name{"Dialogue"};
    std::string entryNodeId{"entry"};
    std::string fallbackLocale{"en-US"};
    std::vector<DialogueNode> nodes{};
};

struct DialogueValidationIssue {
    bool error = false;
    std::string path{};
    std::string message{};
};

const char* ToString(DialogueNodeType type) noexcept;
const char* ToString(DialoguePortraitSlot slot) noexcept;
const char* ToString(DialoguePortraitAction action) noexcept;

void SerializeLocalizedTextJson(const LocalizedText& value, nlohmann::json& outJson);
bool DeserializeLocalizedTextJson(const nlohmann::json& json, LocalizedText& outValue);

void NormalizeDialogue(DialogueDefinition& definition);

std::vector<DialogueValidationIssue> ValidateDialogue(const DialogueDefinition& definition);
const DialogueNode* FindDialogueNode(const DialogueDefinition& definition, std::string_view nodeId) noexcept;
std::string_view ResolveLocalizedText(const LocalizedText& text, std::string_view requestedLocale,
                                      std::string_view fallbackLocale) noexcept;

void SerializeDialogueJson(const DialogueDefinition& definition, nlohmann::json& outJson);
bool DeserializeDialogueJson(const nlohmann::json& json, DialogueDefinition& outDefinition,
                             std::string* outError = nullptr);

} // namespace HIKARI::DIALOGUE
