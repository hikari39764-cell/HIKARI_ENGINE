#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class IComponent;

    inline constexpr uint32_t kInvalidWorldComponentIndex =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr size_t kMaximumWorldComponentQueryTypeCount = 8u;

    enum class WorldComponentQueryMatch : uint8_t {
        All = 0u,
        Count,
    };

    enum class WorldComponentQueryCardinality : uint8_t {
        AllEntries = 0u,
        FirstPerObject,
        Count,
    };

    enum class WorldComponentQueryAnchor : uint8_t {
        FirstRequiredType = 0u,
        SmallestUniqueBucket,
        Count,
    };

    enum class WorldComponentQueryOrder : uint8_t {
        AnchorAttachment = 0u,
        Count,
    };

    struct WorldComponentQueryPlan {
        std::span<const ComponentTypeId> requiredTypes{};
        WorldComponentQueryMatch match = WorldComponentQueryMatch::All;
        WorldComponentQueryCardinality cardinality =
            WorldComponentQueryCardinality::AllEntries;
        WorldComponentQueryAnchor anchor =
            WorldComponentQueryAnchor::FirstRequiredType;
        WorldComponentQueryOrder order =
            WorldComponentQueryOrder::AnchorAttachment;
    };

    struct IndexedWorldComponent {
        RuntimeObjectHandle object{};
        IComponent* component = nullptr;
        uint32_t previous = kInvalidWorldComponentIndex;
        uint32_t next = kInvalidWorldComponentIndex;
        uint32_t previousObject = kInvalidWorldComponentIndex;
        uint32_t nextObject = kInvalidWorldComponentIndex;
        uint64_t attachSequence = 0u;
        uint64_t objectAttachSequence = 0u;
    };

    struct WorldComponentBucket {
        ComponentTypeId typeId{};
        std::string typeName{};
        std::vector<IndexedWorldComponent> components{};
        // First attachment for one object. Single-type queries still traverse
        // every entry so allowMultiple components remain observable; joined
        // queries use this table for one deterministic row per object.
        std::unordered_map<uint64_t, uint32_t> firstByObject{};
        const std::type_info* nativeType = nullptr;
        uint32_t head = kInvalidWorldComponentIndex;
        uint32_t tail = kInvalidWorldComponentIndex;
        uint32_t objectHead = kInvalidWorldComponentIndex;
        uint32_t objectTail = kInvalidWorldComponentIndex;
        uint64_t nextAttachSequence = 1u;
    };

} // namespace HIKARI
