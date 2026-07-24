#include "Editor/Style/HIKARI_EditorTheme.h"

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    const EditorThemeMetrics& GetEditorThemeMetrics() noexcept {
        static const EditorThemeMetrics metrics{};
        return metrics;
    }

    const EditorThemePalette& GetEditorThemePalette() noexcept {
        static const EditorThemePalette palette{
            .canvas = { 0.035f, 0.043f, 0.054f, 1.00f },
            .panel = { 0.050f, 0.060f, 0.073f, 1.00f },
            .raised = { 0.075f, 0.090f, 0.108f, 1.00f },
            .raisedHover = { 0.105f, 0.125f, 0.148f, 1.00f },
            .border = { 0.17f, 0.20f, 0.24f, 0.78f },
            .borderStrong = { 0.25f, 0.30f, 0.35f, 0.90f },
            .text = { 0.88f, 0.91f, 0.94f, 1.00f },
            .textMuted = { 0.48f, 0.54f, 0.61f, 1.00f },
            .accent = { 0.10f, 0.49f, 0.53f, 1.00f },
            .accentHover = { 0.13f, 0.59f, 0.62f, 1.00f },
            .accentActive = { 0.08f, 0.39f, 0.43f, 1.00f },
            .success = { 0.39f, 0.82f, 0.59f, 1.00f },
            .warning = { 0.93f, 0.67f, 0.25f, 1.00f },
            .error = { 0.94f, 0.35f, 0.32f, 1.00f }
        };
        return palette;
    }
#endif

} // namespace HIKARI::EDITOR
