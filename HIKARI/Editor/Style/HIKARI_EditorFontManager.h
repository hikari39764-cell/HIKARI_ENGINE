#pragma once

struct ImFont;

namespace HIKARI::EDITOR {

enum class EditorFontRole {
    Regular,
    Strong,
    Monospace,
};

bool EnsureEditorFonts(float uiScale = 1.0f);
void ResetEditorFonts() noexcept;
ImFont* GetEditorFont(EditorFontRole role) noexcept;
bool PushEditorFont(EditorFontRole role);
void PopEditorFont(bool pushed);

} // namespace HIKARI::EDITOR
