#pragma once
#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"
#include "Core/HIKARI_AssetGuid.h"
namespace HIKARI::DIALOGUE {
struct DialogueColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// All layout values use one reference canvas. dialoguePanelAssetPath is a
// single complete authored panel; the runtime never assumes separately
// authored white/front and blue/offset layers.
struct DialogueTheme {
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
    float speakerTextRotationDegrees = 0.0f;
    float speakerTextScale = 1.0f;
    AssetGuid speakerTextStyle{};

    float bodyTextX = 320.0f;
    float bodyTextY = 790.0f;
    float bodyTextWidth = 1280.0f;
    float bodyTextHeight = 170.0f;
    float bodyTextRotationDegrees = 0.0f;
    float bodyTextScale = 1.0f;
    AssetGuid bodyTextStyle{};

    float choiceX = 1010.0f;
    float choiceY = 390.0f;
    float choiceWidth = 530.0f;
    float choiceHeight = 92.0f;
    float choiceGap = 18.0f;
    float choiceTextOffsetX = 34.0f;
    float choiceTextOffsetY = 12.0f;
    float choiceTextWidth = 462.0f;
    float choiceTextHeight = 68.0f;
    float choiceTextRotationDegrees = 0.0f;
    float choiceTextScale = 1.0f;
    AssetGuid choiceTextStyle{};

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

void SerializeDialogueThemeJson(const DialogueTheme& theme, nlohmann::json& outJson);
bool DeserializeDialogueThemeJson(const nlohmann::json& json, DialogueTheme& outTheme);
std::vector<DialogueValidationIssue> ValidateDialogueTheme(const DialogueTheme& theme);
void NormalizeDialogueTheme(DialogueTheme& theme);
} // namespace HIKARI::DIALOGUE
