#pragma once

#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"

namespace HIKARI {

    class World;

    class ComponentGizmoRenderer {
    public:
        ComponentGizmoRenderer();

        ComponentGizmoRegistry& Registry() noexcept;
        const ComponentGizmoRegistry& Registry() const noexcept;

        void SubmitWorldGizmos(
            const World& world,
            const ComponentGizmoState& state,
            SceneObjectId selectedObjectId,
            float cameraAspect) const;

    private:
        ComponentGizmoRegistry registry_{};
    };

} // namespace HIKARI
