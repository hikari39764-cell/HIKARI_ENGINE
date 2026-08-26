#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI::GAME_UI {
    class GameUiPresentationService;
}

namespace HIKARI::DIALOGUE {

    struct DialoguePortraitPresentationState {
        std::string_view speakerId{};
        float presence = 0.0f;
        float focus = 0.0f;
    };

    struct DialoguePresentationState {
        const DialogueDefinition* definition = nullptr;
        const DialogueProjectStyle* style = nullptr;
        const DialogueNode* node = nullptr;
        std::string_view locale{};
        uint64_t sourceIdBase = 1u;
        size_t selectedChoice = 0u;
        float sessionElapsed = 0.0f;
        float revealAmount = 1.0f;
        bool useGamepadAdvancePrompt = false;
        DialoguePortraitPresentationState leftPortrait{};
        DialoguePortraitPresentationState rightPortrait{};
    };

    // Shared production presenter used by both runtime playback and the
    // scene-independent dialogue asset preview.
    bool SubmitDialoguePresentation(
        GAME_UI::GameUiPresentationService& service,
        const DialoguePresentationState& state) noexcept;

} // namespace HIKARI::DIALOGUE
