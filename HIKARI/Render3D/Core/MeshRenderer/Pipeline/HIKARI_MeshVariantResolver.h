#pragma once


#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI {
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::MESHRENDERER {

    void ApplyProfileToVariant(
        const MaterialFxProfile& profile,
        VFX::VariantKey& variant);

    void ResolveDrawVariant(DrawItem& item);

    VFX::VariantKey ResolvePrimitiveVariant(
        const DrawItem& item,
        const MaterialAsset* materialAsset,
        const MeshPrimitive* primitiveAsset = nullptr);

} // namespace HIKARI::MESHRENDERER
