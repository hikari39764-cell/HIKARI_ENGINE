#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace HIKARI::ASSETS::MODELS::OBJ {

    inline std::string TrimText(std::string_view value) {
        size_t first = 0;
        while (first < value.size() &&
            std::isspace(static_cast<unsigned char>(value[first])) != 0) {
            ++first;
        }

        size_t last = value.size();
        while (last > first &&
            std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
            --last;
        }
        return std::string(value.substr(first, last - first));
    }

} // namespace HIKARI::ASSETS::MODELS::OBJ
