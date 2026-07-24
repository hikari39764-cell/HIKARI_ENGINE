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
        explicit InspectorPropertyRow(
            std::string_view label,
            InspectorPropertyValueKind valueKind =
                InspectorPropertyValueKind::Regular);
        ~InspectorPropertyRow();

        InspectorPropertyRow(const InspectorPropertyRow&) = delete;
        InspectorPropertyRow& operator=(const InspectorPropertyRow&) = delete;

        bool IsVisible() const noexcept;

    private:
        std::string label_{};
        bool tableOpen_ = false;
        bool idPushed_ = false;
        bool visible_ = false;
    };

    void DrawInspectorPropertyLabel(
        std::string_view label,
        float availableWidth);

} // namespace HIKARI::EDITOR
