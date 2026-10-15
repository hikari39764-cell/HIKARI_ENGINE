#pragma once

#include <string>
#include <string_view>

struct ImVec2;

namespace HIKARI::EDITOR {

enum class InspectorPropertyValueKind {
    Regular,
    Compact,
};

// Keeps inspector property presentation in the editor layer. Components
// describe semantic fields through IInspectorBuilder and never need to
// know whether the editor currently uses a two-column or stacked layout.
class InspectorPropertyRow {
public:
    // description はラベルに重ねた時の説明（空なら出さない）。
    explicit InspectorPropertyRow(std::string_view label,
                                  InspectorPropertyValueKind valueKind = InspectorPropertyValueKind::Regular,
                                  std::string_view description = {});
    ~InspectorPropertyRow();

    InspectorPropertyRow(const InspectorPropertyRow&) = delete;
    InspectorPropertyRow& operator=(const InspectorPropertyRow&) = delete;

    bool IsVisible() const noexcept;

private:
    std::string label_{};
    std::string description_{};
    bool tableOpen_ = false;
    bool idPushed_ = false;
    bool visible_ = false;
};

void DrawInspectorPropertyLabel(std::string_view label, float availableWidth, std::string_view description = {});

// 組件が宣言する英文（項目名・説明・選択肢・節名、組件の表示名・分類・説明）を
// "ui.inspector.text.<原文>" で訳す。表に無ければ原文。"##" 以降の ImGui ID 部分は訳さずに残す。
std::string InspectorText(std::string_view text);

} // namespace HIKARI::EDITOR
