#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace HIKARI::TEXT {

    inline std::string ToHexString(uint64_t value) {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0')
               << std::setw(16) << value;
        return stream.str();
    }

} // namespace HIKARI::TEXT
