#include "HIKARI_EditorStyle.h"

#include "Editor/Style/HIKARI_EditorTheme.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void ApplyEditorStyle() {
#if defined(HIKARI_WITH_EDITOR)
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const EditorThemePalette& palette = GetEditorThemePalette();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(metrics.spacingMd, 10.0f);
        style.FramePadding = ImVec2(metrics.spacingSm, 5.0f);
        style.CellPadding = ImVec2(metrics.spacingSm, 5.0f);
        style.ItemSpacing = ImVec2(metrics.spacingSm, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, metrics.spacingXs);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 13.0f;
        style.GrabMinSize = 10.0f;
        style.DisabledAlpha = 0.48f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;

        style.WindowRounding = metrics.panelRounding;
        style.ChildRounding = metrics.panelRounding;
        style.FrameRounding = metrics.controlRounding;
        style.PopupRounding = metrics.panelRounding;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = metrics.controlRounding;
        style.TabRounding = metrics.controlRounding;

        style.DockingSeparatorSize = 2.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = palette.text;
        colors[ImGuiCol_TextDisabled] = palette.textMuted;
        colors[ImGuiCol_WindowBg] = palette.panel;
        colors[ImGuiCol_ChildBg] = palette.canvas;
        colors[ImGuiCol_PopupBg] = ImVec4(
            palette.panel.x,
            palette.panel.y,
            palette.panel.z,
            0.99f);
        colors[ImGuiCol_Border] = palette.border;
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = palette.raised;
        colors[ImGuiCol_FrameBgHovered] = palette.raisedHover;
        colors[ImGuiCol_FrameBgActive] = ImVec4(
            palette.accentActive.x,
            palette.accentActive.y,
            palette.accentActive.z,
            0.72f);
        colors[ImGuiCol_TitleBg] = palette.canvas;
        colors[ImGuiCol_TitleBgActive] = palette.panel;
        colors[ImGuiCol_TitleBgCollapsed] = palette.canvas;
        colors[ImGuiCol_MenuBarBg] = palette.canvas;
        colors[ImGuiCol_ScrollbarBg] = ImVec4(
            palette.canvas.x,
            palette.canvas.y,
            palette.canvas.z,
            0.80f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.40f, 0.46f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.49f, 0.56f, 1.00f);
        colors[ImGuiCol_CheckMark] = palette.accent;
        colors[ImGuiCol_SliderGrab] = palette.accent;
        colors[ImGuiCol_SliderGrabActive] = palette.accentHover;
        colors[ImGuiCol_Button] = palette.raised;
        colors[ImGuiCol_ButtonHovered] = palette.raisedHover;
        colors[ImGuiCol_ButtonActive] = palette.accentActive;
        colors[ImGuiCol_Header] = ImVec4(
            palette.raised.x,
            palette.raised.y,
            palette.raised.z,
            0.76f);
        colors[ImGuiCol_HeaderHovered] = palette.raisedHover;
        colors[ImGuiCol_HeaderActive] = ImVec4(
            palette.accentActive.x,
            palette.accentActive.y,
            palette.accentActive.z,
            0.82f);
        colors[ImGuiCol_Separator] = palette.border;
        colors[ImGuiCol_SeparatorHovered] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.78f);
        colors[ImGuiCol_SeparatorActive] = palette.accent;
        colors[ImGuiCol_ResizeGrip] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.18f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.45f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.78f);
        colors[ImGuiCol_Tab] = palette.canvas;
        colors[ImGuiCol_TabHovered] = palette.raisedHover;
        colors[ImGuiCol_TabActive] = palette.raised;
        colors[ImGuiCol_TabUnfocused] = palette.canvas;
        colors[ImGuiCol_TabUnfocusedActive] = palette.panel;
        colors[ImGuiCol_DockingPreview] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.55f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.02f, 0.03f, 0.04f, 0.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.55f, 0.65f, 0.75f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = palette.warning;
        colors[ImGuiCol_TableHeaderBg] = palette.raised;
        colors[ImGuiCol_TableBorderStrong] = palette.borderStrong;
        colors[ImGuiCol_TableBorderLight] = palette.border;
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.38f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(
            palette.accent.x,
            palette.accent.y,
            palette.accent.z,
            0.78f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.04f, 0.62f);
#endif
    }

} // namespace HIKARI::EDITOR
