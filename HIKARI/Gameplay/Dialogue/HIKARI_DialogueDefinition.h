#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

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
        // speaker remains as a legacy localized fallback for v1/v2 assets.
        std::string speakerId{};
        LocalizedText speaker{};
        LocalizedText text{};
        std::string nextNodeId{};
        std::string eventId{};
        std::vector<DialogueChoice> choices{};
        float autoAdvanceSeconds = 0.0f;
        bool skippable = true;
        DialoguePortraitSlot portraitSlot = DialoguePortraitSlot::Auto;
        DialoguePortraitAction portraitAction =
            DialoguePortraitAction::Auto;
    };

    struct DialogueColor {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DialogueTextStyle {
        DialogueColor textColor{
            0.0156863f, 0.1686275f, 0.5058824f, 1.0f };
        DialogueColor outlineColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DialogueColor shadowColor{ 0.0f, 0.0f, 0.0f, 0.20f };
        DialogueColor glowColor{
            0.0156863f, 0.1686275f, 0.5058824f, 0.40f };
        float outlineWidth = 0.65f;
        float shadowOffsetX = 1.5f;
        float shadowOffsetY = 2.0f;
        float glowWidth = 3.0f;
        float glowIntensity = 0.18f;
        float letterSpacing = 0.0f;
        float lineHeightMultiplier = 1.20f;
    };

    // All layout values use one reference canvas. dialoguePanelAssetPath is a
    // single complete authored panel; the runtime never assumes separately
    // authored white/front and blue/offset layers.
    struct DialogueTheme {
        std::string fontAssetPath{};
        std::string dialoguePanelAssetPath{};
        std::string speakerPlateAssetPath{};
        std::string choiceAssetPath{};
        std::string advanceGamepadAssetPath{};
        std::string advanceMouseAssetPath{};

        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;

        float panelX = 176.0f;
        float panelY = 708.0f;
        float panelWidth = 1580.0f;
        float panelHeight = 325.0f;

        float speakerPlateX = 310.0f;
        float speakerPlateY = 696.0f;
        float speakerPlateWidth = 360.0f;
        float speakerPlateHeight = 80.0f;

        float speakerTextX = 360.0f;
        float speakerTextY = 710.0f;
        float speakerTextWidth = 260.0f;
        float speakerTextHeight = 54.0f;
        float speakerPixelHeight = 34.0f;
        float speakerTextRotationDegrees = 0.0f;
        float speakerTextScale = 1.0f;
        DialogueTextStyle speakerTextStyle{};

        float bodyTextX = 320.0f;
        float bodyTextY = 790.0f;
        float bodyTextWidth = 1280.0f;
        float bodyTextHeight = 170.0f;
        float bodyPixelHeight = 40.0f;
        float bodyTextRotationDegrees = 0.0f;
        float bodyTextScale = 1.0f;
        DialogueTextStyle bodyTextStyle{};

        float choiceX = 1010.0f;
        float choiceY = 390.0f;
        float choiceWidth = 530.0f;
        float choiceHeight = 92.0f;
        float choiceGap = 18.0f;
        float choiceTextOffsetX = 34.0f;
        float choiceTextOffsetY = 12.0f;
        float choiceTextWidth = 462.0f;
        float choiceTextHeight = 68.0f;
        float choicePixelHeight = 32.0f;
        float choiceTextRotationDegrees = 0.0f;
        float choiceTextScale = 1.0f;
        DialogueTextStyle choiceTextStyle{};

        // Bottom-centre anchor and natural-size scale for the device-aware
        // continue prompt. Each supplied texture keeps its authored aspect.
        float advancePromptX = 1685.0f;
        float advancePromptY = 970.0f;
        float advancePromptScale = 1.0f;
        float advancePromptPulseSeconds = 0.90f;
        float advancePromptBobDistance = 7.0f;
        float advancePromptScaleAmount = 0.06f;

        float panelRevealSeconds = 0.22f;
        float charactersPerSecond = 34.0f;
        float backdropDim = 0.38f;
        float backdropBlur = 0.55f;
        float backdropDesaturation = 0.18f;
    };

    struct DialogueDefinition {
        std::string name{ "Dialogue" };
        std::string entryNodeId{ "entry" };
        std::string fallbackLocale{ "en-US" };
        DialogueTheme theme{};
        std::vector<DialogueNode> nodes{};
    };

    struct DialogueValidationIssue {
        bool error = false;
        std::string path{};
        std::string message{};
    };

    const char* ToString(DialogueNodeType type) noexcept;
    bool TryParseDialogueNodeType(
        std::string_view text,
        DialogueNodeType& outType) noexcept;
    const char* ToString(DialoguePortraitSlot slot) noexcept;
    bool TryParseDialoguePortraitSlot(
        std::string_view text,
        DialoguePortraitSlot& outSlot) noexcept;
    const char* ToString(DialoguePortraitAction action) noexcept;
    bool TryParseDialoguePortraitAction(
        std::string_view text,
        DialoguePortraitAction& outAction) noexcept;

    void SerializeLocalizedTextJson(
        const LocalizedText& value,
        nlohmann::json& outJson);
    bool DeserializeLocalizedTextJson(
        const nlohmann::json& json,
        LocalizedText& outValue);
    void SerializeDialogueThemeJson(
        const DialogueTheme& theme,
        nlohmann::json& outJson);
    bool DeserializeDialogueThemeJson(
        const nlohmann::json& json,
        DialogueTheme& outTheme);

    void NormalizeDialogue(DialogueDefinition& definition);
    std::vector<DialogueValidationIssue> ValidateDialogueTheme(
        const DialogueTheme& theme);
    std::vector<DialogueValidationIssue> ValidateDialogue(
        const DialogueDefinition& definition);
    const DialogueNode* FindDialogueNode(
        const DialogueDefinition& definition,
        std::string_view nodeId) noexcept;
    std::string_view ResolveLocalizedText(
        const LocalizedText& text,
        std::string_view requestedLocale,
        std::string_view fallbackLocale) noexcept;

    void SerializeDialogueJson(
        const DialogueDefinition& definition,
        nlohmann::json& outJson);
    bool DeserializeDialogueJson(
        const nlohmann::json& json,
        DialogueDefinition& outDefinition,
        std::string* outError = nullptr);

} // namespace HIKARI::DIALOGUE
