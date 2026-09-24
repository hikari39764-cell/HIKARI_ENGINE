#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HIKARI::DIALOGUE {
namespace {
using json = nlohmann::json;

void SetError(std::string* outError, std::string message) {
    if (outError != nullptr) {
        *outError = std::move(message);
    }
}

void SerializeLocalized(const LocalizedText& value, json& outJson) {
    outJson = json::object();
    for (const auto& [locale, text] : value.values) {
        outJson[locale] = text;
    }
}

bool DeserializeLocalized(const json& node, LocalizedText& outValue) {
    if (!node.is_object()) {
        return false;
    }
    LocalizedText value{};
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it.key().empty() || !it.value().is_string()) {
            return false;
        }
        value.values.emplace(it.key(), it.value().get<std::string>());
    }
    outValue = std::move(value);
    return true;
}

std::string_view BaseLocale(std::string_view locale) {
    const size_t separator = locale.find_first_of("-_");
    return separator == std::string_view::npos ? locale : locale.substr(0u, separator);
}
} // namespace

const char* ToString(DialogueNodeType type) noexcept {
    switch (type) {
    case DialogueNodeType::Line: return "Line";
    case DialogueNodeType::Choice: return "Choice";
    case DialogueNodeType::Event: return "Event";
    case DialogueNodeType::End: return "End";
    default: return "Line";
    }
}

bool TryParseDialogueNodeType(std::string_view text, DialogueNodeType& outType) noexcept {
    if (text == "Line")
        outType = DialogueNodeType::Line;
    else if (text == "Choice")
        outType = DialogueNodeType::Choice;
    else if (text == "Event")
        outType = DialogueNodeType::Event;
    else if (text == "End")
        outType = DialogueNodeType::End;
    else
        return false;
    return true;
}

const char* ToString(DialoguePortraitSlot slot) noexcept {
    switch (slot) {
    case DialoguePortraitSlot::Auto: return "Auto";
    case DialoguePortraitSlot::Left: return "Left";
    case DialoguePortraitSlot::Right: return "Right";
    default: return "Auto";
    }
}

bool TryParseDialoguePortraitSlot(std::string_view text, DialoguePortraitSlot& outSlot) noexcept {
    if (text == "Auto")
        outSlot = DialoguePortraitSlot::Auto;
    else if (text == "Left")
        outSlot = DialoguePortraitSlot::Left;
    else if (text == "Right")
        outSlot = DialoguePortraitSlot::Right;
    else
        return false;
    return true;
}

const char* ToString(DialoguePortraitAction action) noexcept {
    switch (action) {
    case DialoguePortraitAction::Auto: return "Auto";
    case DialoguePortraitAction::Show: return "Show";
    case DialoguePortraitAction::Keep: return "Keep";
    case DialoguePortraitAction::Hide: return "Hide";
    default: return "Auto";
    }
}

bool TryParseDialoguePortraitAction(std::string_view text, DialoguePortraitAction& outAction) noexcept {
    if (text == "Auto")
        outAction = DialoguePortraitAction::Auto;
    else if (text == "Show")
        outAction = DialoguePortraitAction::Show;
    else if (text == "Keep")
        outAction = DialoguePortraitAction::Keep;
    else if (text == "Hide")
        outAction = DialoguePortraitAction::Hide;
    else
        return false;
    return true;
}

void SerializeLocalizedTextJson(const LocalizedText& value, nlohmann::json& outJson) {
    SerializeLocalized(value, outJson);
}

bool DeserializeLocalizedTextJson(const nlohmann::json& jsonNode, LocalizedText& outValue) {
    return DeserializeLocalized(jsonNode, outValue);
}

void NormalizeDialogue(DialogueDefinition& definition) {
    if (definition.name.empty())
        definition.name = "Dialogue";
    if (definition.fallbackLocale.empty()) {
        definition.fallbackLocale = "en-US";
    }
    if (definition.entryNodeId.empty() && !definition.nodes.empty()) {
        definition.entryNodeId = definition.nodes.front().id;
    }
    for (DialogueNode& node : definition.nodes) {
        if (!std::isfinite(node.autoAdvanceSeconds) || node.autoAdvanceSeconds < 0.0f) {
            node.autoAdvanceSeconds = 0.0f;
        }
    }
}

std::vector<DialogueValidationIssue> ValidateDialogue(const DialogueDefinition& definition) {
    std::vector<DialogueValidationIssue> issues{};
    auto add = [&issues](bool error, std::string path, std::string message) {
        issues.push_back(DialogueValidationIssue{error, std::move(path), std::move(message)});
    };
    if (definition.nodes.empty()) {
        add(true, "nodes", "Dialogue must contain at least one node");
        return issues;
    }
    std::unordered_map<std::string, const DialogueNode*> nodes{};
    for (size_t index = 0u; index < definition.nodes.size(); ++index) {
        const DialogueNode& node = definition.nodes[index];
        const std::string path = "nodes[" + std::to_string(index) + "]";
        if (node.id.empty()) {
            add(true, path + ".id", "Node id cannot be empty");
        } else if (!nodes.emplace(node.id, &node).second) {
            add(true, path + ".id", "Node id must be unique");
        }
        if (node.type == DialogueNodeType::Line && node.text.values.empty()) {
            add(true, path + ".text", "Line text cannot be empty");
        }
        if (node.type == DialogueNodeType::Choice) {
            if (node.choices.empty()) {
                add(true, path + ".choices", "Choice node must contain at least one choice");
            }
            std::unordered_set<std::string> choiceIds{};
            for (size_t choiceIndex = 0u; choiceIndex < node.choices.size(); ++choiceIndex) {
                const DialogueChoice& choice = node.choices[choiceIndex];
                const std::string choicePath = path + ".choices[" + std::to_string(choiceIndex) + "]";
                if (choice.id.empty() || !choiceIds.insert(choice.id).second) {
                    add(true, choicePath + ".id", "Choice id must be non-empty and unique");
                }
                if (choice.text.values.empty()) {
                    add(true, choicePath + ".text", "Choice text cannot be empty");
                }
            }
        }
        if (node.type == DialogueNodeType::Event && node.eventId.empty()) {
            add(true, path + ".eventId", "Event node requires an event id");
        }
    }
    if (!nodes.contains(definition.entryNodeId)) {
        add(true, "entryNodeId", "Entry node does not exist");
    }
    auto validateTarget = [&nodes, &add](std::string_view target, const std::string& path) {
        if (target.empty()) {
            add(true, path, "A non-end node requires a target");
        } else if (!nodes.contains(std::string(target))) {
            add(true, path, "Target node does not exist");
        }
    };
    for (size_t index = 0u; index < definition.nodes.size(); ++index) {
        const DialogueNode& node = definition.nodes[index];
        const std::string path = "nodes[" + std::to_string(index) + "]";
        if (node.type == DialogueNodeType::Line || node.type == DialogueNodeType::Event) {
            validateTarget(node.nextNodeId, path + ".nextNodeId");
        } else if (node.type == DialogueNodeType::Choice) {
            for (size_t choiceIndex = 0u; choiceIndex < node.choices.size(); ++choiceIndex) {
                validateTarget(node.choices[choiceIndex].nextNodeId,
                               path + ".choices[" + std::to_string(choiceIndex) + "].nextNodeId");
            }
        }
    }
    return issues;
}

const DialogueNode* FindDialogueNode(const DialogueDefinition& definition, std::string_view nodeId) noexcept {
    const auto found = std::find_if(definition.nodes.begin(), definition.nodes.end(),
                                    [nodeId](const DialogueNode& node) { return node.id == nodeId; });
    return found != definition.nodes.end() ? &*found : nullptr;
}

std::string_view ResolveLocalizedText(const LocalizedText& text, std::string_view requestedLocale,
                                      std::string_view fallbackLocale) noexcept {
    auto exact = [&text](std::string_view locale) -> std::string_view {
        const auto found = text.values.find(locale);
        return found == text.values.end() ? std::string_view{} : std::string_view(found->second);
    };
    if (const std::string_view value = exact(requestedLocale); !value.empty())
        return value;
    const std::string_view requestedBase = BaseLocale(requestedLocale);
    for (const auto& [locale, value] : text.values) {
        if (BaseLocale(locale) == requestedBase && !value.empty()) {
            return value;
        }
    }
    if (const std::string_view value = exact(fallbackLocale); !value.empty())
        return value;
    const std::string_view fallbackBase = BaseLocale(fallbackLocale);
    for (const auto& [locale, value] : text.values) {
        if (BaseLocale(locale) == fallbackBase && !value.empty()) {
            return value;
        }
    }
    if (const std::string_view value = exact("en-US"); !value.empty())
        return value;
    for (const auto& [_, value] : text.values) {
        if (!value.empty())
            return value;
    }
    return {};
}

void SerializeDialogueJson(const DialogueDefinition& definition, nlohmann::json& outJson) {
    DialogueDefinition normalized = definition;
    NormalizeDialogue(normalized);
    json nodes = json::array();
    for (const DialogueNode& node : normalized.nodes) {
        json speaker{};
        json text{};
        SerializeLocalized(node.speaker, speaker);
        SerializeLocalized(node.text, text);
        json choices = json::array();
        for (const DialogueChoice& choice : node.choices) {
            json choiceText{};
            SerializeLocalized(choice.text, choiceText);
            choices.push_back(json{
                {"id", choice.id},
                {"text", std::move(choiceText)},
                {"nextNodeId", choice.nextNodeId},
            });
        }
        nodes.push_back(json{
            {"id", node.id},
            {"type", ToString(node.type)},
            {"speakerId", node.speakerId},
            {"speaker", std::move(speaker)},
            {"text", std::move(text)},
            {"nextNodeId", node.nextNodeId},
            {"eventId", node.eventId},
            {"choices", std::move(choices)},
            {"autoAdvanceSeconds", node.autoAdvanceSeconds},
            {"skippable", node.skippable},
            {"portraitSlot", ToString(node.portraitSlot)},
            {"portraitAction", ToString(node.portraitAction)},
        });
    }
    outJson = json{
        {"name", normalized.name},
        {"entryNodeId", normalized.entryNodeId},
        {"fallbackLocale", normalized.fallbackLocale},
        {"nodes", std::move(nodes)},
    };
}

bool DeserializeDialogueJson(const nlohmann::json& jsonNode, DialogueDefinition& outDefinition, std::string* outError) {
    if (!jsonNode.is_object() || !jsonNode.contains("nodes") || !jsonNode["nodes"].is_array()) {
        SetError(outError, "Dialogue payload must contain a node array");
        return false;
    }
    DialogueDefinition definition{};
    definition.name = jsonNode.value("name", definition.name);
    definition.entryNodeId = jsonNode.value("entryNodeId", definition.entryNodeId);
    definition.fallbackLocale = jsonNode.value("fallbackLocale", definition.fallbackLocale);
    if (jsonNode.contains("theme")) {
        SetError(outError,
                 "Embedded dialogue themes are retired; use the project dialogue style and TextStyle assets.");
        return false;
    }
    for (const json& nodeJson : jsonNode["nodes"]) {
        if (!nodeJson.is_object()) {
            SetError(outError, "Dialogue node must be an object");
            return false;
        }
        DialogueNode node{};
        node.id = nodeJson.value("id", std::string{});
        node.speakerId = nodeJson.value("speakerId", std::string{});
        const std::string type = nodeJson.value("type", "Line");
        if (!TryParseDialogueNodeType(type, node.type)) {
            SetError(outError, "Dialogue node type is invalid: " + type);
            return false;
        }
        const std::string portraitSlot = nodeJson.value("portraitSlot", "Auto");
        if (!TryParseDialoguePortraitSlot(portraitSlot, node.portraitSlot)) {
            SetError(outError, "Dialogue portrait slot is invalid: " + portraitSlot);
            return false;
        }
        const std::string portraitAction = nodeJson.value("portraitAction", "Auto");
        if (!TryParseDialoguePortraitAction(portraitAction, node.portraitAction)) {
            SetError(outError, "Dialogue portrait action is invalid: " + portraitAction);
            return false;
        }
        if (const auto speaker = nodeJson.find("speaker");
            speaker != nodeJson.end() && !DeserializeLocalized(*speaker, node.speaker)) {
            SetError(outError, "Dialogue speaker localization is invalid");
            return false;
        }
        if (const auto text = nodeJson.find("text");
            text != nodeJson.end() && !DeserializeLocalized(*text, node.text)) {
            SetError(outError, "Dialogue text localization is invalid");
            return false;
        }
        node.nextNodeId = nodeJson.value("nextNodeId", std::string{});
        node.eventId = nodeJson.value("eventId", std::string{});
        node.autoAdvanceSeconds = nodeJson.value("autoAdvanceSeconds", 0.0f);
        node.skippable = nodeJson.value("skippable", true);
        if (const auto choices = nodeJson.find("choices"); choices != nodeJson.end()) {
            if (!choices->is_array()) {
                SetError(outError, "Dialogue choices must be an array");
                return false;
            }
            for (const json& choiceJson : *choices) {
                if (!choiceJson.is_object()) {
                    SetError(outError, "Dialogue choice must be an object");
                    return false;
                }
                DialogueChoice choice{};
                choice.id = choiceJson.value("id", std::string{});
                choice.nextNodeId = choiceJson.value("nextNodeId", std::string{});
                const auto choiceText = choiceJson.find("text");
                if (choiceText == choiceJson.end() || !DeserializeLocalized(*choiceText, choice.text)) {
                    SetError(outError, "Dialogue choice localization is invalid");
                    return false;
                }
                node.choices.push_back(std::move(choice));
            }
        }
        definition.nodes.push_back(std::move(node));
    }
    NormalizeDialogue(definition);
    outDefinition = std::move(definition);
    if (outError != nullptr)
        outError->clear();
    return true;
}

} // namespace HIKARI::DIALOGUE
