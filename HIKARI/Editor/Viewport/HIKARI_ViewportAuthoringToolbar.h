#pragma once

namespace HIKARI {
    struct EditorContext;
}

namespace HIKARI::EDITOR {

    struct ViewportAuthoringToolbarResult {
        bool settingsRequested = false;
    };

    ViewportAuthoringToolbarResult DrawViewportAuthoringToolbar(
        EditorContext& context,
        bool scaleDisabled);

} // namespace HIKARI::EDITOR
