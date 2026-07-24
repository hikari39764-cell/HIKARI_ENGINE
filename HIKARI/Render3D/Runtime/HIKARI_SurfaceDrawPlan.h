#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class SurfaceDrawCommandPass : uint8_t {
        Forward,
        DepthAware,
        Shadow,
    };

    enum class SurfaceGeometryBackend : uint8_t {
        TriangleMesh,
        ClusterGeometry,
    };

    enum class SurfaceBackendRoute : uint8_t {
        Unsupported,
        MeshShader,
        StaticVsPs,
        SkinnedVsPs,
    };

    struct SurfaceResourceIds {
        MeshResourceHandle mesh{};
        MaterialResourceHandle material{};
        ClusterGeometryResourceHandle clusterGeometry{};

        SurfaceGeometryBackend geometryBackend = SurfaceGeometryBackend::TriangleMesh;

        uint64_t modelKey = 0;
        uint64_t geometryKey = 0;
        uint64_t clusterGeometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t shaderKey = 0;
        uint64_t pipelineKey = 0;

        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;

        constexpr bool HasStableKeys() const {
            return modelKey != 0 && geometryKey != 0 && materialKey != 0;
        }

        constexpr bool HasPoolHandles() const {
            return mesh.IsValid() && material.IsValid();
        }
    };

    struct SurfaceTriangleMeshGpuView {
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};

        bool IsValid() const {
            return
                vertexBuffer.BufferLocation != 0 &&
                vertexBuffer.SizeInBytes != 0 &&
                vertexBuffer.StrideInBytes != 0 &&
                indexBuffer.BufferLocation != 0 &&
                indexBuffer.SizeInBytes != 0;
        }
    };

    struct SurfaceDrawBatchKey {
        SurfaceDrawCommandPass pass = SurfaceDrawCommandPass::Forward;
        SurfaceGeometryBackend geometryBackend = SurfaceGeometryBackend::TriangleMesh;
        SurfaceBackendRoute backendRoute = SurfaceBackendRoute::Unsupported;
        uint64_t psoKey = 0;
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        bool transparent = false;
        bool alphaMasked = false;
        bool doubleSided = false;
        bool materialFx = false;
        bool waterMaterialFx = false;
        bool materialFxUsesCustomVertexShader = false;
        bool depthAware = false;
        bool clusterMainlineEligible = false;
    };

    template <typename SurfaceResourceKey>
    inline SurfaceDrawBatchKey BuildSurfaceDrawBatchKey(
        SurfaceDrawCommandPass pass,
        const SurfaceResourceKey& key) {

        SurfaceDrawBatchKey batchKey{};
        batchKey.pass = pass;
        batchKey.geometryBackend = key.geometryBackend;
        batchKey.backendRoute = key.backendRoute;
        batchKey.psoKey = key.psoKey;
        batchKey.geometryKey = key.geometryKey;
        batchKey.materialKey = key.materialKey;
        batchKey.textureSetKey = key.textureSetKey;
        batchKey.transparent = key.transparent;
        batchKey.alphaMasked = key.alphaMasked;
        batchKey.doubleSided = key.doubleSided;
        batchKey.materialFx = key.materialFx;
        batchKey.waterMaterialFx = key.waterMaterialFx;
        batchKey.materialFxUsesCustomVertexShader =
            key.materialFxUsesCustomVertexShader;
        batchKey.depthAware = key.depthAware;
        batchKey.clusterMainlineEligible = key.clusterMainlineEligible;
        return batchKey;
    }

    inline bool IsValidSurfaceDrawBatchKey(const SurfaceDrawBatchKey& key) {
        return key.psoKey != 0 && key.geometryKey != 0;
    }

    inline bool IsSameSurfaceDrawBatchKey(
        const SurfaceDrawBatchKey& lhs,
        const SurfaceDrawBatchKey& rhs) {

        return
            lhs.pass == rhs.pass &&
            lhs.geometryBackend == rhs.geometryBackend &&
            lhs.backendRoute == rhs.backendRoute &&
            lhs.psoKey == rhs.psoKey &&
            lhs.geometryKey == rhs.geometryKey &&
            lhs.materialKey == rhs.materialKey &&
            lhs.textureSetKey == rhs.textureSetKey &&
            lhs.transparent == rhs.transparent &&
            lhs.alphaMasked == rhs.alphaMasked &&
            lhs.doubleSided == rhs.doubleSided &&
            lhs.materialFx == rhs.materialFx &&
            lhs.waterMaterialFx == rhs.waterMaterialFx &&
            lhs.materialFxUsesCustomVertexShader ==
                rhs.materialFxUsesCustomVertexShader &&
            lhs.depthAware == rhs.depthAware &&
            lhs.clusterMainlineEligible == rhs.clusterMainlineEligible;
    }

    struct SurfaceDrawIndexedArgs {
        uint32_t indexCountPerInstance = 0;
        uint32_t instanceCount = 0;
        uint32_t startIndexLocation = 0;
        int32_t baseVertexLocation = 0;
        uint32_t startInstanceLocation = 0;
    };

    struct SurfaceDrawCommand {
        SurfaceDrawCommandPass pass = SurfaceDrawCommandPass::Forward;

        uint32_t firstExecutableIndex = 0;
        uint32_t recordCount = 0;
        uint32_t firstRecordIndex = kInvalidRenderSurfaceIndex;

        uint32_t firstGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t gpuSceneInstanceCount = 0;
        uint32_t firstClusterRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeCount = 0;

        SurfaceDrawBatchKey batchKey{};
        SurfaceResourceIds resources{};
        SurfaceDrawIndexedArgs drawArgs{};
        uint64_t psoKey = 0;
        uint64_t geometryKey = 0;
        SurfaceGeometryBackend geometryBackend = SurfaceGeometryBackend::TriangleMesh;
        SurfaceBackendRoute backendRoute = SurfaceBackendRoute::Unsupported;
        uint64_t materialKey = 0;
        uint64_t textureSetKey = 0;
        uint64_t modelKey = 0;
        SurfaceTriangleMeshGpuView triangleMeshView{};
        D3D12_GPU_VIRTUAL_ADDRESS jointPaletteGpuAddress = 0;
        VFX::VariantKey traditionalVariant{};
        uint32_t traditionalBucketIndex = kInvalidRenderSurfaceIndex;

        bool singleRecord = false;
        bool transparent = false;
        bool alphaMasked = false;
        bool doubleSided = false;
        bool materialFx = false;
        bool waterMaterialFx = false;
        bool materialFxUsesCustomVertexShader = false;
        bool clusterMainlineEligible = false;
        bool drawArgsValid = false;

        bool HasTriangleMeshGpuView() const {
            return triangleMeshView.IsValid();
        }
    };

} // namespace HIKARI::RENDER3D::RUNTIME
