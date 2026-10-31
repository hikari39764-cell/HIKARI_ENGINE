#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace HIKARI::TEXT {

// JSON のキーや列挙名から、翻訳が無い時の表示名を作る。"textureIlluminance" -> "Texture Illuminance"、
// "SceneRelative" -> "Scene Relative"（小文字の後ろの大文字で語を分ける。"IES" のような連続した大文字は分けない）。
inline std::string HumanizeKey(std::string_view key) {
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

} // namespace HIKARI::TEXT
