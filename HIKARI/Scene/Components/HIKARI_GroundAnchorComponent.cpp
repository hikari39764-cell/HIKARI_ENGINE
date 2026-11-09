#include "Scene/Components/HIKARI_GroundAnchorComponent.h"

// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Authoring/HIKARI_FieldInspector.h"

namespace HIKARI {

void GroundAnchorComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Text("Follows the ground: when the terrain under it changes, the object is placed back on it. "
                 "End drops the selection with these settings.");
    GroundPlacementSettings next = settings_;
    if (InspectFields(builder, next, kGroundPlacementFields)) {
        settings_ = next;
    }
}

} // namespace HIKARI

#endif
