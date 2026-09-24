#include "Gameplay/Dialogue/HIKARI_DialoguePresentation.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include <algorithm>
#include <cmath>
#include <string>
namespace HIKARI::DIALOGUE {
namespace {
GameProjectUiColor White(float alpha = 1.0f) noexcept { return GameProjectUiColor{1.0f, 1.0f, 1.0f, alpha}; }

GameProjectUiColor InkBlue(float alpha = 1.0f) noexcept {
    return GameProjectUiColor{0.0156863f, 0.1686275f, 0.5058824f, alpha};
}

GameProjectUiColor ToUiColor(const DialogueColor& color) noexcept {
    return GameProjectUiColor{color.r, color.g, color.b, color.a};
}

GameProjectUiColor LerpUiColor(const DialogueColor& from, const DialogueColor& to, float amount) noexcept {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    return GameProjectUiColor{
        from.r + (to.r - from.r) * t,
        from.g + (to.g - from.g) * t,
        from.b + (to.b - from.b) * t,
        from.a + (to.a - from.a) * t,
    };
}

} // namespace

bool SubmitDialoguePresentation(GAME_UI::GameUiPresentationService& service,
                                const DialoguePresentationState& state) noexcept {
    try {
        if (state.definition == nullptr || state.style == nullptr || state.node == nullptr ||
            state.sourceIdBase == 0u) {
            return false;
        }
        const DialogueTheme& theme = state.style->theme;
        bool textReady = true;
        const std::string_view fallbackLocale = state.style != nullptr && !state.style->fallbackLocale.empty()
                                                    ? std::string_view(state.style->fallbackLocale)
                                                    : std::string_view(state.definition->fallbackLocale);
        const float opening =
            std::clamp(state.sessionElapsed / (std::max)(theme.panelRevealSeconds, 0.01f), 0.0f, 1.0f);
        const float easedOpening = 1.0f - std::pow(1.0f - opening, 3.0f);
        const uint64_t base = state.sourceIdBase;

        GameProjectUiBackdropRequest backdrop{};
        backdrop.sourceId = base;
        backdrop.priority = 2000;
        backdrop.flags = kGameProjectUiSuppressNonModal;
        backdrop.dimAmount = theme.backdropDim;
        backdrop.blurRadiusPixels = theme.backdropBlur * 12.0f;
        backdrop.desaturation = theme.backdropDesaturation;
        backdrop.vignette = 0.24f;
        backdrop.progress = easedOpening;
        backdrop.tint = GameProjectUiColor{0.0156863f, 0.0352941f, 0.1019608f, 1.0f};
        (void)service.SubmitBackdrop(backdrop);

        if (state.style != nullptr) {
            const auto submitPortrait = [&](const DialoguePortraitPresentationState& portrait,
                                            const DialoguePortraitSlotStyle& slot, bool left, uint64_t sourceOffset,
                                            int32_t order) {
                if (portrait.speakerId.empty() || portrait.presence <= 0.001f) {
                    return;
                }
                const DialogueSpeakerProfile* speaker = FindDialogueSpeaker(*state.style, portrait.speakerId);
                if (speaker == nullptr || speaker->resolvedPortraitAssetPath.empty()) {
                    return;
                }
                const float focus = std::clamp(portrait.focus, 0.0f, 1.0f);
                const float presence = std::clamp(portrait.presence, 0.0f, 1.0f);
                const auto& transition = state.style->portraitTransition;
                const float focusScale =
                    transition.listeningScale + (transition.speakingScale - transition.listeningScale) * focus;
                const float scale = speaker->scale * focusScale;
                const float width = slot.width * scale;
                const float height = slot.height * scale;
                const float slideDirection = left ? -1.0f : 1.0f;

                GameProjectUiSpriteRequest request{};
                request.sourceId = base + sourceOffset;
                request.order = order;
                request.flags = kGameProjectUiSubmissionModal;
                request.referenceWidth = theme.referenceWidth;
                request.referenceHeight = theme.referenceHeight;
                request.x = slot.x + (slot.width - width) * 0.5f + speaker->offsetX +
                            slideDirection * transition.slideDistance * (1.0f - presence);
                request.y = slot.y + (slot.height - height) + speaker->offsetY;
                request.width = width;
                request.height = height;
                request.pivotX = 0.5f;
                request.pivotY = 1.0f;
                request.rotationRadians = (slot.rotationDegrees + speaker->rotationDegrees) * 0.01745329252f;
                request.fillAmount = focus;
                request.effectProgress = presence;
                request.effectIntensity = transition.edgeLightIntensity;
                request.timeSeconds = state.sessionElapsed;
                request.directionX = speaker->mirror ? -1.0f : 1.0f;
                request.tint = LerpUiColor(transition.listeningTint, transition.speakingTint, focus);
                request.tint.a *= presence * easedOpening;
                request.effectColor = ToUiColor(transition.edgeLightColor);
                request.highlightColor = White();
                request.effect = GameProjectUiEffect::DialoguePortraitFocus;
                (void)service.SubmitSprite(speaker->resolvedPortraitAssetPath, request);
            };
            submitPortrait(state.leftPortrait, state.style->leftPortrait, true, 10u, 760);
            submitPortrait(state.rightPortrait, state.style->rightPortrait, false, 11u, 770);
        }

        GameProjectUiSpriteRequest panel{};
        panel.sourceId = base + 1u;
        panel.order = 900;
        panel.flags = kGameProjectUiSubmissionModal;
        panel.referenceWidth = theme.referenceWidth;
        panel.referenceHeight = theme.referenceHeight;
        panel.x = theme.panelX;
        panel.y = theme.panelY + (1.0f - easedOpening) * 26.0f;
        panel.width = theme.panelWidth;
        panel.height = theme.panelHeight;
        panel.pivotX = 0.5f;
        panel.pivotY = 0.5f;
        panel.effectProgress = opening;
        panel.effectIntensity = 1.15f;
        panel.timeSeconds = state.sessionElapsed;
        panel.tint = White(easedOpening);
        panel.effectColor = InkBlue();
        panel.highlightColor = White();
        panel.effect = GameProjectUiEffect::DialoguePanelReveal;
        if (!theme.dialoguePanelAssetPath.empty())
            (void)service.SubmitSprite(theme.dialoguePanelAssetPath, panel);

        const DialogueSpeakerProfile* speakerProfile =
            state.style != nullptr ? FindDialogueSpeaker(*state.style, state.node->speakerId) : nullptr;
        const std::string_view speaker =
            speakerProfile != nullptr ? ResolveLocalizedText(speakerProfile->displayName, state.locale, fallbackLocale)
                                      : ResolveLocalizedText(state.node->speaker, state.locale, fallbackLocale);
        if (!speaker.empty()) {
            if (!theme.speakerPlateAssetPath.empty()) {
                GameProjectUiSpriteRequest plate = panel;
                plate.sourceId = base + 2u;
                plate.order = 910;
                plate.x = theme.speakerPlateX;
                plate.y = theme.speakerPlateY;
                plate.width = theme.speakerPlateWidth;
                plate.height = theme.speakerPlateHeight;
                plate.effect = GameProjectUiEffect::DialoguePanelReveal;
                (void)service.SubmitSprite(theme.speakerPlateAssetPath, plate);
            }

            GameProjectUiTextRequest speakerText{};
            speakerText.sourceId = base + 3u;
            speakerText.order = 920;
            speakerText.flags = kGameProjectUiSubmissionModal;
            speakerText.referenceWidth = theme.referenceWidth;
            speakerText.referenceHeight = theme.referenceHeight;
            speakerText.x = theme.speakerTextX;
            speakerText.y = theme.speakerTextY;
            speakerText.maxWidth = theme.speakerTextWidth;
            speakerText.maxHeight = theme.speakerTextHeight;
            speakerText.rotationRadians = theme.speakerTextRotationDegrees * 0.01745329252f;
            speakerText.revealAmount = opening;
            textReady = service.SubmitTextStyle(theme.speakerTextStyle, speaker, speakerText, theme.speakerTextScale) &&
                        textReady;
        }

        const std::string_view body = ResolveLocalizedText(state.node->text, state.locale, fallbackLocale);
        if (!body.empty()) {
            GameProjectUiTextRequest bodyText{};
            bodyText.sourceId = base + 4u;
            bodyText.order = 930;
            bodyText.flags = kGameProjectUiSubmissionModal;
            bodyText.referenceWidth = theme.referenceWidth;
            bodyText.referenceHeight = theme.referenceHeight;
            bodyText.x = theme.bodyTextX;
            bodyText.y = theme.bodyTextY;
            bodyText.maxWidth = theme.bodyTextWidth;
            bodyText.maxHeight = theme.bodyTextHeight;
            bodyText.rotationRadians = theme.bodyTextRotationDegrees * 0.01745329252f;
            bodyText.revealAmount = std::clamp(state.revealAmount, 0.0f, 1.0f);
            bodyText.timeSeconds = state.sessionElapsed;
            textReady = service.SubmitTextStyle(theme.bodyTextStyle, body, bodyText, theme.bodyTextScale) && textReady;
        }

        if (state.node->type == DialogueNodeType::Line) {
            std::string_view promptAsset = state.useGamepadAdvancePrompt
                                               ? std::string_view(theme.advanceGamepadAssetPath)
                                               : std::string_view(theme.advanceMouseAssetPath);
            if (promptAsset.empty()) {
                promptAsset = state.useGamepadAdvancePrompt ? std::string_view(theme.advanceMouseAssetPath)
                                                            : std::string_view(theme.advanceGamepadAssetPath);
            }
            const float revealInput = std::clamp((state.revealAmount - 0.92f) / 0.08f, 0.0f, 1.0f);
            const float promptReveal = revealInput * revealInput * (3.0f - 2.0f * revealInput) * easedOpening;
            if (!promptAsset.empty() && promptReveal > 0.001f) {
                constexpr float kGamepadWidth = 59.0f;
                constexpr float kGamepadHeight = 104.0f;
                constexpr float kMouseWidth = 55.0f;
                constexpr float kMouseHeight = 112.0f;
                const float phase =
                    state.sessionElapsed / (std::max)(theme.advancePromptPulseSeconds, 0.20f) * 6.28318530718f;
                const float wave = 0.5f + 0.5f * std::sin(phase);
                const float animatedScale = theme.advancePromptScale * (1.0f + theme.advancePromptScaleAmount * wave);
                const float naturalWidth = state.useGamepadAdvancePrompt ? kGamepadWidth : kMouseWidth;
                const float naturalHeight = state.useGamepadAdvancePrompt ? kGamepadHeight : kMouseHeight;
                const float width = naturalWidth * animatedScale;
                const float height = naturalHeight * animatedScale;

                GameProjectUiSpriteRequest prompt{};
                prompt.sourceId = base + 5u;
                prompt.order = 935;
                prompt.flags = kGameProjectUiSubmissionModal;
                prompt.referenceWidth = theme.referenceWidth;
                prompt.referenceHeight = theme.referenceHeight;
                prompt.x = theme.advancePromptX - width * 0.5f;
                prompt.y = theme.advancePromptY - height + std::sin(phase) * theme.advancePromptBobDistance;
                prompt.width = width;
                prompt.height = height;
                prompt.pivotX = 0.5f;
                prompt.pivotY = 1.0f;
                prompt.effectProgress = promptReveal;
                prompt.effectIntensity = 0.32f + wave * 0.18f;
                prompt.timeSeconds = state.sessionElapsed;
                prompt.tint = White(promptReveal * (0.78f + wave * 0.22f));
                prompt.effectColor = InkBlue();
                prompt.highlightColor = White();
                prompt.effect = GameProjectUiEffect::DialoguePanelReveal;
                (void)service.SubmitSprite(promptAsset, prompt);
            }
        }

        if (state.node->type != DialogueNodeType::Choice) {
            return textReady;
        }
        for (size_t index = 0u; index < state.node->choices.size(); ++index) {
            const bool selected = index == state.selectedChoice;
            const float y = theme.choiceY + static_cast<float>(index) * (theme.choiceHeight + theme.choiceGap);
            GameProjectUiSpriteRequest choicePanel{};
            choicePanel.sourceId = base + 100u + index * 2u;
            choicePanel.order = 940 + static_cast<int32_t>(index) * 2;
            choicePanel.flags = kGameProjectUiSubmissionModal;
            choicePanel.referenceWidth = theme.referenceWidth;
            choicePanel.referenceHeight = theme.referenceHeight;
            choicePanel.x = theme.choiceX + (selected ? -12.0f : 0.0f);
            choicePanel.y = y;
            choicePanel.width = theme.choiceWidth;
            choicePanel.height = theme.choiceHeight;
            choicePanel.effectProgress = opening;
            choicePanel.effectIntensity = selected ? 1.25f : 0.75f;
            choicePanel.timeSeconds = state.sessionElapsed;
            choicePanel.tint = White(opening);
            choicePanel.effectColor = InkBlue();
            choicePanel.highlightColor = White();
            choicePanel.effect =
                selected ? GameProjectUiEffect::DialogueChoiceSelected : GameProjectUiEffect::DialogueChoiceIdle;
            if (!theme.choiceAssetPath.empty()) {
                (void)service.SubmitSprite(theme.choiceAssetPath, choicePanel);
            }

            const std::string_view label =
                ResolveLocalizedText(state.node->choices[index].text, state.locale, fallbackLocale);
            GameProjectUiTextRequest choiceText{};
            choiceText.sourceId = choicePanel.sourceId + 1u;
            choiceText.order = choicePanel.order + 1;
            choiceText.flags = kGameProjectUiSubmissionModal;
            choiceText.referenceWidth = theme.referenceWidth;
            choiceText.referenceHeight = theme.referenceHeight;
            choiceText.x = choicePanel.x + theme.choiceTextOffsetX;
            choiceText.y = y + theme.choiceTextOffsetY;
            choiceText.maxWidth = theme.choiceTextWidth;
            choiceText.maxHeight = theme.choiceTextHeight;
            choiceText.rotationRadians = theme.choiceTextRotationDegrees * 0.01745329252f;
            choiceText.horizontalAlign = GameProjectUiTextAlign::Center;
            choiceText.verticalAlign = GameProjectUiVerticalAlign::Center;
            choiceText.revealAmount = opening;
            textReady =
                service.SubmitTextStyle(theme.choiceTextStyle, label, choiceText, theme.choiceTextScale) && textReady;
        }
        return textReady;
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI::DIALOGUE
