#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::TEXT {

    char ToLowerAscii(char value) noexcept {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<char>(value + ('a' - 'A'));
        }
        return value;
    }

    void ToLowerAsciiInPlace(std::string& value) noexcept {
        for (char& character : value) {
            character = ToLowerAscii(character);
        }
    }

    std::string ToLowerAsciiCopy(std::string_view value) {
        std::string result(value);
        ToLowerAsciiInPlace(result);
        return result;
    }

} // namespace HIKARI::TEXT
