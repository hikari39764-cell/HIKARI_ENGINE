#pragma once

#include <cstdint>

namespace HIKARI {

    struct SceneObjectId {
        uint64_t value = 0;

        bool operator==(const SceneObjectId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

} // namespace HIKARI
