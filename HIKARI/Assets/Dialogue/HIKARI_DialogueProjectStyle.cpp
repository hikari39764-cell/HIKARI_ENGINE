#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace HIKARI::DIALOGUE {
    namespace {
        using json = nlohmann::json;

        constexpr uint32_t kProjectStyleFormatVersion = 1u;

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage != nullptr) *outMessage = std::move(message);
        }

        void NormalizeColor(DialogueColor& color) noexcept {
            float* channels[]{ &color.r, &color.g, &color.b };
            for (float* channel : channels) {
                if (!std::isfinite(*channel)) *channel = 0.0f;
                *channel = std::clamp(*channel, 0.0f, 8.0f);
            }
            if (!std::isfinite(color.a)) color.a = 1.0f;
            color.a = std::clamp(color.a, 0.0f, 1.0f);
        }

        bool IsFinitePositive(float value) noexcept {
            return std::isfinite(value) && value > 0.0f;
        }

        bool IsFiniteInRange(
            float value,
            float minimum,
            float maximum) noexcept {
            return std::isfinite(value) &&
                value >= minimum && value <= maximum;
        }

        json SerializeColor(const DialogueColor& color) {
            return json::array({ color.r, color.g, color.b, color.a });
        }

        bool ReadColor(
            const json& object,
            const char* key,
            DialogueColor& outColor) {
            const auto found = object.find(key);
            if (found == object.end()) return true;
            if (!found->is_array() || found->size() != 4u) return false;
            DialogueColor color{};
            float* channels[]{ &color.r, &color.g, &color.b, &color.a };
            for (size_t index = 0u; index < 4u; ++index) {
                if (!(*found)[index].is_number()) return false;
                *channels[index] = (*found)[index].get<float>();
            }
            outColor = color;
            return true;
        }

        json SerializeSlot(const DialoguePortraitSlotStyle& slot) {
            return json{
                { "rect", { slot.x, slot.y, slot.width, slot.height } },
                { "rotationDegrees", slot.rotationDegrees },
            };
        }

        bool ReadSlot(
            const json& object,
            const char* key,
            DialoguePortraitSlotStyle& outSlot) {
            const auto found = object.find(key);
            if (found == object.end()) return true;
            if (!found->is_object()) return false;
            DialoguePortraitSlotStyle slot = outSlot;
            if (const auto rect = found->find("rect");
                rect != found->end()) {
                if (!rect->is_array() || rect->size() != 4u) return false;
                float* values[]{ &slot.x, &slot.y, &slot.width, &slot.height };
                for (size_t index = 0u; index < 4u; ++index) {
                    if (!(*rect)[index].is_number()) return false;
                    *values[index] = (*rect)[index].get<float>();
                }
            }
            slot.rotationDegrees = found->value(
                "rotationDegrees", slot.rotationDegrees);
            outSlot = slot;
            return true;
        }

        json SerializeTransition(
            const DialoguePortraitTransitionStyle& transition) {
            return json{
                { "speakingTint", SerializeColor(transition.speakingTint) },
                { "listeningTint", SerializeColor(transition.listeningTint) },
                { "edgeLightColor",
                    SerializeColor(transition.edgeLightColor) },
                { "speakingScale", transition.speakingScale },
                { "listeningScale", transition.listeningScale },
                { "focusSeconds", transition.focusSeconds },
                { "enterSeconds", transition.enterSeconds },
                { "exitSeconds", transition.exitSeconds },
                { "slideDistance", transition.slideDistance },
                { "edgeLightIntensity",
                    transition.edgeLightIntensity },
            };
        }

        bool ReadTransition(
            const json& object,
            DialoguePortraitTransitionStyle& outTransition) {
            if (!object.is_object()) return false;
            DialoguePortraitTransitionStyle transition = outTransition;
            if (!ReadColor(object, "speakingTint", transition.speakingTint) ||
                !ReadColor(object, "listeningTint", transition.listeningTint) ||
                !ReadColor(
                    object,
                    "edgeLightColor",
                    transition.edgeLightColor)) {
                return false;
            }
            transition.speakingScale = object.value(
                "speakingScale", transition.speakingScale);
            transition.listeningScale = object.value(
                "listeningScale", transition.listeningScale);
            transition.focusSeconds = object.value(
                "focusSeconds", transition.focusSeconds);
            transition.enterSeconds = object.value(
                "enterSeconds", transition.enterSeconds);
            transition.exitSeconds = object.value(
                "exitSeconds", transition.exitSeconds);
            transition.slideDistance = object.value(
                "slideDistance", transition.slideDistance);
            transition.edgeLightIntensity = object.value(
                "edgeLightIntensity", transition.edgeLightIntensity);
            outTransition = transition;
            return true;
        }

        void UseDefaultAssetIfPresent(
            const std::filesystem::path& projectRoot,
            std::string& destination,
            const char* relative) {
            std::error_code error{};
            if (destination.empty() &&
                std::filesystem::is_regular_file(
                    projectRoot / "Assets" / relative,
                    error) &&
                !error) {
                destination = relative;
            }
        }
    }

    DialogueProjectStyle MakeDefaultDialogueProjectStyle(
        const std::filesystem::path& projectRoot) {
        DialogueProjectStyle style{};
        if (!projectRoot.empty()) {
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.fontAssetPath,
                "Fonts/NotoSansCJKjp-Regular.otf");
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.dialoguePanelAssetPath,
                "UI/Dialogue/Dialog_Box.png");
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.speakerPlateAssetPath,
                "UI/Dialogue/Dialog_Name.png");
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.choiceAssetPath,
                "UI/Dialogue/Choose_Back.png");
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.advanceGamepadAssetPath,
                "UI/Dialogue/Continue_Gamepad.png");
            UseDefaultAssetIfPresent(
                projectRoot,
                style.theme.advanceMouseAssetPath,
                "UI/Dialogue/Continue_Mouse.png");
        }
        NormalizeDialogueProjectStyle(style);
        return style;
    }

    void NormalizeDialogueProjectStyle(DialogueProjectStyle& style) {
        if (style.name.empty()) style.name = "Project Dialogue Style";
        if (style.fallbackLocale.empty()) style.fallbackLocale = "ja-JP";

        DialogueDefinition normalizationHost{};
        normalizationHost.theme = style.theme;
        NormalizeDialogue(normalizationHost);
        style.theme = std::move(normalizationHost.theme);

        const auto normalizeSlot = [](DialoguePortraitSlotStyle& slot) {
            if (!std::isfinite(slot.x)) slot.x = 0.0f;
            if (!std::isfinite(slot.y)) slot.y = 0.0f;
            if (!IsFinitePositive(slot.width)) slot.width = 680.0f;
            if (!IsFinitePositive(slot.height)) slot.height = 900.0f;
            if (!std::isfinite(slot.rotationDegrees)) {
                slot.rotationDegrees = 0.0f;
            }
            slot.width = std::clamp(slot.width, 1.0f, 8192.0f);
            slot.height = std::clamp(slot.height, 1.0f, 8192.0f);
            slot.rotationDegrees = std::clamp(
                slot.rotationDegrees,
                -180.0f,
                180.0f);
        };
        normalizeSlot(style.leftPortrait);
        normalizeSlot(style.rightPortrait);

        auto& transition = style.portraitTransition;
        NormalizeColor(transition.speakingTint);
        NormalizeColor(transition.listeningTint);
        NormalizeColor(transition.edgeLightColor);
        if (!std::isfinite(transition.speakingScale)) {
            transition.speakingScale = 1.0f;
        }
        if (!std::isfinite(transition.listeningScale)) {
            transition.listeningScale = 0.965f;
        }
        if (!std::isfinite(transition.focusSeconds)) {
            transition.focusSeconds = 0.18f;
        }
        if (!std::isfinite(transition.enterSeconds)) {
            transition.enterSeconds = 0.24f;
        }
        if (!std::isfinite(transition.exitSeconds)) {
            transition.exitSeconds = 0.18f;
        }
        if (!std::isfinite(transition.slideDistance)) {
            transition.slideDistance = 44.0f;
        }
        if (!std::isfinite(transition.edgeLightIntensity)) {
            transition.edgeLightIntensity = 0.34f;
        }
        transition.speakingScale = std::clamp(
            transition.speakingScale, 0.25f, 3.0f);
        transition.listeningScale = std::clamp(
            transition.listeningScale, 0.25f, 3.0f);
        transition.focusSeconds = std::clamp(
            transition.focusSeconds, 0.01f, 4.0f);
        transition.enterSeconds = std::clamp(
            transition.enterSeconds, 0.01f, 4.0f);
        transition.exitSeconds = std::clamp(
            transition.exitSeconds, 0.01f, 4.0f);
        transition.slideDistance = std::clamp(
            transition.slideDistance, 0.0f, 1024.0f);
        transition.edgeLightIntensity = std::clamp(
            transition.edgeLightIntensity, 0.0f, 8.0f);

        for (DialogueSpeakerProfile& speaker : style.speakers) {
            if (!std::isfinite(speaker.offsetX)) speaker.offsetX = 0.0f;
            if (!std::isfinite(speaker.offsetY)) speaker.offsetY = 0.0f;
            if (!std::isfinite(speaker.scale)) speaker.scale = 1.0f;
            if (!std::isfinite(speaker.rotationDegrees)) {
                speaker.rotationDegrees = 0.0f;
            }
            speaker.scale = std::clamp(speaker.scale, 0.1f, 8.0f);
            speaker.rotationDegrees = std::clamp(
                speaker.rotationDegrees,
                -180.0f,
                180.0f);
        }
    }

    std::vector<DialogueValidationIssue> ValidateDialogueProjectStyle(
        const DialogueProjectStyle& style) {
        std::vector<DialogueValidationIssue> issues{};
        const auto add = [&issues](std::string path, std::string message) {
            issues.push_back(DialogueValidationIssue{
                true, std::move(path), std::move(message) });
        };

        for (DialogueValidationIssue issue :
             ValidateDialogueTheme(style.theme)) {
            issue.path = "appearance." + issue.path;
            issues.push_back(std::move(issue));
        }

        const DialoguePortraitSlotStyle* slots[]{
            &style.leftPortrait, &style.rightPortrait };
        for (size_t index = 0u; index < 2u; ++index) {
            const DialoguePortraitSlotStyle& slot = *slots[index];
            if (!std::isfinite(slot.x) || !std::isfinite(slot.y) ||
                !IsFinitePositive(slot.width) ||
                !IsFinitePositive(slot.height) ||
                !std::isfinite(slot.rotationDegrees)) {
                add(
                    index == 0u ? "portraits.left" : "portraits.right",
                    "Portrait slot transform is invalid");
            }
        }

        const auto& transition = style.portraitTransition;
        if (!IsFiniteInRange(transition.speakingScale, 0.25f, 3.0f) ||
            !IsFiniteInRange(transition.listeningScale, 0.25f, 3.0f) ||
            !IsFiniteInRange(transition.focusSeconds, 0.01f, 4.0f) ||
            !IsFiniteInRange(transition.enterSeconds, 0.01f, 4.0f) ||
            !IsFiniteInRange(transition.exitSeconds, 0.01f, 4.0f) ||
            !IsFiniteInRange(transition.slideDistance, 0.0f, 1024.0f) ||
            !IsFiniteInRange(
                transition.edgeLightIntensity, 0.0f, 8.0f)) {
            add("portraits.motion", "Portrait transition is invalid");
        }

        std::unordered_set<std::string> speakerIds{};
        for (size_t index = 0u; index < style.speakers.size(); ++index) {
            const DialogueSpeakerProfile& speaker = style.speakers[index];
            const std::string path = "speakers[" +
                std::to_string(index) + "]";
            if (speaker.id.empty() ||
                !speakerIds.insert(speaker.id).second) {
                add(path + ".id", "Character id must be non-empty and unique");
            }
            if (speaker.displayName.values.empty()) {
                add(path + ".displayName", "Character needs a display name");
            }
            if (!std::isfinite(speaker.offsetX) ||
                !std::isfinite(speaker.offsetY) ||
                !IsFiniteInRange(speaker.scale, 0.1f, 8.0f) ||
                !IsFiniteInRange(
                    speaker.rotationDegrees, -180.0f, 180.0f)) {
                add(path + ".portrait", "Character portrait transform is invalid");
            }
        }
        return issues;
    }

    const DialogueSpeakerProfile* FindDialogueSpeaker(
        const DialogueProjectStyle& style,
        std::string_view speakerId) noexcept {
        const auto found = std::find_if(
            style.speakers.begin(),
            style.speakers.end(),
            [speakerId](const DialogueSpeakerProfile& speaker) {
                return speaker.id == speakerId;
            });
        return found != style.speakers.end() ? &*found : nullptr;
    }

    DialogueSpeakerProfile* FindDialogueSpeaker(
        DialogueProjectStyle& style,
        std::string_view speakerId) noexcept {
        return const_cast<DialogueSpeakerProfile*>(FindDialogueSpeaker(
            static_cast<const DialogueProjectStyle&>(style),
            speakerId));
    }

    void SerializeDialogueProjectStyleJson(
        const DialogueProjectStyle& style,
        nlohmann::json& outJson) {
        DialogueProjectStyle normalized = style;
        NormalizeDialogueProjectStyle(normalized);
        json theme{};
        SerializeDialogueThemeJson(normalized.theme, theme);
        json speakers = json::array();
        for (const DialogueSpeakerProfile& speaker : normalized.speakers) {
            json displayName{};
            SerializeLocalizedTextJson(speaker.displayName, displayName);
            speakers.push_back(json{
                { "id", speaker.id },
                { "displayName", std::move(displayName) },
                { "portraitAssetGuid", speaker.portraitAssetGuid },
                { "preferredSlot", ToString(speaker.preferredSlot) },
                { "offset", { speaker.offsetX, speaker.offsetY } },
                { "scale", speaker.scale },
                { "rotationDegrees", speaker.rotationDegrees },
                { "mirror", speaker.mirror },
            });
        }
        outJson = json{
            { "formatVersion", kProjectStyleFormatVersion },
            { "name", normalized.name },
            { "fallbackLocale", normalized.fallbackLocale },
            { "appearance", std::move(theme) },
            { "portraitSlots", {
                { "left", SerializeSlot(normalized.leftPortrait) },
                { "right", SerializeSlot(normalized.rightPortrait) },
                { "transition",
                    SerializeTransition(normalized.portraitTransition) },
            } },
            { "characters", std::move(speakers) },
        };
    }

    bool DeserializeDialogueProjectStyleJson(
        const nlohmann::json& jsonNode,
        DialogueProjectStyle& outStyle,
        std::string* outError) {
        if (!jsonNode.is_object() ||
            jsonNode.value("formatVersion", 0u) !=
                kProjectStyleFormatVersion) {
            SetMessage(outError, "Unsupported dialogue UI style version");
            return false;
        }
        DialogueProjectStyle style{};
        style.name = jsonNode.value("name", style.name);
        style.fallbackLocale = jsonNode.value(
            "fallbackLocale", style.fallbackLocale);
        const auto appearance = jsonNode.find("appearance");
        if (appearance == jsonNode.end() ||
            !DeserializeDialogueThemeJson(*appearance, style.theme)) {
            SetMessage(outError, "Dialogue UI appearance is invalid");
            return false;
        }
        if (const auto portraits = jsonNode.find("portraitSlots");
            portraits != jsonNode.end()) {
            if (!portraits->is_object() ||
                !ReadSlot(*portraits, "left", style.leftPortrait) ||
                !ReadSlot(*portraits, "right", style.rightPortrait)) {
                SetMessage(outError, "Dialogue portrait layout is invalid");
                return false;
            }
            if (const auto transition = portraits->find("transition");
                transition != portraits->end() &&
                !ReadTransition(*transition, style.portraitTransition)) {
                SetMessage(outError, "Dialogue portrait motion is invalid");
                return false;
            }
        }
        if (const auto characters = jsonNode.find("characters");
            characters != jsonNode.end()) {
            if (!characters->is_array()) {
                SetMessage(outError, "Dialogue character list is invalid");
                return false;
            }
            for (const json& character : *characters) {
                if (!character.is_object()) {
                    SetMessage(outError, "Dialogue character is invalid");
                    return false;
                }
                DialogueSpeakerProfile speaker{};
                speaker.id = character.value("id", std::string{});
                speaker.portraitAssetGuid = character.value(
                    "portraitAssetGuid", std::string{});
                const auto displayName = character.find("displayName");
                if (displayName == character.end() ||
                    !DeserializeLocalizedTextJson(
                        *displayName,
                        speaker.displayName)) {
                    SetMessage(outError, "Dialogue character name is invalid");
                    return false;
                }
                const std::string preferredSlot = character.value(
                    "preferredSlot", "Auto");
                if (!TryParseDialoguePortraitSlot(
                        preferredSlot,
                        speaker.preferredSlot)) {
                    SetMessage(outError, "Dialogue character side is invalid");
                    return false;
                }
                if (const auto offset = character.find("offset");
                    offset != character.end()) {
                    if (!offset->is_array() || offset->size() != 2u ||
                        !(*offset)[0].is_number() ||
                        !(*offset)[1].is_number()) {
                        SetMessage(outError, "Dialogue character offset is invalid");
                        return false;
                    }
                    speaker.offsetX = (*offset)[0].get<float>();
                    speaker.offsetY = (*offset)[1].get<float>();
                }
                speaker.scale = character.value("scale", speaker.scale);
                speaker.rotationDegrees = character.value(
                    "rotationDegrees", speaker.rotationDegrees);
                speaker.mirror = character.value("mirror", speaker.mirror);
                style.speakers.push_back(std::move(speaker));
            }
        }
        NormalizeDialogueProjectStyle(style);
        const auto issues = ValidateDialogueProjectStyle(style);
        const auto error = std::find_if(
            issues.begin(),
            issues.end(),
            [](const DialogueValidationIssue& issue) {
                return issue.error;
            });
        if (error != issues.end()) {
            SetMessage(outError, error->path + ": " + error->message);
            return false;
        }
        outStyle = std::move(style);
        if (outError != nullptr) outError->clear();
        return true;
    }

} // namespace HIKARI::DIALOGUE
