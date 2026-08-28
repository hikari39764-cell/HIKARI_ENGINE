#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace HIKARI::INTERNAL {

    inline size_t GrowWorldCapacity(
        size_t current,
        size_t required,
        size_t maximum,
        size_t initial = 64u) {
        if (required > maximum) {
            throw std::length_error(
                "World storage capacity overflow.");
        }
        if (current >= required) {
            return current;
        }

        size_t target = (std::max)(
            (std::min)(initial, maximum),
            current);
        while (target < required) {
            if (target > maximum / 2u) {
                target = maximum;
                break;
            }
            target *= 2u;
        }
        return target;
    }

} // namespace HIKARI::INTERNAL
