#include "Scene/Components/Rendering/Vfx/HIKARI_FluidObstacleComponent.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

void FluidObstacleComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kFluidObstacleFields);
}

void FluidObstacleComponent::Deserialize(const nlohmann::json& in) {
    FluidObstacleSettings decoded{};
    JSONFIELDS::Read(in, decoded, kFluidObstacleFields);
    settings_ = JSONFIELDS::Sanitize(decoded, kFluidObstacleFields);
}

#if defined(HIKARI_WITH_EDITOR)
void FluidObstacleComponent::BuildInspector(IInspectorBuilder& builder) {
    FluidObstacleSettings next = settings_;
    if (InspectFields(builder, next, kFluidObstacleFields))
        settings_ = JSONFIELDS::Sanitize(next, kFluidObstacleFields);
}
#endif

} // namespace HIKARI
