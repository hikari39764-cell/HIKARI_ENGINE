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

        bool operator==(const RuntimeObjectHandle& rhs) const noexcept {
            return slot == rhs.slot && generation == rhs.generation;
        }

        bool operator!=(const RuntimeObjectHandle& rhs) const noexcept {
            return !(*this == rhs);
        }
    };

} // namespace HIKARI
