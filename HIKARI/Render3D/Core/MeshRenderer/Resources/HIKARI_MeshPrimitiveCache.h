#pragma once

#include <memory>
#include <unordered_map>

#include <d3d12.h>

#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/HIKARI_Mesh.h"

namespace HIKARI {
struct MeshPrimitive;
} // namespace HIKARI

namespace HIKARI::MESHRENDERER {

class MeshPrimitiveCache {
public:
    void Clear();

    Mesh* GetOrCreateStatic(ID3D12Device* device, const MeshPrimitive& primitive, MeshRendererDebugStats* stats);

    Mesh* GetOrCreateSkinned(ID3D12Device* device, const MeshPrimitive& primitive, MeshRendererDebugStats* stats);

private:
    std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveMeshCache_;
    std::unordered_map<const MeshPrimitive*, std::unique_ptr<Mesh>> primitiveSkinnedMeshCache_;
};

} // namespace HIKARI::MESHRENDERER
