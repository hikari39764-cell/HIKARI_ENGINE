#include "Scene/Components/HIKARI_GroundAnchorComponent.h"

// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Authoring/HIKARI_FieldInspector.h"

namespace HIKARI {

void GroundAnchorComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Text("Keeps the object on the ground: moving it, or changing the terrain under it, places it back on "
                 "the ground with these settings. Remove this component to place the object freely.");
    GroundPlacementSettings next = settings_;
    if (InspectFields(builder, next, kGroundPlacementFields)) {
        settings_ = next;
    }
}

} // namespace HIKARI

#endif
