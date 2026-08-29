#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

#include "Gameplay/Dialogue/HIKARI_DialogueDefinition.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"

namespace HIKARI::GAME_UI {
    class GameUiPresentationService;
}

namespace HIKARI::DIALOGUE {

    // A dialogue portrait actor is identified by its visual source and
    // authored presentation, never by the localized/display speaker name.
    // This keeps a character label change from creating a second visual
    // actor while still allowing intentionally different mirrored or
    // transformed variants of one source image to coexist.
    struct DialoguePortraitVisualIdentity {
        std::string_view assetGuid{};
        std::string_view assetPath{};
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float scale = 1.0f;
        float rotationDegrees = 0.0f;
        bool mirror = false;
    };

    inline bool SameDialoguePortraitVisualIdentity(
        const DialoguePortraitVisualIdentity& lhs,
        const DialoguePortraitVisualIdentity& rhs) noexcept {
        const bool sameAsset =
            (!lhs.assetGuid.empty() && !rhs.assetGuid.empty() &&
                lhs.assetGuid == rhs.assetGuid) ||
            (!lhs.assetPath.empty() && !rhs.assetPath.empty() &&
                lhs.assetPath == rhs.assetPath);
        constexpr float kVisualEpsilon = 0.0001f;
        return sameAsset && lhs.mirror == rhs.mirror &&
            std::abs(lhs.offsetX - rhs.offsetX) <= kVisualEpsilon &&
            std::abs(lhs.offsetY - rhs.offsetY) <= kVisualEpsilon &&
            std::abs(lhs.scale - rhs.scale) <= kVisualEpsilon &&
            std::abs(lhs.rotationDegrees - rhs.rotationDegrees) <=
                kVisualEpsilon;
    }

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
