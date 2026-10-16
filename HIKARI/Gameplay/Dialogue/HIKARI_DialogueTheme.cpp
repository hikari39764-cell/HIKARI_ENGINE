#include "Gameplay/Dialogue/HIKARI_DialogueTheme.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace HIKARI::DIALOGUE {
namespace {
using json = nlohmann::json;
bool IsSafeAssetPath(std::string_view value) {
    return PATHS::IsSafeRelativePath(std::filesystem::path{std::string(value)});
}

bool IsFinitePositive(float value) { return std::isfinite(value) && value > 0.0f; }

json SerializeTheme(const DialogueTheme& theme) {
    return json{
        {"dialoguePanelAssetPath", theme.dialoguePanelAssetPath},
        {"speakerPlateAssetPath", theme.speakerPlateAssetPath},
        {"choiceAssetPath", theme.choiceAssetPath},
        {"advanceGamepadAssetPath", theme.advanceGamepadAssetPath},
        {"advanceMouseAssetPath", theme.advanceMouseAssetPath},
        {"referenceSize", {theme.referenceWidth, theme.referenceHeight}},
        {"panelRect", {theme.panelX, theme.panelY, theme.panelWidth, theme.panelHeight}},
        {"speakerPlateRect",
         {theme.speakerPlateX, theme.speakerPlateY, theme.speakerPlateWidth, theme.speakerPlateHeight}},
        {"speakerTextRect", {theme.speakerTextX, theme.speakerTextY, theme.speakerTextWidth, theme.speakerTextHeight}},
        {"speakerTextRotationDegrees", theme.speakerTextRotationDegrees},
        {"speakerTextScale", theme.speakerTextScale},
        {"speakerTextStyleAssetGuid", theme.speakerTextStyle.value},
        {"bodyTextRect", {theme.bodyTextX, theme.bodyTextY, theme.bodyTextWidth, theme.bodyTextHeight}},
        {"bodyTextRotationDegrees", theme.bodyTextRotationDegrees},
        {"bodyTextScale", theme.bodyTextScale},
        {"bodyTextStyleAssetGuid", theme.bodyTextStyle.value},
        {"choiceRect", {theme.choiceX, theme.choiceY, theme.choiceWidth, theme.choiceHeight}},
        {"choiceGap", theme.choiceGap},
        {"choiceTextRect",
         {theme.choiceTextOffsetX, theme.choiceTextOffsetY, theme.choiceTextWidth, theme.choiceTextHeight}},
        {"choiceTextRotationDegrees", theme.choiceTextRotationDegrees},
        {"choiceTextScale", theme.choiceTextScale},
        {"choiceTextStyleAssetGuid", theme.choiceTextStyle.value},
        {"advancePromptAnchor", {theme.advancePromptX, theme.advancePromptY}},
        {"advancePromptScale", theme.advancePromptScale},
        {"advancePromptPulseSeconds", theme.advancePromptPulseSeconds},
        {"advancePromptBobDistance", theme.advancePromptBobDistance},
        {"advancePromptScaleAmount", theme.advancePromptScaleAmount},
        {"panelRevealSeconds", theme.panelRevealSeconds},
        {"charactersPerSecond", theme.charactersPerSecond},
        {"backdropDim", theme.backdropDim},
        {"backdropBlur", theme.backdropBlur},
        {"backdropDesaturation", theme.backdropDesaturation},
    };
}

bool ReadFloatArray(const json& object, const char* key, float* values, size_t count) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return true;
    }
    if (!found->is_array() || found->size() != count) {
        return false;
    }
    for (size_t index = 0u; index < count; ++index) {
        if (!(*found)[index].is_number()) {
            return false;
        }
        values[index] = (*found)[index].get<float>();
    }
    return true;
}

bool DeserializeTheme(const json& node, DialogueTheme& outTheme) {
    if (!node.is_object()) {
        return false;
    }
    DialogueTheme theme{};
    if (node.contains("fontAssetPath") || node.contains("bodyTextStyle"))
        return false;
    theme.dialoguePanelAssetPath = node.value("dialoguePanelAssetPath", theme.dialoguePanelAssetPath);
    theme.speakerPlateAssetPath = node.value("speakerPlateAssetPath", theme.speakerPlateAssetPath);
    theme.choiceAssetPath = node.value("choiceAssetPath", theme.choiceAssetPath);
    theme.advanceGamepadAssetPath = node.value("advanceGamepadAssetPath", theme.advanceGamepadAssetPath);
    theme.advanceMouseAssetPath = node.value("advanceMouseAssetPath", theme.advanceMouseAssetPath);
    float reference[2]{theme.referenceWidth, theme.referenceHeight};
    float panel[4]{theme.panelX, theme.panelY, theme.panelWidth, theme.panelHeight};
    float speakerPlate[4]{theme.speakerPlateX, theme.speakerPlateY, theme.speakerPlateWidth, theme.speakerPlateHeight};
    float speakerText[4]{theme.speakerTextX, theme.speakerTextY, theme.speakerTextWidth, theme.speakerTextHeight};
    float bodyText[4]{theme.bodyTextX, theme.bodyTextY, theme.bodyTextWidth, theme.bodyTextHeight};
    float choice[4]{theme.choiceX, theme.choiceY, theme.choiceWidth, theme.choiceHeight};
    float choiceText[4]{theme.choiceTextOffsetX, theme.choiceTextOffsetY, theme.choiceTextWidth,
                        theme.choiceTextHeight};
    float advancePromptAnchor[2]{theme.advancePromptX, theme.advancePromptY};
    if (!ReadFloatArray(node, "referenceSize", reference, 2u) || !ReadFloatArray(node, "panelRect", panel, 4u) ||
        !ReadFloatArray(node, "speakerPlateRect", speakerPlate, 4u) ||
        !ReadFloatArray(node, "speakerTextRect", speakerText, 4u) ||
        !ReadFloatArray(node, "bodyTextRect", bodyText, 4u) || !ReadFloatArray(node, "choiceRect", choice, 4u) ||
        !ReadFloatArray(node, "choiceTextRect", choiceText, 4u) ||
        !ReadFloatArray(node, "advancePromptAnchor", advancePromptAnchor, 2u)) {
        return false;
    }
    theme.referenceWidth = reference[0];
    theme.referenceHeight = reference[1];
    theme.panelX = panel[0];
    theme.panelY = panel[1];
    theme.panelWidth = panel[2];
    theme.panelHeight = panel[3];
    theme.speakerPlateX = speakerPlate[0];
    theme.speakerPlateY = speakerPlate[1];
    theme.speakerPlateWidth = speakerPlate[2];
    theme.speakerPlateHeight = speakerPlate[3];
    theme.speakerTextX = speakerText[0];
    theme.speakerTextY = speakerText[1];
    theme.speakerTextWidth = speakerText[2];
    theme.speakerTextHeight = speakerText[3];
    theme.bodyTextX = bodyText[0];
    theme.bodyTextY = bodyText[1];
    theme.bodyTextWidth = bodyText[2];
    theme.bodyTextHeight = bodyText[3];
    theme.choiceX = choice[0];
    theme.choiceY = choice[1];
    theme.choiceWidth = choice[2];
    theme.choiceHeight = choice[3];
    theme.choiceTextOffsetX = choiceText[0];
    theme.choiceTextOffsetY = choiceText[1];
    theme.choiceTextWidth = choiceText[2];
    theme.choiceTextHeight = choiceText[3];
    theme.advancePromptX = advancePromptAnchor[0];
    theme.advancePromptY = advancePromptAnchor[1];
    theme.speakerTextStyle.value = node.value("speakerTextStyleAssetGuid", std::string{});
    theme.speakerTextRotationDegrees = node.value("speakerTextRotationDegrees", theme.speakerTextRotationDegrees);
    theme.speakerTextScale = node.value("speakerTextScale", theme.speakerTextScale);
    theme.bodyTextStyle.value = node.value("bodyTextStyleAssetGuid", std::string{});
    theme.bodyTextRotationDegrees = node.value("bodyTextRotationDegrees", theme.bodyTextRotationDegrees);
    theme.bodyTextScale = node.value("bodyTextScale", theme.bodyTextScale);
    theme.choiceGap = node.value("choiceGap", theme.choiceGap);
    theme.choiceTextStyle.value = node.value("choiceTextStyleAssetGuid", std::string{});
    theme.choiceTextRotationDegrees = node.value("choiceTextRotationDegrees", theme.choiceTextRotationDegrees);
    theme.choiceTextScale = node.value("choiceTextScale", theme.choiceTextScale);
    theme.advancePromptScale = node.value("advancePromptScale", theme.advancePromptScale);
    theme.advancePromptPulseSeconds = node.value("advancePromptPulseSeconds", theme.advancePromptPulseSeconds);
    theme.advancePromptBobDistance = node.value("advancePromptBobDistance", theme.advancePromptBobDistance);
    theme.advancePromptScaleAmount = node.value("advancePromptScaleAmount", theme.advancePromptScaleAmount);
    theme.panelRevealSeconds = node.value("panelRevealSeconds", theme.panelRevealSeconds);
    theme.charactersPerSecond = node.value("charactersPerSecond", theme.charactersPerSecond);
    theme.backdropDim = node.value("backdropDim", theme.backdropDim);
    theme.backdropBlur = node.value("backdropBlur", theme.backdropBlur);
    theme.backdropDesaturation = node.value("backdropDesaturation", theme.backdropDesaturation);
    outTheme = std::move(theme);
    return true;
}
} // namespace
void SerializeDialogueThemeJson(const DialogueTheme& theme, nlohmann::json& outJson) {
    outJson = SerializeTheme(theme);
}

bool DeserializeDialogueThemeJson(const nlohmann::json& jsonNode, DialogueTheme& outTheme) {
    return DeserializeTheme(jsonNode, outTheme);
}

std::vector<DialogueValidationIssue> ValidateDialogueTheme(const DialogueTheme& theme) {
    std::vector<DialogueValidationIssue> issues{};
    auto add = [&issues](bool error, std::string path, std::string message) {
        issues.push_back(DialogueValidationIssue{error, std::move(path), std::move(message)});
    };

    if (!theme.dialoguePanelAssetPath.empty() && !IsSafeAssetPath(theme.dialoguePanelAssetPath)) {
        add(true, "theme.dialoguePanelAssetPath", "Dialogue panel must be a project-relative asset path");
    }
    if (!theme.speakerPlateAssetPath.empty() && !IsSafeAssetPath(theme.speakerPlateAssetPath)) {
        add(true, "theme.speakerPlateAssetPath", "Speaker plate must be a project-relative asset path");
    }
    if (!theme.choiceAssetPath.empty() && !IsSafeAssetPath(theme.choiceAssetPath)) {
        add(true, "theme.choiceAssetPath", "Choice panel must be a project-relative asset path");
    }
    if (!theme.advanceGamepadAssetPath.empty() && !IsSafeAssetPath(theme.advanceGamepadAssetPath)) {
        add(true, "theme.advanceGamepadAssetPath", "Gamepad continue prompt must be a project-relative asset path");
    }
    if (!theme.advanceMouseAssetPath.empty() && !IsSafeAssetPath(theme.advanceMouseAssetPath)) {
        add(true, "theme.advanceMouseAssetPath", "Mouse continue prompt must be a project-relative asset path");
    }
    const float positiveValues[] = {theme.referenceWidth,
                                    theme.referenceHeight,
                                    theme.panelWidth,
                                    theme.panelHeight,
                                    theme.bodyTextWidth,
                                    theme.bodyTextHeight,
                                    theme.choiceTextWidth,
                                    theme.choiceTextHeight,
                                    theme.speakerTextScale,
                                    theme.bodyTextScale,
                                    theme.choiceTextScale,
                                    theme.advancePromptScale,
                                    theme.advancePromptPulseSeconds,
                                    theme.panelRevealSeconds,
                                    theme.charactersPerSecond};
    if (std::any_of(std::begin(positiveValues), std::end(positiveValues),
                    [](float value) { return !IsFinitePositive(value); })) {
        add(true, "theme", "Dialogue theme dimensions are invalid");
    }
    const float rotations[] = {theme.speakerTextRotationDegrees, theme.bodyTextRotationDegrees,
                               theme.choiceTextRotationDegrees};
    if (std::any_of(std::begin(rotations), std::end(rotations), [](float value) { return !std::isfinite(value); })) {
        add(true, "theme", "Dialogue text rotations must be finite");
    }
    if (!std::isfinite(theme.choiceTextOffsetX) || !std::isfinite(theme.choiceTextOffsetY)) {
        add(true, "theme.choiceTextRect", "Choice text offsets must be finite");
    }
    if (!std::isfinite(theme.advancePromptX) || !std::isfinite(theme.advancePromptY) ||
        !std::isfinite(theme.advancePromptBobDistance) || !std::isfinite(theme.advancePromptScaleAmount)) {
        add(true, "theme.advancePrompt", "Continue prompt layout and animation must be finite");
    }
    if (!theme.speakerTextStyle.IsValid()) {
        add(true, "theme.speakerTextStyle", "Select a speaker TextStyle asset");
    }
    if (!theme.bodyTextStyle.IsValid()) {
        add(true, "theme.bodyTextStyle", "Select a body TextStyle asset");
    }
    if (!theme.choiceTextStyle.IsValid()) {
        add(true, "theme.choiceTextStyle", "Select a choice TextStyle asset");
    }
    return issues;
}
void NormalizeDialogueTheme(DialogueTheme& theme) {
    theme.backdropDim = std::clamp(theme.backdropDim, 0.0f, 0.9f);
    theme.backdropBlur = std::clamp(theme.backdropBlur, 0.0f, 1.0f);
    theme.backdropDesaturation = std::clamp(theme.backdropDesaturation, 0.0f, 1.0f);
    theme.panelRevealSeconds = (std::max)(theme.panelRevealSeconds, 0.01f);
    theme.charactersPerSecond = (std::max)(theme.charactersPerSecond, 1.0f);
    if (!std::isfinite(theme.speakerTextRotationDegrees)) {
        theme.speakerTextRotationDegrees = 0.0f;
    }
    if (!std::isfinite(theme.bodyTextRotationDegrees)) {
        theme.bodyTextRotationDegrees = 0.0f;
    }
    if (!std::isfinite(theme.choiceTextRotationDegrees)) {
        theme.choiceTextRotationDegrees = 0.0f;
    }
    theme.speakerTextScale = std::clamp(theme.speakerTextScale, 0.1f, 8.0f);
    theme.bodyTextScale = std::clamp(theme.bodyTextScale, 0.1f, 8.0f);
    theme.choiceTextScale = std::clamp(theme.choiceTextScale, 0.1f, 8.0f);
    if (!std::isfinite(theme.advancePromptX)) {
        theme.advancePromptX = 1685.0f;
    }
    if (!std::isfinite(theme.advancePromptY)) {
        theme.advancePromptY = 970.0f;
    }
    theme.advancePromptScale = std::clamp(theme.advancePromptScale, 0.1f, 8.0f);
    theme.advancePromptPulseSeconds = std::clamp(theme.advancePromptPulseSeconds, 0.20f, 4.0f);
    theme.advancePromptBobDistance = std::clamp(theme.advancePromptBobDistance, 0.0f, 64.0f);
    theme.advancePromptScaleAmount = std::clamp(theme.advancePromptScaleAmount, 0.0f, 0.30f);
    if (!std::isfinite(theme.choiceTextOffsetX)) {
        theme.choiceTextOffsetX = 34.0f;
    }
    if (!std::isfinite(theme.choiceTextOffsetY)) {
        theme.choiceTextOffsetY = 12.0f;
    }
    if (!std::isfinite(theme.choiceTextWidth) || theme.choiceTextWidth <= 0.0f) {
        theme.choiceTextWidth = (std::max)(theme.choiceWidth - theme.choiceTextOffsetX * 2.0f, 1.0f);
    }
    if (!std::isfinite(theme.choiceTextHeight) || theme.choiceTextHeight <= 0.0f) {
        theme.choiceTextHeight = (std::max)(theme.choiceHeight - theme.choiceTextOffsetY * 2.0f, 1.0f);
    }
}
} // namespace HIKARI::DIALOGUE
