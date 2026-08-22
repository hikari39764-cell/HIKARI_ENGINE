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

        bool IsSafeAssetPath(std::string_view value) {
            if (value.empty()) {
                return false;
            }
            const std::filesystem::path path{ std::string(value) };
            if (path.is_absolute() || path.has_root_path()) {
                return false;
            }
            return std::none_of(
                path.begin(),
                path.end(),
                [](const std::filesystem::path& part) {
                    return part == "..";
                });
        }

        bool IsFinitePositive(float value) {
            return std::isfinite(value) && value > 0.0f;
        }

        bool IsFiniteInRange(float value, float minimum, float maximum) {
            return std::isfinite(value) &&
                value >= minimum && value <= maximum;
        }

        void NormalizeColor(DialogueColor& color) {
            float* channels[] = { &color.r, &color.g, &color.b };
            for (float* channel : channels) {
                if (!std::isfinite(*channel)) *channel = 0.0f;
                *channel = std::clamp(*channel, 0.0f, 8.0f);
            }
            if (!std::isfinite(color.a)) color.a = 0.0f;
            color.a = std::clamp(color.a, 0.0f, 1.0f);
        }

        void NormalizeTextStyle(DialogueTextStyle& style) {
            NormalizeColor(style.textColor);
            NormalizeColor(style.outlineColor);
            NormalizeColor(style.shadowColor);
            NormalizeColor(style.glowColor);
            if (!std::isfinite(style.outlineWidth)) style.outlineWidth = 0.0f;
            if (!std::isfinite(style.shadowOffsetX)) style.shadowOffsetX = 0.0f;
            if (!std::isfinite(style.shadowOffsetY)) style.shadowOffsetY = 0.0f;
            if (!std::isfinite(style.glowWidth)) style.glowWidth = 0.0f;
            if (!std::isfinite(style.glowIntensity)) style.glowIntensity = 0.0f;
            if (!std::isfinite(style.letterSpacing)) style.letterSpacing = 0.0f;
            if (!std::isfinite(style.lineHeightMultiplier)) {
                style.lineHeightMultiplier = 1.2f;
            }
            style.outlineWidth = std::clamp(style.outlineWidth, 0.0f, 12.0f);
            style.shadowOffsetX = std::clamp(
                style.shadowOffsetX, -64.0f, 64.0f);
            style.shadowOffsetY = std::clamp(
                style.shadowOffsetY, -64.0f, 64.0f);
            style.glowWidth = std::clamp(style.glowWidth, 0.0f, 12.0f);
            style.glowIntensity = std::clamp(
                style.glowIntensity, 0.0f, 8.0f);
            style.letterSpacing = std::clamp(
                style.letterSpacing, -16.0f, 64.0f);
            style.lineHeightMultiplier = std::clamp(
                style.lineHeightMultiplier, 0.5f, 4.0f);
        }

        bool IsValidColor(const DialogueColor& color) {
            return IsFiniteInRange(color.r, 0.0f, 8.0f) &&
                IsFiniteInRange(color.g, 0.0f, 8.0f) &&
                IsFiniteInRange(color.b, 0.0f, 8.0f) &&
                IsFiniteInRange(color.a, 0.0f, 1.0f);
        }

        bool IsValidTextStyle(const DialogueTextStyle& style) {
            return IsValidColor(style.textColor) &&
                IsValidColor(style.outlineColor) &&
                IsValidColor(style.shadowColor) &&
                IsValidColor(style.glowColor) &&
                IsFiniteInRange(style.outlineWidth, 0.0f, 12.0f) &&
                IsFiniteInRange(style.shadowOffsetX, -64.0f, 64.0f) &&
                IsFiniteInRange(style.shadowOffsetY, -64.0f, 64.0f) &&
                IsFiniteInRange(style.glowWidth, 0.0f, 12.0f) &&
                IsFiniteInRange(style.glowIntensity, 0.0f, 8.0f) &&
                IsFiniteInRange(style.letterSpacing, -16.0f, 64.0f) &&
                IsFiniteInRange(
                    style.lineHeightMultiplier, 0.5f, 4.0f);
        }

        void SerializeLocalized(
            const LocalizedText& value,
            json& outJson) {
            outJson = json::object();
            for (const auto& [locale, text] : value.values) {
                outJson[locale] = text;
            }
        }

        bool DeserializeLocalized(
            const json& node,
            LocalizedText& outValue) {
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

        json SerializeColor(const DialogueColor& color) {
            return json::array({ color.r, color.g, color.b, color.a });
        }

        json SerializeTextStyle(const DialogueTextStyle& style) {
            return json{
                { "textColor", SerializeColor(style.textColor) },
                { "outlineColor", SerializeColor(style.outlineColor) },
                { "shadowColor", SerializeColor(style.shadowColor) },
                { "glowColor", SerializeColor(style.glowColor) },
                { "outlineWidth", style.outlineWidth },
                { "shadowOffset", {
                    style.shadowOffsetX, style.shadowOffsetY } },
                { "glowWidth", style.glowWidth },
                { "glowIntensity", style.glowIntensity },
                { "letterSpacing", style.letterSpacing },
                { "lineHeightMultiplier", style.lineHeightMultiplier },
            };
        }

        json SerializeTheme(const DialogueTheme& theme) {
            return json{
                { "fontAssetPath", theme.fontAssetPath },
                { "dialoguePanelAssetPath", theme.dialoguePanelAssetPath },
                { "speakerPlateAssetPath", theme.speakerPlateAssetPath },
                { "choiceAssetPath", theme.choiceAssetPath },
                { "referenceSize", {
                    theme.referenceWidth, theme.referenceHeight } },
                { "panelRect", {
                    theme.panelX, theme.panelY,
                    theme.panelWidth, theme.panelHeight } },
                { "speakerPlateRect", {
                    theme.speakerPlateX, theme.speakerPlateY,
                    theme.speakerPlateWidth, theme.speakerPlateHeight } },
                { "speakerTextRect", {
                    theme.speakerTextX, theme.speakerTextY,
                    theme.speakerTextWidth, theme.speakerTextHeight } },
                { "speakerPixelHeight", theme.speakerPixelHeight },
                { "speakerTextRotationDegrees",
                    theme.speakerTextRotationDegrees },
                { "speakerTextScale", theme.speakerTextScale },
                { "speakerTextStyle",
                    SerializeTextStyle(theme.speakerTextStyle) },
                { "bodyTextRect", {
                    theme.bodyTextX, theme.bodyTextY,
                    theme.bodyTextWidth, theme.bodyTextHeight } },
                { "bodyPixelHeight", theme.bodyPixelHeight },
                { "bodyTextRotationDegrees",
                    theme.bodyTextRotationDegrees },
                { "bodyTextScale", theme.bodyTextScale },
                { "bodyTextStyle",
                    SerializeTextStyle(theme.bodyTextStyle) },
                { "choiceRect", {
                    theme.choiceX, theme.choiceY,
                    theme.choiceWidth, theme.choiceHeight } },
                { "choiceGap", theme.choiceGap },
                { "choiceTextRect", {
                    theme.choiceTextOffsetX,
                    theme.choiceTextOffsetY,
                    theme.choiceTextWidth,
                    theme.choiceTextHeight } },
                { "choicePixelHeight", theme.choicePixelHeight },
                { "choiceTextRotationDegrees",
                    theme.choiceTextRotationDegrees },
                { "choiceTextScale", theme.choiceTextScale },
                { "choiceTextStyle",
                    SerializeTextStyle(theme.choiceTextStyle) },
                { "panelRevealSeconds", theme.panelRevealSeconds },
                { "charactersPerSecond", theme.charactersPerSecond },
                { "backdropDim", theme.backdropDim },
                { "backdropBlur", theme.backdropBlur },
                { "backdropDesaturation", theme.backdropDesaturation },
            };
        }

        bool ReadFloatArray(
            const json& object,
            const char* key,
            float* values,
            size_t count) {
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

        bool DeserializeColor(
            const json& object,
            const char* key,
            DialogueColor& outColor) {
            float values[4]{
                outColor.r, outColor.g, outColor.b, outColor.a };
            if (!ReadFloatArray(object, key, values, 4u)) {
                return false;
            }
            outColor = DialogueColor{
                values[0], values[1], values[2], values[3] };
            return true;
        }

        bool DeserializeTextStyle(
            const json& object,
            const char* key,
            DialogueTextStyle& outStyle) {
            const auto found = object.find(key);
            if (found == object.end()) {
                return true;
            }
            if (!found->is_object()) {
                return false;
            }
            DialogueTextStyle style = outStyle;
            float shadowOffset[2]{
                style.shadowOffsetX, style.shadowOffsetY };
            if (!DeserializeColor(*found, "textColor", style.textColor) ||
                !DeserializeColor(
                    *found, "outlineColor", style.outlineColor) ||
                !DeserializeColor(
                    *found, "shadowColor", style.shadowColor) ||
                !DeserializeColor(*found, "glowColor", style.glowColor) ||
                !ReadFloatArray(
                    *found, "shadowOffset", shadowOffset, 2u)) {
                return false;
            }
            style.outlineWidth = found->value(
                "outlineWidth", style.outlineWidth);
            style.shadowOffsetX = shadowOffset[0];
            style.shadowOffsetY = shadowOffset[1];
            style.glowWidth = found->value("glowWidth", style.glowWidth);
            style.glowIntensity = found->value(
                "glowIntensity", style.glowIntensity);
            style.letterSpacing = found->value(
                "letterSpacing", style.letterSpacing);
            style.lineHeightMultiplier = found->value(
                "lineHeightMultiplier", style.lineHeightMultiplier);
            outStyle = style;
            return true;
        }

        bool DeserializeTheme(
            const json& node,
            DialogueTheme& outTheme) {
            if (!node.is_object()) {
                return false;
            }
            DialogueTheme theme{};
            theme.fontAssetPath = node.value(
                "fontAssetPath", theme.fontAssetPath);
            theme.dialoguePanelAssetPath = node.value(
                "dialoguePanelAssetPath", theme.dialoguePanelAssetPath);
            theme.speakerPlateAssetPath = node.value(
                "speakerPlateAssetPath", theme.speakerPlateAssetPath);
            theme.choiceAssetPath = node.value(
                "choiceAssetPath", theme.choiceAssetPath);
            float reference[2]{
                theme.referenceWidth, theme.referenceHeight };
            float panel[4]{
                theme.panelX, theme.panelY,
                theme.panelWidth, theme.panelHeight };
            float speakerPlate[4]{
                theme.speakerPlateX, theme.speakerPlateY,
                theme.speakerPlateWidth, theme.speakerPlateHeight };
            float speakerText[4]{
                theme.speakerTextX, theme.speakerTextY,
                theme.speakerTextWidth, theme.speakerTextHeight };
            float bodyText[4]{
                theme.bodyTextX, theme.bodyTextY,
                theme.bodyTextWidth, theme.bodyTextHeight };
            float choice[4]{
                theme.choiceX, theme.choiceY,
                theme.choiceWidth, theme.choiceHeight };
            float choiceText[4]{
                theme.choiceTextOffsetX, theme.choiceTextOffsetY,
                theme.choiceTextWidth, theme.choiceTextHeight };
            if (!ReadFloatArray(node, "referenceSize", reference, 2u) ||
                !ReadFloatArray(node, "panelRect", panel, 4u) ||
                !ReadFloatArray(
                    node, "speakerPlateRect", speakerPlate, 4u) ||
                !ReadFloatArray(
                    node, "speakerTextRect", speakerText, 4u) ||
                !ReadFloatArray(node, "bodyTextRect", bodyText, 4u) ||
                !ReadFloatArray(node, "choiceRect", choice, 4u) ||
                !ReadFloatArray(
                    node, "choiceTextRect", choiceText, 4u) ||
                !DeserializeTextStyle(
                    node, "speakerTextStyle", theme.speakerTextStyle) ||
                !DeserializeTextStyle(
                    node, "bodyTextStyle", theme.bodyTextStyle) ||
                !DeserializeTextStyle(
                    node, "choiceTextStyle", theme.choiceTextStyle)) {
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
            theme.speakerPixelHeight = node.value(
                "speakerPixelHeight", theme.speakerPixelHeight);
            theme.speakerTextRotationDegrees = node.value(
                "speakerTextRotationDegrees",
                theme.speakerTextRotationDegrees);
            theme.speakerTextScale = node.value(
                "speakerTextScale", theme.speakerTextScale);
            theme.bodyPixelHeight = node.value(
                "bodyPixelHeight", theme.bodyPixelHeight);
            theme.bodyTextRotationDegrees = node.value(
                "bodyTextRotationDegrees",
                theme.bodyTextRotationDegrees);
            theme.bodyTextScale = node.value(
                "bodyTextScale", theme.bodyTextScale);
            theme.choiceGap = node.value("choiceGap", theme.choiceGap);
            theme.choicePixelHeight = node.value(
                "choicePixelHeight", theme.choicePixelHeight);
            theme.choiceTextRotationDegrees = node.value(
                "choiceTextRotationDegrees",
                theme.choiceTextRotationDegrees);
            theme.choiceTextScale = node.value(
                "choiceTextScale", theme.choiceTextScale);
            theme.panelRevealSeconds = node.value(
                "panelRevealSeconds", theme.panelRevealSeconds);
            theme.charactersPerSecond = node.value(
                "charactersPerSecond", theme.charactersPerSecond);
            theme.backdropDim = node.value(
                "backdropDim", theme.backdropDim);
            theme.backdropBlur = node.value(
                "backdropBlur", theme.backdropBlur);
            theme.backdropDesaturation = node.value(
                "backdropDesaturation", theme.backdropDesaturation);
            outTheme = std::move(theme);
            return true;
        }

        std::string_view BaseLocale(std::string_view locale) {
            const size_t separator = locale.find_first_of("-_");
            return separator == std::string_view::npos
                ? locale
                : locale.substr(0u, separator);
        }
    }

    const char* ToString(DialogueNodeType type) noexcept {
        switch (type) {
        case DialogueNodeType::Line: return "Line";
        case DialogueNodeType::Choice: return "Choice";
        case DialogueNodeType::Event: return "Event";
        case DialogueNodeType::End: return "End";
        default: return "Line";
        }
    }

    bool TryParseDialogueNodeType(
        std::string_view text,
        DialogueNodeType& outType) noexcept {
        if (text == "Line") outType = DialogueNodeType::Line;
        else if (text == "Choice") outType = DialogueNodeType::Choice;
        else if (text == "Event") outType = DialogueNodeType::Event;
        else if (text == "End") outType = DialogueNodeType::End;
        else return false;
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

    bool TryParseDialoguePortraitSlot(
        std::string_view text,
        DialoguePortraitSlot& outSlot) noexcept {
        if (text == "Auto") outSlot = DialoguePortraitSlot::Auto;
        else if (text == "Left") outSlot = DialoguePortraitSlot::Left;
        else if (text == "Right") outSlot = DialoguePortraitSlot::Right;
        else return false;
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

    bool TryParseDialoguePortraitAction(
        std::string_view text,
        DialoguePortraitAction& outAction) noexcept {
        if (text == "Auto") outAction = DialoguePortraitAction::Auto;
        else if (text == "Show") outAction = DialoguePortraitAction::Show;
        else if (text == "Keep") outAction = DialoguePortraitAction::Keep;
        else if (text == "Hide") outAction = DialoguePortraitAction::Hide;
        else return false;
        return true;
    }

    void SerializeLocalizedTextJson(
        const LocalizedText& value,
        nlohmann::json& outJson) {
        SerializeLocalized(value, outJson);
    }

    bool DeserializeLocalizedTextJson(
        const nlohmann::json& jsonNode,
        LocalizedText& outValue) {
        return DeserializeLocalized(jsonNode, outValue);
    }

    void SerializeDialogueThemeJson(
        const DialogueTheme& theme,
        nlohmann::json& outJson) {
        outJson = SerializeTheme(theme);
    }

    bool DeserializeDialogueThemeJson(
        const nlohmann::json& jsonNode,
        DialogueTheme& outTheme) {
        return DeserializeTheme(jsonNode, outTheme);
    }

    void NormalizeDialogue(DialogueDefinition& definition) {
        if (definition.name.empty()) definition.name = "Dialogue";
        if (definition.fallbackLocale.empty()) {
            definition.fallbackLocale = "en-US";
        }
        if (definition.entryNodeId.empty() && !definition.nodes.empty()) {
            definition.entryNodeId = definition.nodes.front().id;
        }
        DialogueTheme& theme = definition.theme;
        theme.backdropDim = std::clamp(theme.backdropDim, 0.0f, 0.9f);
        theme.backdropBlur = std::clamp(theme.backdropBlur, 0.0f, 1.0f);
        theme.backdropDesaturation = std::clamp(
            theme.backdropDesaturation, 0.0f, 1.0f);
        theme.panelRevealSeconds = (std::max)(
            theme.panelRevealSeconds, 0.01f);
        theme.charactersPerSecond = (std::max)(
            theme.charactersPerSecond, 1.0f);
        if (!std::isfinite(theme.speakerTextRotationDegrees)) {
            theme.speakerTextRotationDegrees = 0.0f;
        }
        if (!std::isfinite(theme.bodyTextRotationDegrees)) {
            theme.bodyTextRotationDegrees = 0.0f;
        }
        if (!std::isfinite(theme.choiceTextRotationDegrees)) {
            theme.choiceTextRotationDegrees = 0.0f;
        }
        theme.speakerTextScale = std::clamp(
            theme.speakerTextScale, 0.1f, 8.0f);
        theme.bodyTextScale = std::clamp(
            theme.bodyTextScale, 0.1f, 8.0f);
        theme.choiceTextScale = std::clamp(
            theme.choiceTextScale, 0.1f, 8.0f);
        if (!std::isfinite(theme.choiceTextOffsetX)) {
            theme.choiceTextOffsetX = 34.0f;
        }
        if (!std::isfinite(theme.choiceTextOffsetY)) {
            theme.choiceTextOffsetY = 12.0f;
        }
        if (!std::isfinite(theme.choiceTextWidth) ||
            theme.choiceTextWidth <= 0.0f) {
            theme.choiceTextWidth = (std::max)(
                theme.choiceWidth - theme.choiceTextOffsetX * 2.0f,
                1.0f);
        }
        if (!std::isfinite(theme.choiceTextHeight) ||
            theme.choiceTextHeight <= 0.0f) {
            theme.choiceTextHeight = (std::max)(
                theme.choiceHeight - theme.choiceTextOffsetY * 2.0f,
                1.0f);
        }
        NormalizeTextStyle(theme.speakerTextStyle);
        NormalizeTextStyle(theme.bodyTextStyle);
        NormalizeTextStyle(theme.choiceTextStyle);
        for (DialogueNode& node : definition.nodes) {
            if (!std::isfinite(node.autoAdvanceSeconds) ||
                node.autoAdvanceSeconds < 0.0f) {
                node.autoAdvanceSeconds = 0.0f;
            }
        }
    }

    std::vector<DialogueValidationIssue> ValidateDialogueTheme(
        const DialogueTheme& theme) {
        std::vector<DialogueValidationIssue> issues{};
        auto add = [&issues](bool error, std::string path,
                            std::string message) {
            issues.push_back(DialogueValidationIssue{
                error, std::move(path), std::move(message) });
        };
        if (!IsSafeAssetPath(theme.fontAssetPath)) {
            add(true, "theme.fontAssetPath",
                "Font must be a project-relative asset path");
        }
        if (!IsSafeAssetPath(theme.dialoguePanelAssetPath)) {
            add(true, "theme.dialoguePanelAssetPath",
                "Dialogue panel must be a project-relative asset path");
        }
        if (!theme.speakerPlateAssetPath.empty() &&
            !IsSafeAssetPath(theme.speakerPlateAssetPath)) {
            add(true, "theme.speakerPlateAssetPath",
                "Speaker plate must be a project-relative asset path");
        }
        if (!theme.choiceAssetPath.empty() &&
            !IsSafeAssetPath(theme.choiceAssetPath)) {
            add(true, "theme.choiceAssetPath",
                "Choice panel must be a project-relative asset path");
        }
        const float positiveValues[] = {
            theme.referenceWidth, theme.referenceHeight,
            theme.panelWidth, theme.panelHeight,
            theme.bodyTextWidth, theme.bodyTextHeight,
            theme.choiceTextWidth, theme.choiceTextHeight,
            theme.bodyPixelHeight, theme.speakerTextScale,
            theme.bodyTextScale, theme.choiceTextScale,
            theme.panelRevealSeconds,
            theme.charactersPerSecond };
        if (std::any_of(
                std::begin(positiveValues),
                std::end(positiveValues),
                [](float value) { return !IsFinitePositive(value); })) {
            add(true, "theme", "Dialogue theme dimensions are invalid");
        }
        const float rotations[] = {
            theme.speakerTextRotationDegrees,
            theme.bodyTextRotationDegrees,
            theme.choiceTextRotationDegrees };
        if (std::any_of(
                std::begin(rotations),
                std::end(rotations),
                [](float value) { return !std::isfinite(value); })) {
            add(true, "theme", "Dialogue text rotations must be finite");
        }
        if (!std::isfinite(theme.choiceTextOffsetX) ||
            !std::isfinite(theme.choiceTextOffsetY)) {
            add(true, "theme.choiceTextRect",
                "Choice text offsets must be finite");
        }
        if (!IsValidTextStyle(theme.speakerTextStyle)) {
            add(true, "theme.speakerTextStyle",
                "Speaker text style is outside the supported range");
        }
        if (!IsValidTextStyle(theme.bodyTextStyle)) {
            add(true, "theme.bodyTextStyle",
                "Body text style is outside the supported range");
        }
        if (!IsValidTextStyle(theme.choiceTextStyle)) {
            add(true, "theme.choiceTextStyle",
                "Choice text style is outside the supported range");
        }
        return issues;
    }

    std::vector<DialogueValidationIssue> ValidateDialogue(
        const DialogueDefinition& definition) {
        std::vector<DialogueValidationIssue> issues{};
        auto add = [&issues](bool error, std::string path,
                            std::string message) {
            issues.push_back(DialogueValidationIssue{
                error, std::move(path), std::move(message) });
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
            }
            else if (!nodes.emplace(node.id, &node).second) {
                add(true, path + ".id", "Node id must be unique");
            }
            if (node.type == DialogueNodeType::Line &&
                node.text.values.empty()) {
                add(true, path + ".text", "Line text cannot be empty");
            }
            if (node.type == DialogueNodeType::Choice) {
                if (node.choices.empty()) {
                    add(true, path + ".choices",
                        "Choice node must contain at least one choice");
                }
                std::unordered_set<std::string> choiceIds{};
                for (size_t choiceIndex = 0u;
                     choiceIndex < node.choices.size();
                     ++choiceIndex) {
                    const DialogueChoice& choice = node.choices[choiceIndex];
                    const std::string choicePath = path + ".choices[" +
                        std::to_string(choiceIndex) + "]";
                    if (choice.id.empty() ||
                        !choiceIds.insert(choice.id).second) {
                        add(true, choicePath + ".id",
                            "Choice id must be non-empty and unique");
                    }
                    if (choice.text.values.empty()) {
                        add(true, choicePath + ".text",
                            "Choice text cannot be empty");
                    }
                }
            }
            if (node.type == DialogueNodeType::Event &&
                node.eventId.empty()) {
                add(true, path + ".eventId",
                    "Event node requires an event id");
            }
        }
        if (!nodes.contains(definition.entryNodeId)) {
            add(true, "entryNodeId", "Entry node does not exist");
        }
        auto validateTarget = [&nodes, &add](
            std::string_view target,
            const std::string& path) {
            if (target.empty()) {
                add(true, path, "A non-end node requires a target");
            }
            else if (!nodes.contains(std::string(target))) {
                add(true, path, "Target node does not exist");
            }
        };
        for (size_t index = 0u; index < definition.nodes.size(); ++index) {
            const DialogueNode& node = definition.nodes[index];
            const std::string path = "nodes[" + std::to_string(index) + "]";
            if (node.type == DialogueNodeType::Line ||
                node.type == DialogueNodeType::Event) {
                validateTarget(node.nextNodeId, path + ".nextNodeId");
            }
            else if (node.type == DialogueNodeType::Choice) {
                for (size_t choiceIndex = 0u;
                     choiceIndex < node.choices.size();
                     ++choiceIndex) {
                    validateTarget(
                        node.choices[choiceIndex].nextNodeId,
                        path + ".choices[" +
                            std::to_string(choiceIndex) + "].nextNodeId");
                }
            }
        }
        return issues;
    }

    const DialogueNode* FindDialogueNode(
        const DialogueDefinition& definition,
        std::string_view nodeId) noexcept {
        const auto found = std::find_if(
            definition.nodes.begin(),
            definition.nodes.end(),
            [nodeId](const DialogueNode& node) {
                return node.id == nodeId;
            });
        return found != definition.nodes.end() ? &*found : nullptr;
    }

    std::string_view ResolveLocalizedText(
        const LocalizedText& text,
        std::string_view requestedLocale,
        std::string_view fallbackLocale) noexcept {
        auto exact = [&text](std::string_view locale) -> std::string_view {
            const auto found = text.values.find(locale);
            return found == text.values.end()
                ? std::string_view{}
                : std::string_view(found->second);
        };
        if (const std::string_view value = exact(requestedLocale);
            !value.empty()) return value;
        const std::string_view requestedBase = BaseLocale(requestedLocale);
        for (const auto& [locale, value] : text.values) {
            if (BaseLocale(locale) == requestedBase && !value.empty()) {
                return value;
            }
        }
        if (const std::string_view value = exact(fallbackLocale);
            !value.empty()) return value;
        const std::string_view fallbackBase = BaseLocale(fallbackLocale);
        for (const auto& [locale, value] : text.values) {
            if (BaseLocale(locale) == fallbackBase && !value.empty()) {
                return value;
            }
        }
        if (const std::string_view value = exact("en-US");
            !value.empty()) return value;
        for (const auto& [_, value] : text.values) {
            if (!value.empty()) return value;
        }
        return {};
    }

    void SerializeDialogueJson(
        const DialogueDefinition& definition,
        nlohmann::json& outJson) {
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
                    { "id", choice.id },
                    { "text", std::move(choiceText) },
                    { "nextNodeId", choice.nextNodeId },
                });
            }
            nodes.push_back(json{
                { "id", node.id },
                { "type", ToString(node.type) },
                { "speakerId", node.speakerId },
                { "speaker", std::move(speaker) },
                { "text", std::move(text) },
                { "nextNodeId", node.nextNodeId },
                { "eventId", node.eventId },
                { "choices", std::move(choices) },
                { "autoAdvanceSeconds", node.autoAdvanceSeconds },
                { "skippable", node.skippable },
                { "portraitSlot", ToString(node.portraitSlot) },
                { "portraitAction", ToString(node.portraitAction) },
            });
        }
        outJson = json{
            { "name", normalized.name },
            { "entryNodeId", normalized.entryNodeId },
            { "fallbackLocale", normalized.fallbackLocale },
            { "nodes", std::move(nodes) },
        };
    }

    bool DeserializeDialogueJson(
        const nlohmann::json& jsonNode,
        DialogueDefinition& outDefinition,
        std::string* outError) {
        if (!jsonNode.is_object() ||
            !jsonNode.contains("nodes") ||
            !jsonNode["nodes"].is_array()) {
            SetError(outError, "Dialogue payload must contain a node array");
            return false;
        }
        DialogueDefinition definition{};
        definition.name = jsonNode.value("name", definition.name);
        definition.entryNodeId = jsonNode.value(
            "entryNodeId", definition.entryNodeId);
        definition.fallbackLocale = jsonNode.value(
            "fallbackLocale", definition.fallbackLocale);
        if (const auto theme = jsonNode.find("theme");
            theme != jsonNode.end() &&
            !DeserializeTheme(*theme, definition.theme)) {
            SetError(outError, "Dialogue theme is invalid");
            return false;
        }
        for (const json& nodeJson : jsonNode["nodes"]) {
            if (!nodeJson.is_object()) {
                SetError(outError, "Dialogue node must be an object");
                return false;
            }
            DialogueNode node{};
            node.id = nodeJson.value("id", std::string{});
            node.speakerId = nodeJson.value(
                "speakerId", std::string{});
            const std::string type = nodeJson.value("type", "Line");
            if (!TryParseDialogueNodeType(type, node.type)) {
                SetError(outError, "Dialogue node type is invalid: " + type);
                return false;
            }
            const std::string portraitSlot = nodeJson.value(
                "portraitSlot", "Auto");
            if (!TryParseDialoguePortraitSlot(
                    portraitSlot,
                    node.portraitSlot)) {
                SetError(
                    outError,
                    "Dialogue portrait slot is invalid: " + portraitSlot);
                return false;
            }
            const std::string portraitAction = nodeJson.value(
                "portraitAction", "Auto");
            if (!TryParseDialoguePortraitAction(
                    portraitAction,
                    node.portraitAction)) {
                SetError(
                    outError,
                    "Dialogue portrait action is invalid: " +
                        portraitAction);
                return false;
            }
            if (const auto speaker = nodeJson.find("speaker");
                speaker != nodeJson.end() &&
                !DeserializeLocalized(*speaker, node.speaker)) {
                SetError(outError, "Dialogue speaker localization is invalid");
                return false;
            }
            if (const auto text = nodeJson.find("text");
                text != nodeJson.end() &&
                !DeserializeLocalized(*text, node.text)) {
                SetError(outError, "Dialogue text localization is invalid");
                return false;
            }
            node.nextNodeId = nodeJson.value(
                "nextNodeId", std::string{});
            node.eventId = nodeJson.value("eventId", std::string{});
            node.autoAdvanceSeconds = nodeJson.value(
                "autoAdvanceSeconds", 0.0f);
            node.skippable = nodeJson.value("skippable", true);
            if (const auto choices = nodeJson.find("choices");
                choices != nodeJson.end()) {
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
                    choice.nextNodeId = choiceJson.value(
                        "nextNodeId", std::string{});
                    const auto choiceText = choiceJson.find("text");
                    if (choiceText == choiceJson.end() ||
                        !DeserializeLocalized(*choiceText, choice.text)) {
                        SetError(outError,
                            "Dialogue choice localization is invalid");
                        return false;
                    }
                    node.choices.push_back(std::move(choice));
                }
            }
            definition.nodes.push_back(std::move(node));
        }
        NormalizeDialogue(definition);
        outDefinition = std::move(definition);
        if (outError != nullptr) outError->clear();
        return true;
    }

} // namespace HIKARI::DIALOGUE
