#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

#include <algorithm>
#include <string>
#include <utility>

#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

namespace HIKARI::RENDER3D::RUNTIME {
namespace {
bool IsPrimitiveSkinned(const MeshPrimitive& primitive) { return primitive.layout == VertexLayoutKind::SkinnedPNTTJW; }

Bounds ResolvePrimitiveBounds(const MeshPrimitive& primitive) {
    return BOUNDS::IsUsable(primitive.bounds) ? primitive.bounds : BOUNDS::ComputePrimitiveBounds(primitive);
}

Bounds ResolveMeshBounds(const MeshAsset& mesh) {
    return BOUNDS::IsUsable(mesh.bounds) ? mesh.bounds : BOUNDS::ComputeMeshBounds(mesh);
}

uint32_t ClampToSurfaceIndex(size_t value) {
    return static_cast<uint32_t>((std::min)(value, static_cast<size_t>(kInvalidRenderSurfaceIndex - 1u)));
}

RenderSurfaceRecord BuildSurfaceRecord(uint32_t surfaceIndex, uint32_t nodeIndex, uint32_t meshIndex,
                                       uint32_t primitiveIndex, int skinIndex, const MeshPrimitive& primitive) {
    RenderSurfaceRecord record{};
    record.surfaceIndex = surfaceIndex;
    record.nodeIndex = nodeIndex;
    record.meshIndex = meshIndex;
    record.primitiveIndex = primitiveIndex;
    record.materialIndex = primitive.materialIndex;
    record.localBounds = ResolvePrimitiveBounds(primitive);
    record.skinIndex = skinIndex;
    record.skinningMode =
        IsPrimitiveSkinned(primitive) ? RenderSurfaceSkinningMode::Skinned : RenderSurfaceSkinningMode::Static;
    return record;
}

void AppendSurface(RenderModelAsset& out, uint32_t nodeIndex, uint32_t meshIndex, uint32_t primitiveIndex,
                   int skinIndex, const MeshPrimitive& primitive) {
    RenderSurfaceRecord record = BuildSurfaceRecord(ClampToSurfaceIndex(out.surfaces.size()), nodeIndex, meshIndex,
                                                    primitiveIndex, skinIndex, primitive);

    if (record.skinningMode == RenderSurfaceSkinningMode::Skinned) {
        out.hasSkinnedSurfaces = true;
    } else {
        out.hasStaticSurfaces = true;
    }
    out.surfaces.push_back(std::move(record));
}

void BuildNodeRecords(const ModelAsset& source, RenderModelAsset& out) {
    out.nodes.reserve(source.nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < source.nodes.size(); ++nodeIndex) {
        const ModelNode& sourceNode = source.nodes[nodeIndex];

        RenderModelNodeRecord nodeRecord{};
        nodeRecord.nodeIndex = static_cast<uint32_t>(nodeIndex);
        nodeRecord.parentIndex = sourceNode.parent;
        nodeRecord.localMatrix = BOUNDS::GetNodeLocalMatrix(sourceNode);
        nodeRecord.meshIndex = sourceNode.meshIndex;
        nodeRecord.skinIndex = sourceNode.skinIndex;

        const bool hasValidMesh =
            sourceNode.meshIndex >= 0 && sourceNode.meshIndex < static_cast<int>(source.meshes.size());
        nodeRecord.hasMesh = hasValidMesh;
        if (hasValidMesh) {
            const MeshAsset& mesh = source.meshes[static_cast<size_t>(sourceNode.meshIndex)];
            nodeRecord.localBounds = ResolveMeshBounds(mesh);
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                AppendSurface(out, static_cast<uint32_t>(nodeIndex), static_cast<uint32_t>(sourceNode.meshIndex),
                              static_cast<uint32_t>(primitiveIndex), sourceNode.skinIndex,
                              mesh.primitives[primitiveIndex]);
            }
        }

        out.nodes.push_back(std::move(nodeRecord));
    }
}

void BuildNodelessSurfaces(const ModelAsset& source, RenderModelAsset& out) {
    for (size_t meshIndex = 0; meshIndex < source.meshes.size(); ++meshIndex) {
        const MeshAsset& mesh = source.meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            AppendSurface(out, kInvalidRenderModelIndex, static_cast<uint32_t>(meshIndex),
                          static_cast<uint32_t>(primitiveIndex), -1, mesh.primitives[primitiveIndex]);
        }
    }
}

Bounds ResolveModelBounds(const ModelAsset& source) {
    if (BOUNDS::IsUsable(source.bounds)) {
        return source.bounds;
    }
    return BOUNDS::ComputeModelBounds(source);
}
} // namespace

bool BuildRenderModelAsset(const ModelAsset& source, RenderModelAsset& out, std::string* outMessage) {
    out = {};
    out.source = &source;
    out.sourceName = source.GetName();
    out.sourcePath = source.GetSourcePath();

    if (!source.nodes.empty()) {
        BuildNodeRecords(source, out);
        out.bindNodeGlobals = BuildRenderModelNodeGlobals(out);
    } else {
        // node を持たないモデル（プロシージャルモデル等）は mesh ごとに surface を作る。
        BuildNodelessSurfaces(source, out);
    }

    out.localBounds = ResolveModelBounds(source);
    out.valid = !out.surfaces.empty();

    if (outMessage != nullptr) {
        *outMessage = out.valid ? std::string{} : "RenderModelAsset has no surface records.";
    }
    return out.valid;
}

Bounds ResolveLocalBounds(const ModelAsset* model, const RenderModelAsset* renderModel) {
    if (renderModel != nullptr && renderModel->valid && BOUNDS::IsUsable(renderModel->localBounds)) {
        return renderModel->localBounds;
    }
    if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
        return model->bounds;
    }
    return {};
}

} // namespace HIKARI::RENDER3D::RUNTIME
