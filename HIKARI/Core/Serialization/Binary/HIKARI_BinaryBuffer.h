#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace HIKARI::SERIALIZATION::BINARY::BUFFER {

    template<class TValue>
    void AppendTrivial(
        std::vector<uint8_t>& bytes,
        const TValue& value) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        const std::size_t oldSize = bytes.size();
        bytes.resize(oldSize + sizeof(TValue));
        std::memcpy(bytes.data() + oldSize, &value, sizeof(TValue));
    }

    template<class TValue>
    bool ReadTrivial(
        std::span<const uint8_t> bytes,
        std::size_t& cursor,
        TValue& value) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        if (cursor > bytes.size() ||
            bytes.size() - cursor < sizeof(TValue)) {
            return false;
        }
        std::memcpy(&value, bytes.data() + cursor, sizeof(TValue));
        cursor += sizeof(TValue);
        return true;
    }

    template<class TValue>
    bool OverwriteTrivial(
        std::span<uint8_t> bytes,
        std::size_t offset,
        const TValue& value) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        if (offset > bytes.size() ||
            bytes.size() - offset < sizeof(TValue)) {
            return false;
        }
        std::memcpy(bytes.data() + offset, &value, sizeof(TValue));
        return true;
    }

} // namespace HIKARI::SERIALIZATION::BINARY::BUFFER
