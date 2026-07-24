#pragma once

#include <cstddef>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    enum class EditorGlyph;

    enum class EditorButtonTone {
        Neutral,
        Primary,
        Quiet,
        Danger,
    };

    enum class EditorStatusTone {
        Normal,
        Ready,
        Warning,
        Error,
    };

#if defined(HIKARI_WITH_EDITOR)
    bool ActionButton(
        const char* label,
        const char* id,
        EditorButtonTone tone = EditorButtonTone::Neutral,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool ToggleButton(
        const char* label,
        const char* id,
        bool selected,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool ModeButton(
        const char* label,
        const char* id,
        bool selected,
        float height = 28.0f);

    bool IconButton(
        EditorGlyph glyph,
        const char* id,
        EditorButtonTone tone = EditorButtonTone::Quiet,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool IconToggleButton(
        EditorGlyph glyph,
        const char* id,
        bool selected,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    bool IconTextButton(
        EditorGlyph glyph,
        const char* label,
        const char* id,
        EditorButtonTone tone = EditorButtonTone::Neutral,
        const ImVec2& size = ImVec2(0.0f, 0.0f),
        const char* tooltip = nullptr);

    void ToolbarLabel(const char* label);
    void ToolbarDivider(float height = 20.0f);
    void StatusText(const char* label, EditorStatusTone tone);
    void StatusBadge(const char* label, EditorStatusTone tone);
    void PanelTitle(const char* title, const char* subtitle = nullptr);
    void EmptyState(const char* title, const char* detail = nullptr);

    bool SearchField(
        const char* id,
        const char* hint,
        char* buffer,
        std::size_t bufferSize,
        float width = -1.0f);

    bool BeginPropertyTable(const char* id, float labelWidth = 152.0f);
    bool BeginMetricTable(const char* id, float labelWidth = 210.0f);
    void PropertyLabel(const char* label);
    void EndPropertyTable();
#endif

} // namespace HIKARI::EDITOR
