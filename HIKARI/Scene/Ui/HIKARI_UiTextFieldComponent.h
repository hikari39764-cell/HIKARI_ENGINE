#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"
#include "Render2D/HIKARI_GameUiRenderData.h"

namespace HIKARI {

class UiTextComponent;

struct UiTextFieldSettings {
    bool enabled = true;
    uint32_t maximumCharacters = 256u;
    std::string submitEventId{};
};

class UiTextFieldComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiTextFieldComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept { return settings_.enabled; }
    const std::string& GetSubmitEventId() const noexcept { return settings_.submitEventId; }

    size_t GetCaretByteOffset() const noexcept { return caretByteOffset_; }
    size_t GetSelectionStartByteOffset() const noexcept;
    size_t GetSelectionEndByteOffset() const noexcept;
    bool HasSelection() const noexcept { return caretByteOffset_ != selectionAnchorByteOffset_; }
    const std::string& GetCompositionText() const noexcept { return compositionText_; }
    bool IsComposing() const noexcept { return composing_; }

    bool SetText(UiTextComponent& text, std::string_view value) noexcept;
    void ClampEditingState(const UiTextComponent& text) noexcept;
    bool SetCaret(UiTextComponent& text, size_t byteOffset, bool extendSelection) noexcept;
    bool MoveCaret(UiTextComponent& text, int direction, bool extendSelection) noexcept;
    bool SelectAll(UiTextComponent& text) noexcept;
    bool InsertCommittedText(UiTextComponent& text, std::string_view value) noexcept;
    bool DeleteBackward(UiTextComponent& text) noexcept;
    bool DeleteForward(UiTextComponent& text) noexcept;
    void SetComposition(std::string_view value, bool active) noexcept;
    void ClearComposition() noexcept;
    void ResetEditingState() noexcept;
    std::string DisplayText(const UiTextComponent& text) const;
    size_t DisplayCaret() const noexcept;
    size_t ToCommittedOffset(size_t displayOffset) const noexcept;
    const std::shared_ptr<GAME_UI::GameUiTextLayoutFeedback>& LayoutFeedback() const noexcept { return feedback_; }

private:
    UiTextFieldSettings settings_{};
    size_t caretByteOffset_ = 0u;
    size_t selectionAnchorByteOffset_ = 0u;
    std::string compositionText_{};
    bool composing_ = false;
    std::shared_ptr<GAME_UI::GameUiTextLayoutFeedback> feedback_ =
        std::make_shared<GAME_UI::GameUiTextLayoutFeedback>();
};

} // namespace HIKARI
