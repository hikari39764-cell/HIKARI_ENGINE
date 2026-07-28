#include "Scene/Debug/HIKARI_BuiltInComponentGizmoProviders.h"

#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/Internal/HIKARI_CameraGizmoProviders.h"
#include "Scene/Debug/Internal/HIKARI_LightingGizmoProviders.h"

namespace HIKARI {

    void RegisterBuiltInComponentGizmoProviders(
        ComponentGizmoRegistry& registry) {
        RegisterCameraGizmoProviders(registry);
        RegisterLightingGizmoProviders(registry);
        RegisterColliderGizmoProvider(registry);
    }

} // namespace HIKARI
