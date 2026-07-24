#include "HIKARI_AssetGuid.h"

#include <array>
#include <cstdint>
#include <random>

namespace HIKARI {

    AssetGuid GenerateAssetGuid() {
        static constexpr char kHex[] = "0123456789abcdef";

        std::array<uint8_t, 16> bytes{};
        std::random_device rd;
        for (uint8_t& byte : bytes) {
            byte = static_cast<uint8_t>(rd());
        }

        AssetGuid guid{};
        guid.value.resize(32);
        for (size_t i = 0; i < bytes.size(); ++i) {
            guid.value[i * 2] = kHex[(bytes[i] >> 4) & 0x0f];
            guid.value[i * 2 + 1] = kHex[bytes[i] & 0x0f];
        }
        return guid;
    }

    bool IsValidAssetGuid(std::string_view text) {
        if (text.size() != 32) {
            return false;
        }

        for (const char c : text) {
            const bool isDigit = c >= '0' && c <= '9';
            const bool isLowerHex = c >= 'a' && c <= 'f';
            const bool isUpperHex = c >= 'A' && c <= 'F';
            if (!isDigit && !isLowerHex && !isUpperHex) {
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI
