#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "Core/Hashing/HIKARI_FnvHash.h"

namespace HIKARI::SHADOW::INTERNAL {

uint64_t AppendShadowHashValue(uint64_t seed, uint64_t value) { return HASH::Fnv1aCombine(seed, value); }

uint64_t AppendShadowHashBytes(uint64_t seed, const void* data, size_t size) {
    return HASH::Fnv1aBytes(seed, data, size);
}

uint64_t AppendShadowHashString(uint64_t seed, const std::string& value) {
    seed = HASH::Fnv1aCombine(seed, static_cast<uint64_t>(value.size()));
    return value.empty() ? seed : HASH::Fnv1aBytes(seed, value.data(), value.size());
}

} // namespace HIKARI::SHADOW::INTERNAL
