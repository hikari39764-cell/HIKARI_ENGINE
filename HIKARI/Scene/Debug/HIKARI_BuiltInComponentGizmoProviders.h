#pragma once

#include <string_view>

#include "Scene/Debug/HIKARI_ColliderGizmoProvider.h"

namespace HIKARI {

    class ComponentGizmoRegistry;

    inline constexpr std::string_view kCameraFrustumGizmoProviderId =
        "CameraFrustums";
    inline constexpr std::string_view kSpawnPointGizmoProviderId =
        "SpawnPoints";
    inline constexpr std::string_view kCameraActivationVolumeGizmoProviderId =
        "CameraActivationVolumes";
    void RegisterBuiltInComponentGizmoProviders(
        ComponentGizmoRegistry& registry);

} // namespace HIKARI
