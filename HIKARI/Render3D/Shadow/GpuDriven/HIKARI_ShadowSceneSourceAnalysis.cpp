#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace HIKARI::SHADOW::INTERNAL {

    bool HasSurfaceGpuSceneFlag(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags flag) {

        return (instance.flags & static_cast<uint32_t>(flag)) != 0u;
    }

    bool IsPrimaryShadowCaster(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

        using RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags;
        return
            HasSurfaceGpuSceneFlag(instance, SurfaceGpuSceneInstanceFlags::PassShadow) &&
            HasSurfaceGpuSceneFlag(instance, SurfaceGpuSceneInstanceFlags::CastShadow);
    }

    bool IsStaticPrimaryShadowCaster(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

        return IsPrimaryShadowCaster(instance) &&
            HasSurfaceGpuSceneFlag(
                instance,
                RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry);
    }

    uint64_t HashSurfaceGpuSceneInstanceForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance) {

        seed = AppendShadowHashBytes(seed, &instance.world, sizeof(instance.world));
        seed = AppendShadowHashBytes(seed, &instance.normalMatrix, sizeof(instance.normalMatrix));
        seed = AppendShadowHashBytes(seed, &instance.clusterWorld, sizeof(instance.clusterWorld));
        seed = AppendShadowHashBytes(
            seed,
            &instance.clusterNormalMatrix,
            sizeof(instance.clusterNormalMatrix));
        seed = AppendShadowHashBytes(seed, &instance.boundsCenterRadius, sizeof(instance.boundsCenterRadius));
        seed = AppendShadowHashValue(seed, instance.sourceRecordIndex);
        seed = AppendShadowHashValue(seed, instance.sourceSurfaceInstanceIndex);
        seed = AppendShadowHashValue(seed, instance.objectIdLow);
        seed = AppendShadowHashValue(seed, instance.objectIdHigh);
        seed = AppendShadowHashValue(seed, instance.meshIndex);
        seed = AppendShadowHashValue(seed, instance.primitiveIndex);
        seed = AppendShadowHashValue(seed, instance.sourceMaterialIndex);
        seed = AppendShadowHashValue(seed, instance.nodeIndex);
        seed = AppendShadowHashValue(seed, instance.flags);
        seed = AppendShadowHashValue(seed, instance.clusterRangeIndex);
        seed = AppendShadowHashValue(seed, instance.clusterRangeCount);
        seed = AppendShadowHashValue(seed, instance.meshResourceIndex);
        seed = AppendShadowHashValue(seed, instance.meshResourceGeneration);
        seed = AppendShadowHashValue(seed, instance.materialResourceIndex);
        seed = AppendShadowHashValue(seed, instance.materialResourceGeneration);
        seed = AppendShadowHashValue(seed, instance.clusterGeometryResourceIndex);
        seed = AppendShadowHashValue(seed, instance.clusterGeometryMetadataSrvDescriptorIndex);
        seed = AppendShadowHashValue(seed, instance.resourceFlags);
        seed = AppendShadowHashValue(seed, instance.geometryBackend);
        seed = AppendShadowHashValue(seed, instance.fxFlags);
        seed = AppendShadowHashValue(seed, instance.clusterGeometrySrvDescriptorIndex);
        seed = AppendShadowHashValue(seed, instance.clusterSurfaceIndex);
        seed = AppendShadowHashValue(seed, instance.clusterIndexCount);
        seed = AppendShadowHashValue(seed, instance.clusterLodRangeIndex);
        seed = AppendShadowHashValue(seed, instance.clusterLodRangeCount);
        seed = AppendShadowHashValue(seed, instance.clusterSelectedLodIndex);
        seed = AppendShadowHashValue(seed, instance.clusterLodFlags);
        for (const MATH::Vec4& value : instance.fxUser) {
            seed = AppendShadowHashBytes(seed, &value, sizeof(value));
        }
        return seed;
    }

    uint64_t HashSurfaceGpuSceneMaterialSourceForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source) {

        seed = AppendShadowHashValue(seed, source.localGpuSceneInstanceIndex);
        seed = AppendShadowHashValue(seed, source.sourceRecordIndex);
        seed = AppendShadowHashValue(seed, source.sourceSurfaceInstanceIndex);
        seed = AppendShadowHashValue(seed, source.materialIndex);
        seed = AppendShadowHashValue(seed, source.materialKey);
        seed = AppendShadowHashBytes(seed, &source.world, sizeof(source.world));
        seed = AppendShadowHashBytes(seed, &source.normalMatrix, sizeof(source.normalMatrix));
        seed = AppendShadowHashValue(seed, source.receiveShadow ? 1u : 0u);
        seed = AppendShadowHashValue(seed, source.fxFlags);
        for (const MATH::Vec4& value : source.fxUser) {
            seed = AppendShadowHashBytes(seed, &value, sizeof(value));
        }
        return seed;
    }

    uint64_t HashSurfaceResourceIdsForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceResourceIds& resources) {

        seed = AppendShadowHashValue(seed, resources.mesh.index);
        seed = AppendShadowHashValue(seed, resources.mesh.generation);
        seed = AppendShadowHashValue(seed, resources.material.index);
        seed = AppendShadowHashValue(seed, resources.material.generation);
        seed = AppendShadowHashValue(seed, resources.clusterGeometry.index);
        seed = AppendShadowHashValue(seed, resources.clusterGeometry.generation);
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(resources.geometryBackend));
        seed = AppendShadowHashValue(seed, resources.modelKey);
        seed = AppendShadowHashValue(seed, resources.geometryKey);
        seed = AppendShadowHashValue(seed, resources.clusterGeometryKey);
        seed = AppendShadowHashValue(seed, resources.materialKey);
        seed = AppendShadowHashValue(seed, resources.textureSetKey);
        seed = AppendShadowHashValue(seed, resources.shaderKey);
        seed = AppendShadowHashValue(seed, resources.pipelineKey);
        seed = AppendShadowHashValue(seed, resources.meshIndex);
        seed = AppendShadowHashValue(seed, resources.primitiveIndex);
        seed = AppendShadowHashValue(seed, resources.materialIndex);
        return seed;
    }

    uint64_t HashSurfaceDrawBatchKeyForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceDrawBatchKey& key) {

        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(key.pass));
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(key.geometryBackend));
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(key.backendRoute));
        seed = AppendShadowHashValue(seed, key.psoKey);
        seed = AppendShadowHashValue(seed, key.geometryKey);
        seed = AppendShadowHashValue(seed, key.materialKey);
        seed = AppendShadowHashValue(seed, key.textureSetKey);
        seed = AppendShadowHashValue(seed, key.transparent ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.alphaMasked ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.doubleSided ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.materialFx ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.waterMaterialFx ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.materialFxUsesCustomVertexShader ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.depthAware ? 1u : 0u);
        seed = AppendShadowHashValue(seed, key.clusterMainlineEligible ? 1u : 0u);
        return seed;
    }

    uint64_t HashSurfaceDrawArgsForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceDrawIndexedArgs& args) {

        seed = AppendShadowHashValue(seed, args.indexCountPerInstance);
        seed = AppendShadowHashValue(seed, args.instanceCount);
        seed = AppendShadowHashValue(seed, args.startIndexLocation);
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(args.baseVertexLocation));
        seed = AppendShadowHashValue(seed, args.startInstanceLocation);
        return seed;
    }

    uint64_t HashSurfaceDrawCommandForShadow(
        uint64_t seed,
        const RENDER3D::RUNTIME::SurfaceDrawCommand& command) {

        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(command.pass));
        seed = AppendShadowHashValue(seed, command.recordCount);
        seed = AppendShadowHashValue(seed, command.firstRecordIndex);
        seed = AppendShadowHashValue(seed, command.gpuSceneInstanceCount);
        seed = AppendShadowHashValue(seed, command.clusterRangeCount);
        seed = HashSurfaceDrawBatchKeyForShadow(seed, command.batchKey);
        seed = HashSurfaceResourceIdsForShadow(seed, command.resources);
        seed = HashSurfaceDrawArgsForShadow(seed, command.drawArgs);
        seed = AppendShadowHashValue(seed, command.psoKey);
        seed = AppendShadowHashValue(seed, command.geometryKey);
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(command.geometryBackend));
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(command.backendRoute));
        seed = AppendShadowHashValue(seed, command.materialKey);
        seed = AppendShadowHashValue(seed, command.textureSetKey);
        seed = AppendShadowHashValue(seed, command.modelKey);
        seed = AppendShadowHashString(seed, command.traditionalVariant.shaderId);
        seed = AppendShadowHashString(seed, command.traditionalVariant.vertexShaderId);
        seed = AppendShadowHashString(seed, command.traditionalVariant.pixelShaderId);
        seed = AppendShadowHashValue(seed, command.traditionalVariant.featureBits);
        seed = AppendShadowHashValue(seed, static_cast<uint32_t>(command.traditionalVariant.composite));
        seed = AppendShadowHashValue(seed, command.traditionalVariant.depthTest ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.traditionalVariant.depthWrite ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.traditionalVariant.doubleSided ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.singleRecord ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.transparent ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.alphaMasked ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.doubleSided ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.materialFx ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.waterMaterialFx ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.materialFxUsesCustomVertexShader ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.clusterMainlineEligible ? 1u : 0u);
        seed = AppendShadowHashValue(seed, command.drawArgsValid ? 1u : 0u);
        return seed;
    }

    void AppendShadowPrimaryInstance(
        const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& sourceInstance,
        const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* sourceMaterial,
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>& instances,
        std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources) {

        const uint32_t localIndex =
            static_cast<uint32_t>((std::min)(
                instances.size(),
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        RENDER3D::RUNTIME::SurfaceGpuSceneInstance instance = sourceInstance;
        instance.materialDataIndex =
            RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
        instances.push_back(instance);

        RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource material{};
        if (sourceMaterial != nullptr) {
            material = *sourceMaterial;
        }
        material.localGpuSceneInstanceIndex = localIndex;
        material.sourceRecordIndex = sourceInstance.sourceRecordIndex;
        material.sourceSurfaceInstanceIndex =
            sourceInstance.sourceSurfaceInstanceIndex;
        materialSources.push_back(material);
    }

    uint64_t BuildShadowSourceLayoutHash(
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) {

        uint64_t seed = AppendShadowHashValue(1469598103934665603ull, 0x53484c4fu);
        seed = AppendShadowHashValue(seed, pass.gpuSceneInstanceCount);
        seed = AppendShadowHashValue(seed, pass.traditionalIndirect.gpuSceneInstanceCount);
        seed = AppendShadowHashValue(seed, pass.traditionalIndirect.staticCommandCount);
        seed = AppendShadowHashValue(seed, pass.traditionalIndirect.skinnedCommandCount);
        if (pass.instances != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                *pass.instances) {
                seed = AppendShadowHashValue(seed, instance.sourceRecordIndex);
                seed = AppendShadowHashValue(seed, instance.sourceSurfaceInstanceIndex);
                seed = AppendShadowHashValue(seed, instance.meshIndex);
                seed = AppendShadowHashValue(seed, instance.primitiveIndex);
                seed = AppendShadowHashValue(seed, instance.nodeIndex);
                seed = AppendShadowHashValue(seed, instance.meshResourceIndex);
                seed = AppendShadowHashValue(seed, instance.meshResourceGeneration);
                seed = AppendShadowHashValue(seed, instance.materialResourceIndex);
                seed = AppendShadowHashValue(seed, instance.materialResourceGeneration);
                seed = AppendShadowHashValue(seed, instance.clusterGeometryResourceIndex);
                seed = AppendShadowHashValue(seed, instance.geometryBackend);
            }
        }
        if (pass.traditionalIndirect.commands != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                *pass.traditionalIndirect.commands) {
                seed = AppendShadowHashValue(seed, command.firstRecordIndex);
                seed = AppendShadowHashValue(seed, command.recordCount);
                seed = AppendShadowHashValue(seed, command.geometryKey);
                seed = AppendShadowHashValue(seed, command.materialKey);
                seed = AppendShadowHashValue(seed, command.textureSetKey);
                seed = AppendShadowHashValue(seed, command.modelKey);
            }
        }
        return seed;
    }

    uint64_t BuildShadowSourceContentHash(
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass) {

        uint64_t seed = AppendShadowHashValue(1469598103934665603ull, 0x5348434fu);
        if (pass.instances != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                *pass.instances) {
                seed = HashSurfaceGpuSceneInstanceForShadow(seed, instance);
            }
        }
        if (pass.materialSources != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source :
                *pass.materialSources) {
                seed = HashSurfaceGpuSceneMaterialSourceForShadow(seed, source);
            }
        }
        if (pass.traditionalIndirect.instances != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance :
                *pass.traditionalIndirect.instances) {
                seed = HashSurfaceGpuSceneInstanceForShadow(seed, instance);
            }
        }
        if (pass.traditionalIndirect.materialSources != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source :
                *pass.traditionalIndirect.materialSources) {
                seed = HashSurfaceGpuSceneMaterialSourceForShadow(seed, source);
            }
        }
        if (pass.traditionalIndirect.commands != nullptr) {
            for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command :
                *pass.traditionalIndirect.commands) {
                seed = HashSurfaceDrawCommandForShadow(seed, command);
            }
        }
        return seed;
    }

} // namespace HIKARI::SHADOW::INTERNAL
