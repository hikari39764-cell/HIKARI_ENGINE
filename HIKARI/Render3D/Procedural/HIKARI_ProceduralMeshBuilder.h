#pragma once

#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {
    struct MeshPrimitive;
}

namespace HIKARI::PROCEDURAL {

    void BuildProceduralMesh(
        const ProceduralMeshSettings& settings,
        MeshPrimitive& outPrimitive);

} // namespace HIKARI::PROCEDURAL
