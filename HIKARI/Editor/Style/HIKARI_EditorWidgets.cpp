#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)
#include <algorithm>

#include "Editor/Style/HIKARI_EditorFontManager.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"

namespace HIKARI::EDITOR {

    namespace {
        struct ButtonPalette {
            ImVec4 normal;
            ImVec4 hovered;
            ImVec4 active;
            ImVec4 text;
        };

        ButtonPalette ResolvePalette(EditorButtonTone tone) {
            const EditorThemePalette& theme = GetEditorThemePalette();
            switch (tone) {
            case EditorButtonTone::Primary:
                return {
                    theme.accent,
                    theme.accentHover,
                    theme.accentActive,
                    { 0.92f, 0.98f, 0.98f, 1.00f }
                };
            case EditorButtonTone::Quiet:
                return {
                    { theme.raised.x, theme.raised.y, theme.raised.z, 0.38f },
                    theme.raisedHover,
                    { theme.accentActive.x, theme.accentActive.y, theme.accentActive.z, 0.72f },
                    { 0.76f, 0.80f, 0.83f, 1.00f }
                };
            case EditorButtonTone::Danger:
                return {
                    theme.raised,
                    { theme.error.x * 0.70f, theme.error.y * 0.70f, theme.error.z * 0.70f, 1.00f },
                    { theme.error.x * 0.55f, theme.error.y * 0.55f, theme.error.z * 0.55f, 1.00f },
                    { 1.00f, 0.93f, 0.92f, 1.00f }
                };
            case EditorButtonTone::Neutral:
            default:
                return {
                    theme.raised,
                    theme.raisedHover,
                    theme.accentActive,
                    theme.text
                };
            }
        }

        ImVec4 ResolveStatusColor(EditorStatusTone tone) {
            const EditorThemePalette& theme = GetEditorThemePalette();
            switch (tone) {
            case EditorStatusTone::Ready: return theme.success;
            case EditorStatusTone::Warning: return theme.warning;
            case EditorStatusTone::Error: return theme.error;
            case EditorStatusTone::Normal:
            default: return theme.textMuted;
            }
        }

        ImVec4 ResolveButtonBackground(
            const ButtonPalette& palette,
            bool hovered,
            bool active) {
            if (active) {
                return palette.active;
            }
            if (hovered) {
                return palette.hovered;
            }
            return palette.normal;
        }

        void DrawButtonSurface(
            const ImVec2& min,
            const ImVec2& max,
            const ImVec4& background,
            float rounding) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                min,
                max,
                ImGui::GetColorU32(background),
                rounding);
        }
    }

    bool ActionButton(
        const char* label,
        const char* id,
        EditorButtonTone tone,
        const ImVec2& size,
        const char* tooltip) {
        const ButtonPalette palette = ResolvePalette(tone);
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        ImGui::PushID(id);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            metrics.controlRounding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(metrics.spacingSm, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, palette.normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.active);
        ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
        const bool pressed = ImGui::Button(label, size);
        if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        return pressed;
    }

    bool ToggleButton(
        const char* label,
        const char* id,
        bool selected,
        const ImVec2& size,
        const char* tooltip) {
        return ActionButton(
            label,
            id,
            selected ? EditorButtonTone::Primary : EditorButtonTone::Quiet,
            size,
            tooltip);
    }

    bool ModeButton(
        const char* label,
        const char* id,
        bool selected,
        float height) {
        return ToggleButton(
            label,
            id,
            selected,
            ImVec2(-1.0f, height));
    }

    bool IconButton(
        EditorGlyph glyph,
        const char* id,
        EditorButtonTone tone,
        const ImVec2& size,
        const char* tooltip) {
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const ButtonPalette palette = ResolvePalette(tone);
        const ImVec2 resolvedSize{
            size.x > 0.0f ? size.x : metrics.rowHeight,
            size.y > 0.0f ? size.y : metrics.rowHeight
        };
        ImGui::PushID(id);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(
            "##IconButton",
            resolvedSize);
        const bool hovered = ImGui::IsItemHovered(
            ImGuiHoveredFlags_AllowWhenDisabled);
        const bool active = ImGui::IsItemActive();
        const ImVec2 max{ min.x + resolvedSize.x, min.y + resolvedSize.y };
        DrawButtonSurface(
            min,
            max,
            ResolveButtonBackground(palette, hovered, active),
            metrics.controlRounding);
        const float glyphSize = (std::max)(
            8.0f,
            (std::min)(resolvedSize.x, resolvedSize.y) - 10.0f);
        DrawEditorGlyph(
            *ImGui::GetWindowDrawList(),
            glyph,
            ImVec2(
                min.x + resolvedSize.x * 0.5f,
                min.y + resolvedSize.y * 0.5f),
            glyphSize,
            ImGui::GetColorU32(palette.text));
        if (tooltip != nullptr && tooltip[0] != '\0' && hovered) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopID();
        return pressed;
    }

    bool IconToggleButton(
        EditorGlyph glyph,
        const char* id,
        bool selected,
        const ImVec2& size,
        const char* tooltip) {
        return IconButton(
            glyph,
            id,
            selected
                ? EditorButtonTone::Primary
                : EditorButtonTone::Quiet,
            size,
            tooltip);
    }

    bool IconTextButton(
        EditorGlyph glyph,
        const char* label,
        const char* id,
        EditorButtonTone tone,
        const ImVec2& size,
        const char* tooltip) {
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const ButtonPalette palette = ResolvePalette(tone);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float height = size.y > 0.0f
            ? size.y
            : metrics.rowHeight;
        const float glyphSize = (std::max)(8.0f, height - 12.0f);
        float width = size.x;
        if (width == 0.0f) {
            width = metrics.spacingSm + glyphSize + metrics.spacingSm +
                textSize.x + metrics.spacingMd;
        } else if (width < 0.0f) {
            width = ImGui::GetContentRegionAvail().x;
        }

        ImGui::PushID(id);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 resolvedSize{ width, height };
        const bool pressed = ImGui::InvisibleButton(
            "##IconTextButton",
            resolvedSize);
        const bool hovered = ImGui::IsItemHovered(
            ImGuiHoveredFlags_AllowWhenDisabled);
        const bool active = ImGui::IsItemActive();
        const ImVec2 max{ min.x + width, min.y + height };
        DrawButtonSurface(
            min,
            max,
            ResolveButtonBackground(palette, hovered, active),
            metrics.controlRounding);

        const ImVec2 glyphCenter{
            min.x + metrics.spacingSm + glyphSize * 0.5f,
            min.y + height * 0.5f
        };
        DrawEditorGlyph(
            *ImGui::GetWindowDrawList(),
            glyph,
            glyphCenter,
            glyphSize,
            ImGui::GetColorU32(palette.text));

        const ImVec2 textPosition{
            glyphCenter.x + glyphSize * 0.5f + metrics.spacingSm,
            min.y + (height - textSize.y) * 0.5f
        };
        const ImVec4 clipRect{
            textPosition.x,
            min.y,
            max.x - metrics.spacingSm,
            max.y
        };
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            textPosition,
            ImGui::GetColorU32(palette.text),
            label,
            nullptr,
            0.0f,
            &clipRect);
        if (tooltip != nullptr && tooltip[0] != '\0' && hovered) {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopID();
        return pressed;
    }

    void ToolbarLabel(const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
    }

    void ToolbarDivider(float height) {
        const EditorThemePalette& theme = GetEditorThemePalette();
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        const float resolvedHeight = (std::max)(8.0f, height);
        ImGui::Dummy(ImVec2(1.0f, resolvedHeight));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(minimum.x, minimum.y + 3.0f),
            ImVec2(minimum.x, minimum.y + resolvedHeight - 3.0f),
            ImGui::GetColorU32(theme.border),
            1.0f);
    }

    void StatusText(const char* label, EditorStatusTone tone) {
        ImGui::TextColored(ResolveStatusColor(tone), "%s", label);
    }

    void StatusBadge(const char* label, EditorStatusTone tone) {
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const ImVec4 color = ResolveStatusColor(tone);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 size{
            textSize.x + metrics.spacingSm * 2.0f,
            textSize.y + metrics.spacingXs * 2.0f
        };
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max{ min.x + size.x, min.y + size.y };
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            min,
            max,
            ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.14f)),
            metrics.controlRounding);
        drawList->AddRect(
            min,
            max,
            ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.42f)),
            metrics.controlRounding);
        drawList->AddText(
            ImVec2(min.x + metrics.spacingSm, min.y + metrics.spacingXs),
            ImGui::GetColorU32(color),
            label);
        ImGui::Dummy(size);
    }

    void PanelTitle(const char* title, const char* subtitle) {
        const bool pushed = PushEditorFont(EditorFontRole::Strong);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
        PopEditorFont(pushed);
        if (subtitle != nullptr && subtitle[0] != '\0') {
            ImGui::TextDisabled("%s", subtitle);
        }
    }

    void EmptyState(const char* title, const char* detail) {
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float titleWidth = ImGui::CalcTextSize(title).x;
        ImGui::Dummy(ImVec2(0.0f, metrics.spacingLg));
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            (std::max)(0.0f, (availableWidth - titleWidth) * 0.5f));
        const bool pushed = PushEditorFont(EditorFontRole::Strong);
        ImGui::TextUnformatted(title);
        PopEditorFont(pushed);
        if (detail == nullptr || detail[0] == '\0') {
            return;
        }
        const float detailWidth = ImGui::CalcTextSize(detail).x;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            (std::max)(0.0f, (availableWidth - detailWidth) * 0.5f));
        ImGui::TextDisabled("%s", detail);
    }

    bool SearchField(
        const char* id,
        const char* hint,
        char* buffer,
        std::size_t bufferSize,
        float width) {
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        ImGui::PushID(id);
        const bool hasText = buffer != nullptr && buffer[0] != '\0';
        const float availableWidth = width > 0.0f
            ? width
            : ImGui::GetContentRegionAvail().x;
        const float clearWidth = hasText
            ? ImGui::GetFrameHeight() + metrics.spacingSm
            : 0.0f;
        ImGui::SetNextItemWidth((std::max)(80.0f, availableWidth - clearWidth));
        bool changed = ImGui::InputTextWithHint(
            "##Search",
            hint,
            buffer,
            bufferSize);
        if (hasText) {
            ImGui::SameLine(0.0f, metrics.spacingSm);
            if (IconButton(
                    EditorGlyph::Close,
                    "Clear",
                    EditorButtonTone::Quiet,
                    ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()),
                    "Clear search")) {
                buffer[0] = '\0';
                changed = true;
            }
        }
        ImGui::PopID();
        return changed;
    }

    bool BeginPropertyTable(const char* id, float labelWidth) {
        if (!ImGui::BeginTable(
                id,
                2,
                ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_PadOuterX)) {
            return false;
        }
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    bool BeginMetricTable(const char* id, float labelWidth) {
        if (!ImGui::BeginTable(
                id,
                2,
                ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp)) {
            return false;
        }
        ImGui::TableSetupColumn(
            "Signal",
            ImGuiTableColumnFlags_WidthFixed,
            labelWidth);
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        return true;
    }

    void PropertyLabel(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
    }

    void EndPropertyTable() {
        ImGui::EndTable();
    }

} // namespace HIKARI::EDITOR

#endif
