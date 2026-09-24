#pragma once
#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

namespace HIKARI::ASSETS::SHADER::INTERNAL {
inline bool IsIdentifier(std::string_view value, bool stablePunctuation) noexcept {
    if (value.empty() || value.size() > kMaximumShaderIdentifierBytes) {
        return false;
    }
    for (size_t index = 0u; index < value.size(); ++index) {
        const char character = value[index];
        const bool alpha = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        const bool punctuation = stablePunctuation && (character == '.' || character == '-');
        if ((!alpha && !digit && character != '_' && !punctuation) || (index == 0u && digit)) {
            return false;
        }
    }
    return true;
}
} // namespace HIKARI::ASSETS::SHADER::INTERNAL
