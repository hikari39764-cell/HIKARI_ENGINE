#pragma once

#include <cstdint>
#include <vector>

#include "Render2D/HIKARI_GameUiRenderData.h"
#include "Render3D/Text/HIKARI_SdfTextLayout.h"

namespace HIKARI::GAME_UI {

    struct GameUiTextGlyphDraw {
        int textureHandle = -1;
        GameProjectUiSpriteRequest request{};
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        uint64_t submissionSequence = 0u;
        uint32_t glyphSequence = 0u;
    };

    bool BuildGameUiTextGlyphDraws(
        const std::vector<GameUiTextDraw>& texts,
        std::vector<GameUiTextGlyphDraw>& outDraws,
        TEXT_RENDERING::SdfTextLayoutLease* outAtlasLease = nullptr) noexcept;
    void ShutdownGameUiTextLayout() noexcept;

} // namespace HIKARI::GAME_UI
