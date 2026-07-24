#include "Scene/Debug/HIKARI_ComponentGizmoRenderer.h"

#include "Scene/Debug/HIKARI_BuiltInComponentGizmoProviders.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    ComponentGizmoRenderer::ComponentGizmoRenderer() {
        RegisterBuiltInComponentGizmoProviders(registry_);
    }

    ComponentGizmoRegistry& ComponentGizmoRenderer::Registry() noexcept {
        return registry_;
    }

    const ComponentGizmoRegistry&
        ComponentGizmoRenderer::Registry() const noexcept {
        return registry_;
    }

    void ComponentGizmoRenderer::SubmitWorldGizmos(
        const World& world,
        const ComponentGizmoState& state,
        SceneObjectId selectedObjectId,
        float cameraAspect) const {

        if (!state.showComponentGizmos) {
            return;
        }

        const ComponentGizmoDrawContext context{
            selectedObjectId,
            cameraAspect,
            &world
        };
        for (const auto& object : world.GetObjects()) {
            if (!object ||
                state.lockedObjectIds.contains(
                    object->GetDocumentId().value) ||
                (state.showOnlySelectedObject &&
                    (selectedObjectId.value == 0 ||
                        object->GetDocumentId() != selectedObjectId))) {
                continue;
            }
            for (const ComponentGizmoProvider& provider :
                registry_.GetProviders()) {
                if (state.IsProviderVisible(
                        provider.providerId,
                        provider.defaultVisible)) {
                    provider.draw(*object, context);
                }
            }
        }
    }

} // namespace HIKARI
