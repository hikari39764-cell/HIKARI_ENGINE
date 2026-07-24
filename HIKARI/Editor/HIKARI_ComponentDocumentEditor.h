#pragma once

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    class ComponentRegistry;
    struct SceneComponentData;

    class ComponentDocumentEditor {
    public:
        bool DrawComponent(
            const ComponentRegistry& componentRegistry,
            SceneComponentData& componentData,
            IInspectorBuilder& inspectorBuilder,
            const InspectorContext& context) const;
    };

} // namespace HIKARI
