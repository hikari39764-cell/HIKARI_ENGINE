#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class IComponent;

    inline constexpr uint32_t kInvalidWorldComponentIndex =
        (std::numeric_limits<uint32_t>::max)();

    struct IndexedWorldComponent {
        RuntimeObjectHandle object{};
        IComponent* component = nullptr;
        uint32_t previous = kInvalidWorldComponentIndex;
        uint32_t next = kInvalidWorldComponentIndex;
        uint64_t attachSequence = 0u;
    };

    struct WorldComponentBucket {
        ComponentTypeId typeId{};
        std::string typeName{};
        std::vector<IndexedWorldComponent> components{};
        uint32_t head = kInvalidWorldComponentIndex;
        uint32_t tail = kInvalidWorldComponentIndex;
        uint64_t nextAttachSequence = 1u;
    };

} // namespace HIKARI
