
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"

#include "Core/Text/HIKARI_HumanizeKey.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR {

std::string ResolveFieldLabel(std::string_view labelPrefix, std::string_view key) {
    std::string localizationKey;
    localizationKey.reserve(labelPrefix.size() + key.size() + 1u);
    localizationKey.append(labelPrefix).append(".").append(key);
    const std::string& localized = TEXT::Localize(localizationKey);
    return localized == localizationKey ? TEXT::HumanizeKey(key) : localized;
}

bool IsColorFieldKey(std::string_view key) noexcept {
    const auto contains = [key](std::string_view part) { return key.find(part) != std::string_view::npos; };
    return contains("olor") || contains("lbedo") || contains("tint") || contains("Tint");
}

} // namespace HIKARI::EDITOR
