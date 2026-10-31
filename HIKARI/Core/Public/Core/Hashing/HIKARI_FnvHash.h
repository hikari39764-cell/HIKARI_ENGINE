#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace HIKARI::HASH {

// Standard FNV-1a 64-bit constants. The offset basis is the authoritative
// value; several legacy copies used a truncated 1469598103934665603.
inline constexpr uint64_t kFnv1a64OffsetBasis = 14695981039346656037ull;
inline constexpr uint64_t kFnv1a64Prime = 1099511628211ull;

inline constexpr uint64_t Fnv1aBytes(uint64_t seed, const char* data, size_t size) noexcept {
    uint64_t hash = seed;
    for (size_t index = 0u; index < size; ++index) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= kFnv1a64Prime;
    }
    return hash;
}

inline uint64_t Fnv1aBytes(uint64_t seed, const void* data, size_t size) noexcept {
    return Fnv1aBytes(seed, static_cast<const char*>(data), size);
}

inline uint64_t Fnv1aString(std::string_view value) noexcept {
    return Fnv1aBytes(kFnv1a64OffsetBasis, value.data(), value.size());
}

inline uint64_t Fnv1aCombine(uint64_t seed, uint64_t value) noexcept { return Fnv1aBytes(seed, &value, sizeof(value)); }

} // namespace HIKARI::HASH
