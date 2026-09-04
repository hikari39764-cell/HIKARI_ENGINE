#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace HIKARI::TEXT {

    inline bool IsUtf8ContinuationByte(
        unsigned char value) noexcept {
        return (value & 0xC0u) == 0x80u;
    }

    // Returns the byte length of the scalar value at offset, or zero when
    // the sequence is incomplete, overlong, a surrogate, or otherwise
    // outside RFC 3629 UTF-8.
    inline size_t Utf8CodepointByteCount(
        std::string_view text,
        size_t offset) noexcept {
        if (offset >= text.size()) {
            return 0u;
        }

        const auto* bytes = reinterpret_cast<const unsigned char*>(
            text.data());
        const unsigned char leading = bytes[offset];
        if (leading <= 0x7Fu) {
            return 1u;
        }

        size_t length = 0u;
        if (leading >= 0xC2u && leading <= 0xDFu) {
            length = 2u;
        } else if (leading >= 0xE0u && leading <= 0xEFu) {
            length = 3u;
        } else if (leading >= 0xF0u && leading <= 0xF4u) {
            length = 4u;
        } else {
            return 0u;
        }

        if (length > text.size() - offset) {
            return 0u;
        }
        for (size_t index = 1u; index < length; ++index) {
            if (!IsUtf8ContinuationByte(bytes[offset + index])) {
                return 0u;
            }
        }

        const unsigned char second = bytes[offset + 1u];
        if ((leading == 0xE0u && second < 0xA0u) ||
            (leading == 0xEDu && second > 0x9Fu) ||
            (leading == 0xF0u && second < 0x90u) ||
            (leading == 0xF4u && second > 0x8Fu)) {
            return 0u;
        }
        return length;
    }

    inline bool IsValidUtf8(std::string_view text) noexcept {
        size_t offset = 0u;
        while (offset < text.size()) {
            const size_t byteCount = Utf8CodepointByteCount(text, offset);
            if (byteCount == 0u) {
                return false;
            }
            offset += byteCount;
        }
        return true;
    }

    // Returns the largest valid UTF-8 prefix no larger than maximumBytes.
    // Invalid input stops at the first malformed sequence.
    inline size_t Utf8PrefixByteCount(
        std::string_view text,
        size_t maximumBytes) noexcept {
        size_t offset = 0u;
        while (offset < text.size()) {
            const size_t byteCount = Utf8CodepointByteCount(text, offset);
            if (byteCount == 0u ||
                byteCount > maximumBytes - offset) {
                break;
            }
            offset += byteCount;
        }
        return offset;
    }

    // Invalid input fails closed by clearing the value. Valid input is
    // shortened only at a complete UTF-8 scalar-value boundary.
    inline bool TruncateUtf8(
        std::string& text,
        size_t maximumBytes) noexcept {
        if (!IsValidUtf8(text)) {
            text.clear();
            return false;
        }

        const size_t prefixBytes = Utf8PrefixByteCount(
            text,
            maximumBytes);
        if (prefixBytes < text.size()) {
            text.resize(prefixBytes);
        }
        return true;
    }

} // namespace HIKARI::TEXT
