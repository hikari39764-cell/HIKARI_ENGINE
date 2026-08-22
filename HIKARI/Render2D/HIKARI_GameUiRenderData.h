#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "HIKARI/Project/UiTypes.h"

namespace HIKARI::GAME_UI {

    struct GameUiSpriteDraw {
        std::string absoluteAssetPath{};
        GameProjectUiSpriteRequest request{};
        uint64_t submissionSequence = 0u;
    };

    struct GameUiFrameSource {
        std::vector<GameUiSpriteDraw> sprites{};
        uint64_t frameIndex = 0u;
        uint64_t revision = 0u;

        bool HasSprites() const noexcept {
            return frameIndex != 0u && !sprites.empty();
        }
    };

} // namespace HIKARI::GAME_UI
