#pragma once

#include <cstdint>

namespace HIKARI {
    class IInspectorBuilder;

    // Shared project-layer authoring for every runtime physics participant.
    // The component keeps the resolved numeric contract; the editor supplies
    // project vocabulary without coupling runtime code to layer names.
    bool DrawPhysicsCollisionFilterInspector(
        IInspectorBuilder& builder,
        uint32_t& collisionLayer,
        uint32_t& collisionMask);

} // namespace HIKARI
