#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceContract.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"
#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::RUNTIME {

    constexpr uint32_t kSurfaceGpuSceneMaxJointMatrices = 128u;
    constexpr uint32_t kSurfaceGpuSceneMaxDeformationPalettes = 2048u;
    constexpr uint32_t kSurfaceGpuSceneJointPaletteStrideBytes =
        kSurfaceGpuSceneMaxJointMatrices * sizeof(MATH::Mat4);

    enum class SurfaceGpuSceneInstanceFlags : uint32_t {
        None = 0,
        StaticGeometry = 1u << 0,
        CastShadow = 1u << 1,
        ReceiveShadow = 1u << 2,
        AlphaMasked = 1u << 3,
        Transparent = 1u << 4,
        MaterialOverride = 1u << 5,
        DoubleSided = 1u << 6,
        MaterialFx = 1u << 7,
        ClusterMainline = 1u << 8,
        WaterMaterialFx = 1u << 9,
        DepthAware = 1u << 10,
        PassForwardOpaque = 1u << 11,
        PassDepthPrepass = 1u << 12,
        PassForwardDepthAware = 1u << 13,
        PassForwardTransparent = 1u << 14,
        PassShadow = 1u << 15,
        Skinned = 1u << 16,
    };

    enum class SurfaceGpuSceneResourceFlags : uint32_t {
        None = 0,
        Mesh = 1u << 0,
        Material = 1u << 1,
        ClusterGeometry = 1u << 2,
        ClusterGeometryShaderVisible = 1u << 3,
        ClusterGeometrySurfaceRange = 1u << 4,
        ClusterGeometryLodRanges = 1u << 5,
    };

    struct SurfaceGpuSceneInstance {
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        MATH::Mat4 clusterWorld{};
        MATH::Mat4 clusterNormalMatrix{};
        MATH::Vec4 boundsCenterRadius{};

        uint32_t sourceRecordIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t objectIdLow = 0;
        uint32_t objectIdHigh = 0;

        uint32_t meshIndex = kInvalidRenderSurfaceIndex;
        uint32_t primitiveIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceMaterialIndex = 0;
        uint32_t nodeIndex = kInvalidRenderSurfaceIndex;

        uint32_t flags = 0;
        uint32_t materialDataIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterRangeCount = 0;

        uint32_t meshResourceIndex = 0;
        uint32_t meshResourceGeneration = 0;
        uint32_t materialResourceIndex = 0;
        uint32_t materialResourceGeneration = 0;

        uint32_t clusterGeometryResourceIndex = 0;
        uint32_t clusterGeometryMetadataSrvDescriptorIndex = kInvalidRenderSurfaceIndex;
        uint32_t resourceFlags = 0;
        uint32_t geometryBackend = 0;

        uint32_t fxFlags = 0;
        uint32_t clusterGeometrySrvDescriptorIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterSurfaceIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterIndexCount = 0;

        uint32_t clusterLodRangeIndex = kInvalidRenderSurfaceIndex;
        uint32_t clusterLodRangeCount = 0;
        uint32_t clusterSelectedLodIndex = 0;
        uint32_t clusterLodFlags = 0;

        uint32_t jointPaletteOffsetBytes = 0;
        uint32_t jointPaletteMatrixCount = 0;
        uint32_t deformationFlags = 0;
        uint32_t deformationReserved = 0;

        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    };

    static_assert(sizeof(SurfaceGpuSceneInstance) == 528u);
    static_assert(kSurfaceGpuSceneJointPaletteStrideBytes == 8192u);

    struct SurfaceGpuSceneMaterialSource {
        const ModelAsset* model = nullptr;
        const Material* materialOverride = nullptr;

        uint32_t localGpuSceneInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceRecordIndex = kInvalidRenderSurfaceIndex;
        uint32_t sourceSurfaceInstanceIndex = kInvalidRenderSurfaceIndex;
        uint32_t materialIndex = 0;
        uint64_t materialKey = 0;
        MaterialResourceHandle materialResource{};
        uint64_t materialRevision = 0;

        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        bool receiveShadow = true;

        uint32_t fxFlags = 0;
        MATH::Vec4 fxUser[VFX::kMaterialFxUserCount]{};
    };

    struct SurfaceGpuSceneBuildStats {
        uint32_t commandCount = 0;
        uint32_t instanceCount = 0;
        uint32_t skippedInvalidCommandCount = 0;
        uint32_t skippedInvalidRecordCount = 0;
        uint32_t maxCommandInstanceCount = 0;
        uint32_t resourceBackedInstanceCount = 0;
        uint32_t missingResourceHandleInstanceCount = 0;
        uint32_t clusterResourceInstanceCount = 0;
        uint32_t clusterShaderVisibleInstanceCount = 0;
        uint32_t clusterSurfaceRangeInstanceCount = 0;
        uint32_t clusterMissingSurfaceRangeInstanceCount = 0;
    };

} // namespace HIKARI::RENDER3D::RUNTIME
