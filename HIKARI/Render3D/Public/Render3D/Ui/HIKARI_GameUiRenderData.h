#pragma once

#include "Core/Math/HIKARI_Affine2D.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "RenderCore/Text/HIKARI_SdfTextLayout.h"

#include "HIKARI/Project/UiTypes.h"

namespace HIKARI::GAME_UI {

struct GameUiClipRect {
    bool enabled = false;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    bool operator==(const GameUiClipRect&) const noexcept = default;
};

struct GameUiTextEffectStyle {
    GameProjectUiColor glowColor{};
    float glowWidth = 0.0f;
    float glowIntensity = 0.0f;
    bool operator==(const GameUiTextEffectStyle&) const noexcept = default;
};

// Render 側の実測配置のみを共有する。Component を render thread に渡さない。
struct GameUiTextLayoutFeedback {
    std::mutex mutex;
    TEXT_RENDERING::SdfTextLayoutRequest expected;
    std::vector<TEXT_RENDERING::SdfTextCaretStop> stops;
    bool ready = false;
};

struct GameUiTextEditing {
    std::shared_ptr<GameUiTextLayoutFeedback> feedback;
    size_t caret = 0, selectionStart = 0, selectionEnd = 0, compositionStart = 0, compositionEnd = 0;
    bool focused = false;
    bool operator==(const GameUiTextEditing&) const noexcept = default;
};

struct GameUiSpriteDraw {
    std::string absoluteAssetPath{};
    GameProjectUiSpriteRequest request{};
    Affine2D transform{};
    GameUiClipRect clip{};
    uint64_t submissionSequence = 0u;
};

struct GameUiTextDraw {
    std::string absoluteFontAssetPath{};
    uint64_t fontRevision = 0;
    std::string utf8Text{};
    GameProjectUiTextRequest request{};
    Affine2D transform{};
    GameUiClipRect clip{};
    GameUiTextEffectStyle style{};
    GameUiTextEditing editing{};
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

    bool HasDraws() const noexcept { return frameIndex != 0u && (!sprites.empty() || !texts.empty()); }
};

} // namespace HIKARI::GAME_UI
