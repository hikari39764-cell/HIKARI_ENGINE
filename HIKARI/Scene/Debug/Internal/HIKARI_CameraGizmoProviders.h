#pragma once

#include <string_view>

namespace HIKARI {

class ComponentGizmoRegistry;

inline constexpr std::string_view kCameraFrustumGizmoProviderId = "CameraFrustums";
inline constexpr std::string_view kSpawnPointGizmoProviderId = "SpawnPoints";
inline constexpr std::string_view kCameraActivationVolumeGizmoProviderId = "CameraActivationVolumes";

void RegisterCameraGizmoProviders(ComponentGizmoRegistry& registry);

} // namespace HIKARI
