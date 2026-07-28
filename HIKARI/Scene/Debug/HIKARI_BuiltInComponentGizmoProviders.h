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
    inline constexpr std::string_view kReflectionProbeGizmoProviderId =
        "ReflectionProbes";
    inline constexpr std::string_view kIrradianceVolumeGizmoProviderId =
        "IrradianceVolumes";
    void RegisterBuiltInComponentGizmoProviders(
        ComponentGizmoRegistry& registry);

} // namespace HIKARI
