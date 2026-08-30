#pragma once

#include <cstdint>
#include <limits>

namespace HIKARI {

    struct RuntimeObjectHandle {
        static constexpr uint32_t kInvalidSlot =
            (std::numeric_limits<uint32_t>::max)();

        uint32_t slot = kInvalidSlot;
        uint32_t generation = 0;

        bool IsValid() const noexcept {
            return slot != kInvalidSlot && generation != 0;
        }

        uint64_t ToValue() const noexcept {
            if (!IsValid()) {
                return 0;
            }
            return
                (static_cast<uint64_t>(generation) << 32u) |
                (static_cast<uint64_t>(slot) + 1u);
        }

        static RuntimeObjectHandle FromValue(uint64_t value) noexcept {
            const uint32_t encodedSlot = static_cast<uint32_t>(value);
            const uint32_t decodedGeneration =
                static_cast<uint32_t>(value >> 32u);
            return encodedSlot != 0u && decodedGeneration != 0u
                ? RuntimeObjectHandle{
                    encodedSlot - 1u,
                    decodedGeneration }
                : RuntimeObjectHandle{};
        }

        bool operator==(const RuntimeObjectHandle& rhs) const noexcept {
            return slot == rhs.slot && generation == rhs.generation;
        }

        bool operator!=(const RuntimeObjectHandle& rhs) const noexcept {
            return !(*this == rhs);
        }
    };

} // namespace HIKARI
