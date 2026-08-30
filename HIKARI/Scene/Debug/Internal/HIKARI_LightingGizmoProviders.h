#pragma once

#include <string_view>

namespace HIKARI {

    class ComponentGizmoRegistry;

    inline constexpr std::string_view kReflectionProbeGizmoProviderId =
        "ReflectionProbes";
    inline constexpr std::string_view kIrradianceVolumeGizmoProviderId =
        "IrradianceVolumes";

    void RegisterLightingGizmoProviders(
        ComponentGizmoRegistry& registry);

} // namespace HIKARI
