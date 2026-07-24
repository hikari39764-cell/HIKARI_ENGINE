#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace HIKARI::SHADOW::INTERNAL {

    uint64_t AppendShadowHashValue(uint64_t seed, uint64_t value) {
        constexpr uint64_t kFnvPrime = 1099511628211ull;
        seed ^= value;
        seed *= kFnvPrime;
        return seed;
    }

    uint64_t AppendShadowHashBytes(
        uint64_t seed,
        const void* data,
        size_t size) {

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t byteIndex = 0; byteIndex < size; ++byteIndex) {
            seed = AppendShadowHashValue(
                seed,
                static_cast<uint64_t>(bytes[byteIndex]));
        }
        return seed;
    }

    uint64_t AppendShadowHashString(
        uint64_t seed,
        const std::string& value) {

        seed = AppendShadowHashValue(
            seed,
            static_cast<uint64_t>(value.size()));
        return value.empty()
            ? seed
            : AppendShadowHashBytes(seed, value.data(), value.size());
    }

} // namespace HIKARI::SHADOW::INTERNAL
