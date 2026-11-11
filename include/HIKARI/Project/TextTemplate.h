#pragma once

#include <span>
#include <string>
#include <string_view>

namespace HIKARI {

// Fills {0} ... {9} in a text template with values. A placeholder without a value stays as it is.
// Used for interface sentences with names or numbers: the template is the English source text that the text tables
// translate (see FormatText in EditorWorkspace.h); logs and agents use the English template filled the same way.
inline std::string FillText(std::string_view pattern, std::span<const std::string_view> values) {
    std::string text{};
    text.reserve(pattern.size());
    for (size_t index = 0u; index < pattern.size(); ++index) {
        const bool placeholder = pattern[index] == '{' && index + 2u < pattern.size() && pattern[index + 2u] == '}' &&
                                 pattern[index + 1u] >= '0' && pattern[index + 1u] <= '9' &&
                                 static_cast<size_t>(pattern[index + 1u] - '0') < values.size();
        if (placeholder) {
            text.append(values[static_cast<size_t>(pattern[index + 1u] - '0')]);
            index += 2u;
        } else {
            text.push_back(pattern[index]);
        }
    }
    return text;
}

} // namespace HIKARI
