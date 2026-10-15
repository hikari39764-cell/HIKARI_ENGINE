#pragma once

#include <cstddef>

#include "imgui.h"

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

bool ActionButton(const char* label, const char* id, EditorButtonTone tone = EditorButtonTone::Neutral,
                  const ImVec2& size = ImVec2(0.0f, 0.0f), const char* tooltip = nullptr);

bool ToggleButton(const char* label, const char* id, bool selected, const ImVec2& size = ImVec2(0.0f, 0.0f),
                  const char* tooltip = nullptr);

bool ModeButton(const char* label, const char* id, bool selected, float height = 28.0f);

bool IconButton(EditorGlyph glyph, const char* id, EditorButtonTone tone = EditorButtonTone::Quiet,
                const ImVec2& size = ImVec2(0.0f, 0.0f), const char* tooltip = nullptr);

bool IconToggleButton(EditorGlyph glyph, const char* id, bool selected, const ImVec2& size = ImVec2(0.0f, 0.0f),
                      const char* tooltip = nullptr);

bool IconTextButton(EditorGlyph glyph, const char* label, const char* id,
                    EditorButtonTone tone = EditorButtonTone::Neutral, const ImVec2& size = ImVec2(0.0f, 0.0f),
                    const char* tooltip = nullptr);

void ToolbarLabel(const char* label);
// 次の操作が収まる場合だけ同じ行へ置く。
void NextToolbarItem(float width = 28.0f);
void ToolbarDivider(float height = 20.0f);
void StatusText(const char* label, EditorStatusTone tone);
void StatusBadge(const char* label, EditorStatusTone tone);
void PanelTitle(const char* title, const char* subtitle = nullptr);
// 空の一覧の案内。actionLabel があれば、その場で始められる操作のボタンを下に置き、押されたら true。
bool EmptyState(const char* title, const char* detail = nullptr, const char* actionLabel = nullptr);
// 面板の「常用 + 高級」。常用の項目の後に置く折り畳み（既定で閉じる）。開いていれば中身を描いて EndAdvancedSection。
bool BeginAdvancedSection(const char* id);
void EndAdvancedSection();

bool SearchField(const char* id, const char* hint, char* buffer, std::size_t bufferSize, float width = -1.0f);

bool BeginPropertyTable(const char* id, float labelWidth = 152.0f);
bool BeginMetricTable(const char* id, float labelWidth = 210.0f);
void PropertyLabel(const char* label);
void EndPropertyTable();

} // namespace HIKARI::EDITOR
