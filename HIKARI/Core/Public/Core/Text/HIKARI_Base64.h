#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 標準の Base64（+ と /、= で埋める）。
namespace HIKARI::TEXT {

inline std::string EncodeBase64(std::string_view bytes) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    text.reserve((bytes.size() + 2u) / 3u * 4u);
    for (size_t index = 0u; index < bytes.size(); index += 3u) {
        const uint32_t a = static_cast<unsigned char>(bytes[index]);
        const uint32_t b = index + 1u < bytes.size() ? static_cast<unsigned char>(bytes[index + 1u]) : 0u;
        const uint32_t c = index + 2u < bytes.size() ? static_cast<unsigned char>(bytes[index + 2u]) : 0u;
        const uint32_t triple = (a << 16u) | (b << 8u) | c;
        text.push_back(kTable[(triple >> 18u) & 63u]);
        text.push_back(kTable[(triple >> 12u) & 63u]);
        text.push_back(index + 1u < bytes.size() ? kTable[(triple >> 6u) & 63u] : '=');
        text.push_back(index + 2u < bytes.size() ? kTable[triple & 63u] : '=');
    }
    return text;
}

// 空白は読み飛ばし、最初の = で終わる。表に無い文字があれば false。
inline bool DecodeBase64(std::string_view text, std::vector<uint8_t>& out) {
    const auto decode = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    out.clear();
    int value = 0;
    int bits = -8;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0)
            continue;
        if (c == '=')
            break;
        const int decoded = decode(c);
        if (decoded < 0)
            return false;
        value = (value << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

} // namespace HIKARI::TEXT
