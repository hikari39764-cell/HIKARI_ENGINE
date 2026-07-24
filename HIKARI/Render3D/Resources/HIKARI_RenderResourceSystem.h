#pragma once

#include <string>

#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::RENDER3D {

    struct RenderResourceSystemStats {
        RenderResourcePoolStats pool{};
    };

    RenderResourcePool& GetRenderResourcePool();
    RenderResourceSystemStats GetRenderResourceSystemStats();

    MeshResourceHandle RegisterVirtualMeshResource(
        const std::string& sourceKey,
        const std::string& debugName = {});
    MaterialResourceHandle RegisterVirtualMaterialResource(
        const std::string& sourceKey,
        const std::string& debugName = {});
    ClusterGeometryResourceHandle RegisterVirtualClusterGeometryResource(
        const std::string& sourceKey,
        const std::string& debugName = {});

} // namespace HIKARI::RENDER3D
