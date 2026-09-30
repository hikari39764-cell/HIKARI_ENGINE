#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"

namespace HIKARI {
namespace {

constexpr auto kUiTextFieldSettingsFields = [](auto&& field) {
    field("enabled", &UiTextFieldSettings::enabled);
    field("maximumCharacters", &UiTextFieldSettings::maximumCharacters, JSONFIELDS::Range{1.0, 4096.0});
    field("submitEventId", &UiTextFieldSettings::submitEventId);
};

const char* L(const char* key) { return TEXT::LocalizeCStr(key); }

size_t ScalarCount(std::string_view value) noexcept {
    size_t count = 0u;
    for (size_t offset = 0u; offset < value.size();) {
        const size_t length = TEXT::Utf8CodepointByteCount(value, offset);
        if (!length)
            return 0u;
        offset += length;
        ++count;
    }
    return count;
}

size_t ClampBoundary(std::string_view value, size_t offset) noexcept {
    offset = (std::min)(offset, value.size());
    size_t boundary = 0u;
    while (boundary < offset) {
        const size_t length = TEXT::Utf8CodepointByteCount(value, boundary);
        if (!length || boundary + length > offset)
            break;
        boundary += length;
    }
    return boundary;
}

size_t PreviousBoundary(std::string_view value, size_t offset) noexcept {
    offset = ClampBoundary(value, offset);
    if (!offset)
        return 0u;
    do {
        --offset;
    } while (offset && TEXT::IsUtf8ContinuationByte(static_cast<unsigned char>(value[offset])));
    return offset;
}

size_t NextBoundary(std::string_view value, size_t offset) noexcept {
    offset = ClampBoundary(value, offset);
    const size_t length = TEXT::Utf8CodepointByteCount(value, offset);
    return length ? offset + length : offset;
}

} // namespace

void UiTextFieldComponent::Serialize(nlohmann::json& out) const {
    out = {{"version", 1}};
    JSONFIELDS::Write(out, settings_, kUiTextFieldSettingsFields);
}

void UiTextFieldComponent::Deserialize(const nlohmann::json& in) {
    settings_ = {};
    JSONFIELDS::ReadLenient(in, settings_, kUiTextFieldSettingsFields);
    settings_ = JSONFIELDS::Sanitize(std::move(settings_), kUiTextFieldSettingsFields);
    ResetEditingState();
}

void UiTextFieldComponent::BuildInspector(IInspectorBuilder& builder) {
    if (!builder.Section(L("uix.textField"), true))
        return;
    (void)builder.Bool({"enabled", L("uix.enabled"), L("uix.textFieldEnabledHelp")}, settings_.enabled);
    int maximumCharacters = static_cast<int>(settings_.maximumCharacters);
    if (builder.Int({"maximumCharacters", L("uix.textFieldMaximum"), L("uix.textFieldMaximumHelp")}, maximumCharacters))
        settings_.maximumCharacters = static_cast<uint32_t>(std::clamp(maximumCharacters, 1, 4096));
    (void)builder.RuntimeEventIdPicker({"submitEventId", L("uix.textFieldSubmit"), L("uix.textFieldSubmitHelp")},
                                       AuthoringEventPayloadPolicy::None, BINDING::ValueType::Bool,
                                       settings_.submitEventId);
}

size_t UiTextFieldComponent::GetSelectionStartByteOffset() const noexcept {
    return (std::min)(caretByteOffset_, selectionAnchorByteOffset_);
}

size_t UiTextFieldComponent::GetSelectionEndByteOffset() const noexcept {
    return (std::max)(caretByteOffset_, selectionAnchorByteOffset_);
}

bool UiTextFieldComponent::SetText(UiTextComponent& text, std::string_view value) noexcept {
    try {
        if (!TEXT::IsValidUtf8(value) || value.find('\0') != std::string_view::npos ||
            value.find_first_of("\r\n") != std::string_view::npos || value.size() > 16384u ||
            ScalarCount(value) > settings_.maximumCharacters || !text.SetText(value))
            return false;
        ClampEditingState(text);
        ClearComposition();
        return true;
    } catch (...) {
        return false;
    }
}

void UiTextFieldComponent::ClampEditingState(const UiTextComponent& text) noexcept {
    const std::string& value = text.GetText();
    if (!TEXT::IsValidUtf8(value)) {
        ResetEditingState();
        return;
    }
    caretByteOffset_ = ClampBoundary(value, caretByteOffset_);
    selectionAnchorByteOffset_ = ClampBoundary(value, selectionAnchorByteOffset_);
}

bool UiTextFieldComponent::SetCaret(UiTextComponent& text, size_t byteOffset, bool extendSelection) noexcept {
    try {
        const std::string& value = text.GetText();
        if (!TEXT::IsValidUtf8(value))
            return false;
        ClampEditingState(text);
        caretByteOffset_ = ClampBoundary(value, byteOffset);
        if (!extendSelection)
            selectionAnchorByteOffset_ = caretByteOffset_;
        ClearComposition();
        return true;
    } catch (...) {
        return false;
    }
}

bool UiTextFieldComponent::MoveCaret(UiTextComponent& text, int direction, bool extendSelection) noexcept {
    if (direction == 0)
        return false;
    const std::string& value = text.GetText();
    if (!TEXT::IsValidUtf8(value))
        return false;
    ClampEditingState(text);
    if (!extendSelection && HasSelection()) {
        caretByteOffset_ = direction < 0 ? GetSelectionStartByteOffset() : GetSelectionEndByteOffset();
    } else {
        caretByteOffset_ =
            direction < 0 ? PreviousBoundary(value, caretByteOffset_) : NextBoundary(value, caretByteOffset_);
    }
    if (!extendSelection)
        selectionAnchorByteOffset_ = caretByteOffset_;
    ClearComposition();
    return true;
}

bool UiTextFieldComponent::SelectAll(UiTextComponent& text) noexcept {
    if (!TEXT::IsValidUtf8(text.GetText()))
        return false;
    ClampEditingState(text);
    selectionAnchorByteOffset_ = 0u;
    caretByteOffset_ = text.GetText().size();
    ClearComposition();
    return true;
}

bool UiTextFieldComponent::InsertCommittedText(UiTextComponent& text, std::string_view value) noexcept {
    try {
        if (!TEXT::IsValidUtf8(value) || value.find('\0') != std::string_view::npos)
            return false;
        const std::string& current = text.GetText();
        if (!TEXT::IsValidUtf8(current) || current.find_first_of("\r\n") != std::string::npos)
            return false;
        ClampEditingState(text);
        const size_t start = GetSelectionStartByteOffset();
        const size_t end = GetSelectionEndByteOffset();
        const size_t outside = ScalarCount(current.substr(0u, start)) + ScalarCount(current.substr(end));
        const size_t remaining = outside >= settings_.maximumCharacters ? 0u : settings_.maximumCharacters - outside;
        std::string accepted{};
        accepted.reserve(value.size());
        size_t acceptedCount = 0u;
        for (size_t offset = 0u; offset < value.size();) {
            const size_t length = TEXT::Utf8CodepointByteCount(value, offset);
            if (!length)
                return false;
            const unsigned char first = static_cast<unsigned char>(value[offset]);
            const bool control = length == 1u && (first < 0x20u || first == 0x7Fu);
            if (!control) {
                if (acceptedCount >= remaining)
                    break;
                accepted.append(value.data() + offset, length);
                ++acceptedCount;
            }
            offset += length;
        }
        if (accepted.empty())
            return false;
        std::string replacement{};
        replacement.reserve(start + accepted.size() + current.size() - end);
        replacement.append(current.data(), start);
        replacement.append(accepted);
        replacement.append(current.data() + end, current.size() - end);
        if (replacement.size() > 16384u || !text.SetText(replacement))
            return false;
        caretByteOffset_ = start + accepted.size();
        selectionAnchorByteOffset_ = caretByteOffset_;
        ClearComposition();
        return true;
    } catch (...) {
        return false;
    }
}

bool UiTextFieldComponent::DeleteBackward(UiTextComponent& text) noexcept {
    try {
        const std::string& current = text.GetText();
        if (!TEXT::IsValidUtf8(current))
            return false;
        ClampEditingState(text);
        const size_t start =
            HasSelection() ? GetSelectionStartByteOffset() : PreviousBoundary(current, caretByteOffset_);
        const size_t end = HasSelection() ? GetSelectionEndByteOffset() : caretByteOffset_;
        if (start == end)
            return false;
        std::string replacement = current;
        replacement.erase(start, end - start);
        if (!text.SetText(replacement))
            return false;
        caretByteOffset_ = start;
        selectionAnchorByteOffset_ = start;
        ClearComposition();
        return true;
    } catch (...) {
        return false;
    }
}

bool UiTextFieldComponent::DeleteForward(UiTextComponent& text) noexcept {
    try {
        const std::string& current = text.GetText();
        if (!TEXT::IsValidUtf8(current))
            return false;
        ClampEditingState(text);
        const size_t start = HasSelection() ? GetSelectionStartByteOffset() : caretByteOffset_;
        const size_t end = HasSelection() ? GetSelectionEndByteOffset() : NextBoundary(current, caretByteOffset_);
        if (start == end)
            return false;
        std::string replacement = current;
        replacement.erase(start, end - start);
        if (!text.SetText(replacement))
            return false;
        caretByteOffset_ = start;
        selectionAnchorByteOffset_ = start;
        ClearComposition();
        return true;
    } catch (...) {
        return false;
    }
}

void UiTextFieldComponent::SetComposition(std::string_view value, bool active) noexcept {
    try {
        if (!active || value.size() > 4096u || value.find_first_of("\r\n") != std::string_view::npos ||
            !TEXT::IsValidUtf8(value)) {
            ClearComposition();
            return;
        }
        compositionText_.assign(value);
        composing_ = true;
    } catch (...) {
        ClearComposition();
    }
}

void UiTextFieldComponent::ClearComposition() noexcept {
    compositionText_.clear();
    composing_ = false;
}

void UiTextFieldComponent::ResetEditingState() noexcept {
    caretByteOffset_ = 0u;
    selectionAnchorByteOffset_ = 0u;
    ClearComposition();
}

std::string UiTextFieldComponent::DisplayText(const UiTextComponent& text) const {
    std::string result = text.GetText();
    if (composing_) {
        const size_t first = ClampBoundary(result, GetSelectionStartByteOffset());
        const size_t last = ClampBoundary(result, GetSelectionEndByteOffset());
        result.replace(first, last - first, compositionText_);
    }
    return result;
}

size_t UiTextFieldComponent::DisplayCaret() const noexcept {
    return composing_ ? GetSelectionStartByteOffset() + compositionText_.size() : caretByteOffset_;
}

size_t UiTextFieldComponent::ToCommittedOffset(size_t offset) const noexcept {
    if (!composing_ || offset <= GetSelectionStartByteOffset())
        return offset;
    const size_t end = GetSelectionStartByteOffset() + compositionText_.size();
    return offset < end ? GetSelectionStartByteOffset() : offset - end + GetSelectionEndByteOffset();
}

} // namespace HIKARI
