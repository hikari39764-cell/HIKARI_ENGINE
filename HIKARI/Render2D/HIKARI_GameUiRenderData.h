#pragma once

#include "Core/Math/HIKARI_Affine2D.h"
#include <cstdint>
#include <string>
#include <vector>

#include "HIKARI/Project/UiTypes.h"

namespace HIKARI::GAME_UI {

struct GameUiTextEffectStyle {
    GameProjectUiColor glowColor{};
    float glowWidth = 0.0f;
    float glowIntensity = 0.0f;
    bool operator==(const GameUiTextEffectStyle&) const noexcept = default;
};

struct GameUiSpriteDraw {
    std::string absoluteAssetPath{};
    GameProjectUiSpriteRequest request{};
    Affine2D transform{};
    uint64_t submissionSequence = 0u;
};

struct GameUiTextDraw {
    std::string absoluteFontAssetPath{};
    uint64_t fontRevision = 0;
    std::string utf8Text{};
    GameProjectUiTextRequest request{};
    Affine2D transform{};
    GameUiTextEffectStyle style{};
    uint64_t submissionSequence = 0u;
};

struct GameUiBackdropDraw {
    GameProjectUiBackdropRequest request{};
    uint64_t submissionSequence = 0u;
};

struct GameUiFrameSource {
    std::vector<GameUiSpriteDraw> sprites{};
    std::vector<GameUiTextDraw> texts{};
    GameUiBackdropDraw backdrop{};
    bool hasBackdrop = false;
    uint64_t frameIndex = 0u;
    uint64_t revision = 0u;

    bool HasSprites() const noexcept { return frameIndex != 0u && !sprites.empty(); }

    bool HasDraws() const noexcept { return frameIndex != 0u && (!sprites.empty() || !texts.empty()); }
};

} // namespace HIKARI::GAME_UI
