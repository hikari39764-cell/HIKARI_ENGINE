#pragma once

#include <cstdint>
#include <string>

// Window-system text input (IME) data. Platform owns it so that the window needs no Input header;
// Input re-exports it through HIKARI_InputTypes.h.
namespace HIKARI::INPUT {

enum class TextInputEventKind : uint8_t {
    CompositionStarted,
    CompositionUpdated,
    Committed,
    CompositionEnded,
};

struct TextInputEvent {
    TextInputEventKind kind = TextInputEventKind::Committed;
    // UTF-8 committed text or the current composition preview.
    std::string utf8{};
};

struct ImeCaretAnchor {
    // Coordinates are client pixels in the active game window.
    int32_t clientX = 0;
    int32_t clientY = 0;
    bool valid = false;
};

} // namespace HIKARI::INPUT
