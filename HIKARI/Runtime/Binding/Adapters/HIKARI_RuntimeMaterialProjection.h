#pragma once

#include <cstdint>
#include <memory>

#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

namespace HIKARI::RENDER3D::MATERIAL {
    struct MaterialExternalValueSnapshot;
}

namespace HIKARI {
    class Material;
    class World;
}

namespace HIKARI::BINDING::MATERIAL_ADAPTER {

    // Main-thread composition seam: Material-owned immutable values -> Scene
    // materials. Returns the number of unique runtime Material instances that
    // actually changed.
    uint32_t ApplyExternalValuesToRuntimeMaterials(
        World& world,
        const RENDER3D::MATERIAL::MaterialExternalValueSnapshot& snapshot);

    bool ApplyExternalValuesToRuntimeMaterial(
        Material& material,
        const RENDER3D::MATERIAL::MaterialExternalValueSnapshot& snapshot);

    // Converts a Runtime-owned immutable snapshot into the Material-owned
    // render contract. The returned value contains no Runtime container and
    // can be shared safely with render submissions and tool previews.
    std::shared_ptr<const RENDER3D::MATERIAL::
        MaterialExternalValueSnapshot>
        BuildMaterialExternalValueSnapshot(
            const Snapshot& snapshot);

} // namespace HIKARI::BINDING::MATERIAL_ADAPTER
