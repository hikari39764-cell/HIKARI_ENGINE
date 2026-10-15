
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"

#include <cctype>

#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR {

std::string HumanizeFieldKey(std::string_view key) {
    std::string label;
    label.reserve(key.size() + 4u);
    for (size_t index = 0; index < key.size(); ++index) {
        const char c = key[index];
        if (index == 0u) {
            label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else if (std::isupper(static_cast<unsigned char>(c)) &&
                   !std::isupper(static_cast<unsigned char>(key[index - 1u]))) {
            label.push_back(' ');
            label.push_back(c);
        } else {
            label.push_back(c);
        }
    }
    return label;
}

std::string ResolveFieldLabel(std::string_view labelPrefix, std::string_view key) {
    std::string localizationKey;
    localizationKey.reserve(labelPrefix.size() + key.size() + 1u);
    localizationKey.append(labelPrefix).append(".").append(key);
    const std::string& localized = TEXT::Localize(localizationKey);
    return localized == localizationKey ? HumanizeFieldKey(key) : localized;
}

bool IsColorFieldKey(std::string_view key) noexcept {
    const auto contains = [key](std::string_view part) { return key.find(part) != std::string_view::npos; };
    return contains("olor") || contains("lbedo") || contains("tint") || contains("Tint");
}

} // namespace HIKARI::EDITOR
