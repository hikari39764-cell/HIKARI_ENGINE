#include "Core/Numeric/HIKARI_IntegerConversion.h"

#include <limits>

namespace HIKARI::NUMERIC {

    uint32_t SaturateToUint32(std::size_t value) noexcept {
        constexpr std::size_t kUint32Max =
            static_cast<std::size_t>((std::numeric_limits<uint32_t>::max)());
        return static_cast<uint32_t>(value > kUint32Max ? kUint32Max : value);
    }

} // namespace HIKARI::NUMERIC
