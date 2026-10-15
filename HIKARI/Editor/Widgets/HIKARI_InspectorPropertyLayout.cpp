#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"

#include <algorithm>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorTheme.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

std::string InspectorText(std::string_view text) {
    constexpr std::string_view prefix = "ui.inspector.text.";
    const size_t idStart = text.find("##");
    if (idStart == std::string_view::npos) {
        return TEXT::LocalizeSourceText(prefix, text);
    }
    return TEXT::LocalizeSourceText(prefix, text.substr(0, idStart)).append(text.substr(idStart));
}

void DrawInspectorPropertyLabel(std::string_view label, float availableWidth, std::string_view description) {
    const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const bool clipped = textSize.x > availableWidth;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    if ((clipped || !description.empty()) && ImGui::IsItemHovered()) {
        // 切れたラベルは全文を、説明があれば説明を（両方ならラベルの下に）出す。
        if (ImGui::BeginTooltip()) {
            if (clipped) {
                ImGui::TextUnformatted(label.data(), label.data() + label.size());
            }
            if (!description.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(description.data(), description.data() + description.size());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }
    }
}

InspectorPropertyRow::InspectorPropertyRow(std::string_view label, InspectorPropertyValueKind valueKind,
                                           std::string_view description)
    : label_(label), description_(description) {
    ImGui::PushID(label_.c_str());
    idPushed_ = true;

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& style = ImGui::GetStyle();
    const EditorThemeMetrics& metrics = GetEditorThemeMetrics();
    const float labelTextWidth = ImGui::CalcTextSize(label_.data(), label_.data() + label_.size()).x;
    const float preferredLabelWidth =
        (std::clamp)(labelTextWidth + style.CellPadding.x * 2.0f + 8.0f, metrics.labelColumnWidth, 220.0f);
    const float labelWidth = (std::min)(preferredLabelWidth, availableWidth * 0.48f);
    const float minimumValueWidth = valueKind == InspectorPropertyValueKind::Compact ? 42.0f : 128.0f;
    const float rowSpacing = style.ItemSpacing.x + style.CellPadding.x * 2.0f;
    const bool useStackedLayout =
        availableWidth < 280.0f || labelWidth + minimumValueWidth + rowSpacing > availableWidth;
    if (useStackedLayout) {
        DrawInspectorPropertyLabel(label_, availableWidth, description_);
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1.0f);
        visible_ = true;
        return;
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    tableOpen_ = ImGui::BeginTable("##PropertyRow", 2, flags);
    if (!tableOpen_) {
        return;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    DrawInspectorPropertyLabel(label_, ImGui::GetContentRegionAvail().x, description_);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    visible_ = true;
}

InspectorPropertyRow::~InspectorPropertyRow() {
    if (tableOpen_) {
        ImGui::EndTable();
    }
    if (idPushed_) {
        ImGui::PopID();
    }
}

bool InspectorPropertyRow::IsVisible() const noexcept { return visible_; }

} // namespace HIKARI::EDITOR
