#pragma once

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    struct EditorThemeMetrics {
        float spacingXs = 4.0f;
        float spacingSm = 8.0f;
        float spacingMd = 12.0f;
        float spacingLg = 16.0f;
        float rowHeight = 28.0f;
        float toolbarHeight = 32.0f;
        float controlRounding = 4.0f;
        float panelRounding = 6.0f;
        float labelColumnWidth = 152.0f;
    };

    struct EditorThemePalette {
        ImVec4 canvas{};
        ImVec4 panel{};
        ImVec4 raised{};
        ImVec4 raisedHover{};
        ImVec4 border{};
        ImVec4 borderStrong{};
        ImVec4 text{};
        ImVec4 textMuted{};
        ImVec4 accent{};
        ImVec4 accentHover{};
        ImVec4 accentActive{};
        ImVec4 success{};
        ImVec4 warning{};
        ImVec4 error{};
    };

    const EditorThemeMetrics& GetEditorThemeMetrics() noexcept;
    const EditorThemePalette& GetEditorThemePalette() noexcept;
#endif

} // namespace HIKARI::EDITOR
