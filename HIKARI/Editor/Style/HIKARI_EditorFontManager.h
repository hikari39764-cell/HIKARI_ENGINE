#pragma once

#if defined(HIKARI_WITH_EDITOR)
struct ImFont;
#endif

namespace HIKARI::EDITOR {

    enum class EditorFontRole {
        Regular,
        Strong,
        Monospace,
    };

#if defined(HIKARI_WITH_EDITOR)
    bool EnsureEditorFonts(float uiScale = 1.0f);
    void ResetEditorFonts() noexcept;
    ImFont* GetEditorFont(EditorFontRole role) noexcept;
    bool PushEditorFont(EditorFontRole role);
    void PopEditorFont(bool pushed);
#endif

} // namespace HIKARI::EDITOR
