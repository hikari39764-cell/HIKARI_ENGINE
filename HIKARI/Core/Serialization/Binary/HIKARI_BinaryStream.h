#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <vector>

namespace HIKARI::SERIALIZATION::BINARY::STREAM {

    template<class TValue>
    bool WriteTrivial(
        std::ostream& stream,
        const TValue& value) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        stream.write(
            reinterpret_cast<const char*>(&value),
            static_cast<std::streamsize>(sizeof(TValue)));
        return stream.good();
    }

    template<class TValue>
    bool ReadTrivial(
        std::istream& stream,
        TValue& value) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        stream.read(
            reinterpret_cast<char*>(&value),
            static_cast<std::streamsize>(sizeof(TValue)));
        return stream.good();
    }

    template<class TValue>
    bool WriteTrivialArray(
        std::ostream& stream,
        const TValue* values,
        std::size_t count) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        if (count == 0u) {
            return stream.good();
        }
        if (values == nullptr ||
            count >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::streamsize>::max)()) /
                sizeof(TValue)) {
            return false;
        }
        stream.write(
            reinterpret_cast<const char*>(values),
            static_cast<std::streamsize>(count * sizeof(TValue)));
        return stream.good();
    }

    template<class TValue>
    bool ReadTrivialArray(
        std::istream& stream,
        TValue* values,
        std::size_t count) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        if (count == 0u) {
            return stream.good();
        }
        if (values == nullptr ||
            count >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::streamsize>::max)()) /
                sizeof(TValue)) {
            return false;
        }
        stream.read(
            reinterpret_cast<char*>(values),
            static_cast<std::streamsize>(count * sizeof(TValue)));
        return stream.good();
    }

    inline bool WriteBoolean8(
        std::ostream& stream,
        bool value) {

        const uint8_t stored = value ? 1u : 0u;
        return WriteTrivial(stream, stored);
    }

    inline bool ReadBoolean8(
        std::istream& stream,
        bool& value) {

        uint8_t stored = 0u;
        if (!ReadTrivial(stream, stored)) {
            return false;
        }
        value = stored != 0u;
        return true;
    }

    template<uint32_t MaximumBytes>
    bool WriteLengthPrefixedString32(
        std::ostream& stream,
        const std::string& value) {

        if (value.size() > MaximumBytes ||
            value.size() > (std::numeric_limits<uint32_t>::max)()) {
            return false;
        }
        const uint32_t size = static_cast<uint32_t>(value.size());
        return WriteTrivial(stream, size) &&
            WriteTrivialArray(stream, value.data(), value.size());
    }

    template<uint32_t MaximumBytes>
    bool ReadLengthPrefixedString32(
        std::istream& stream,
        std::string& value) {

        uint32_t size = 0u;
        if (!ReadTrivial(stream, size) || size > MaximumBytes) {
            return false;
        }
        value.resize(size);
        return ReadTrivialArray(stream, value.data(), value.size());
    }

    template<uint32_t MaximumCount, class TValue>
    bool WriteLengthPrefixedTrivialVector32(
        std::ostream& stream,
        const std::vector<TValue>& values) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        if (values.size() > MaximumCount ||
            values.size() > (std::numeric_limits<uint32_t>::max)()) {
            return false;
        }
        const uint32_t count = static_cast<uint32_t>(values.size());
        return WriteTrivial(stream, count) &&
            WriteTrivialArray(stream, values.data(), values.size());
    }

    template<uint32_t MaximumCount, class TValue>
    bool ReadLengthPrefixedTrivialVector32(
        std::istream& stream,
        std::vector<TValue>& values) {

        static_assert(std::is_trivially_copyable_v<TValue>);
        uint32_t count = 0u;
        if (!ReadTrivial(stream, count) || count > MaximumCount) {
            return false;
        }
        values.resize(count);
        return ReadTrivialArray(stream, values.data(), values.size());
    }

} // namespace HIKARI::SERIALIZATION::BINARY::STREAM
