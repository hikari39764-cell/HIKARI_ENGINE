#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"

#include <algorithm>

#include "Editor/Style/HIKARI_EditorTheme.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void DrawInspectorPropertyLabel(
        std::string_view label,
        float availableWidth) {
#if defined(HIKARI_WITH_EDITOR)
        const ImVec2 textSize = ImGui::CalcTextSize(
            label.data(),
            label.data() + label.size());
        const bool clipped = textSize.x > availableWidth;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(
            label.data(),
            label.data() + label.size());
        if (clipped && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%.*s",
                static_cast<int>(label.size()),
                label.data());
        }
#else
        (void)label;
        (void)availableWidth;
#endif
    }

    InspectorPropertyRow::InspectorPropertyRow(
        std::string_view label,
        InspectorPropertyValueKind valueKind)
        : label_(label) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::PushID(label_.c_str());
        idPushed_ = true;

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const ImGuiStyle& style = ImGui::GetStyle();
        const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
        const float labelTextWidth = ImGui::CalcTextSize(
            label_.data(),
            label_.data() + label_.size()).x;
        const float preferredLabelWidth = (std::clamp)(
            labelTextWidth + style.CellPadding.x * 2.0f + 8.0f,
            metrics.labelColumnWidth,
            220.0f);
        const float labelWidth = (std::min)(
            preferredLabelWidth,
            availableWidth * 0.48f);
        const float minimumValueWidth =
            valueKind == InspectorPropertyValueKind::Compact
                ? 42.0f
                : 128.0f;
        const float rowSpacing = style.ItemSpacing.x +
            style.CellPadding.x * 2.0f;
        const bool useStackedLayout =
            availableWidth < 280.0f ||
            labelWidth + minimumValueWidth + rowSpacing > availableWidth;
        if (useStackedLayout) {
            DrawInspectorPropertyLabel(label_, availableWidth);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1.0f);
            visible_ = true;
            return;
        }

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoSavedSettings;
        tableOpen_ = ImGui::BeginTable("##PropertyRow", 2, flags);
        if (!tableOpen_) {
            return;
        }
        ImGui::TableSetupColumn(
            "Label",
            ImGuiTableColumnFlags_WidthFixed,
            labelWidth);
        ImGui::TableSetupColumn(
            "Value",
            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawInspectorPropertyLabel(
            label_,
            ImGui::GetContentRegionAvail().x);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        visible_ = true;
#endif
    }

    InspectorPropertyRow::~InspectorPropertyRow() {
#if defined(HIKARI_WITH_EDITOR)
        if (tableOpen_) {
            ImGui::EndTable();
        }
        if (idPushed_) {
            ImGui::PopID();
        }
#endif
    }

    bool InspectorPropertyRow::IsVisible() const noexcept {
        return visible_;
    }

} // namespace HIKARI::EDITOR
