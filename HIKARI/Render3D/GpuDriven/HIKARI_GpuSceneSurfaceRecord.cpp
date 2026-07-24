#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        uint32_t ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags flag) {
            return static_cast<uint32_t>(flag);
        }

        uint32_t ToResourceFlag(RUNTIME::SurfaceGpuSceneResourceFlags flag) {
            return static_cast<uint32_t>(flag);
        }

        uint32_t BuildPassMask(const GpuSceneSurfaceRecord& record) {
            uint32_t mask = 0;
            if (record.forwardCandidate) {
                mask |= static_cast<uint32_t>(GpuSceneSurfacePassFlags::Forward);
            }
            if (record.shadowCandidate) {
                mask |= static_cast<uint32_t>(GpuSceneSurfacePassFlags::Shadow);
            }
            return mask;
        }

        uint64_t HashAppend(uint64_t seed, uint64_t value) {
            constexpr uint64_t kMul = 1099511628211ull;
            seed ^= value;
            seed *= kMul;
            return seed;
        }

        uint64_t HashString(std::string_view value) {
            uint64_t hash = 1469598103934665603ull;
            for (const char c : value) {
                hash = HashAppend(hash, static_cast<uint64_t>(static_cast<unsigned char>(c)));
            }
            return hash;
        }

        uint64_t HashPointer(const void* value) {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
        }

        uint64_t HashIntSlot(int value) {
            return static_cast<uint64_t>(static_cast<int64_t>(value) + 0x100000000ll);
        }

        uint64_t HashFloatBits(float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return static_cast<uint64_t>(bits);
        }

        uint64_t HashVec2(uint64_t hash, const MATH::Vec2& value) {
            hash = HashAppend(hash, HashFloatBits(value.x));
            return HashAppend(hash, HashFloatBits(value.y));
        }

        uint64_t BuildStableStringKey(std::string_view tag, const std::string& value) {
            if (value.empty()) {
                return 0;
            }
            uint64_t key = HashString(tag);
            key = HashAppend(key, HashString(value));
            return key;
        }

        uint64_t BuildModelKey(const GpuSceneSurfaceRecord& record) {
            if (record.renderModel != nullptr) {
                if (const uint64_t sourceNameKey =
                    BuildStableStringKey("render-model-name", record.renderModel->sourceName);
                    sourceNameKey != 0) {
                    return sourceNameKey;
                }
                if (const uint64_t sourcePathKey =
                    BuildStableStringKey("render-model-path", record.renderModel->sourcePath);
                    sourcePathKey != 0) {
                    return sourcePathKey;
                }
            }

            const ModelAsset* model = record.model;
            if (model == nullptr) {
                return 0;
            }
            if (const uint64_t modelNameKey = BuildStableStringKey("model-name", model->id.value);
                modelNameKey != 0) {
                return modelNameKey;
            }
            if (const uint64_t modelPathKey = BuildStableStringKey("model-path", model->sourcePath);
                modelPathKey != 0) {
                return modelPathKey;
            }
            return HashPointer(model);
        }

        bool IsProceduralModel(const ModelAsset* model) {
            if (model == nullptr) {
                return false;
            }
            const std::string& sourcePath = model->GetSourcePath();
            return sourcePath.rfind("procedural://", 0) == 0;
        }

        uint64_t BuildClusterGeometryKey(
            const GpuSceneSurfaceRecord& record) {

            if (record.clusteredGeometryPath.empty()) {
                return 0;
            }
            if (!IsProceduralModel(record.model)) {
                return BuildStableStringKey(
                    "cluster-geometry-path",
                    record.clusteredGeometryPath);
            }

            uint64_t key = HashString("procedural-cluster-object");
            key = HashAppend(key, record.objectId.value);
            key = HashAppend(key, record.meshIndex);
            return HashAppend(key, record.primitiveIndex);
        }

        const MaterialAsset* ResolveMaterialAsset(const GpuSceneSurfaceRecord& record) {
            if (record.model == nullptr || record.materialIndex >= record.model->materials.size()) {
                return nullptr;
            }
            return &record.model->materials[record.materialIndex];
        }

        const MeshPrimitive* ResolveMeshPrimitiveAsset(const GpuSceneSurfaceRecord& record) {
            if (record.model == nullptr || record.meshIndex >= record.model->meshes.size()) {
                return nullptr;
            }
            const MeshAsset& mesh = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= mesh.primitives.size()) {
                return nullptr;
            }
            return &mesh.primitives[record.primitiveIndex];
        }

        bool HasValidRenderSurfacePrimitive(
            const ModelAsset* model,
            const RUNTIME::RenderSurfaceRecord& surface) {

            return
                model != nullptr &&
                surface.HasMeshPrimitive() &&
                surface.meshIndex < model->meshes.size() &&
                surface.primitiveIndex < model->meshes[surface.meshIndex].primitives.size();
        }

        uint64_t HashTextureSlot(uint64_t hash, const TextureSlot& slot) {
            hash = HashAppend(hash, HashIntSlot(slot.textureIndex));
            hash = HashAppend(hash, static_cast<uint64_t>(std::clamp(slot.texCoord, 0, 1)));
            hash = HashVec2(hash, slot.uvScale);
            hash = HashVec2(hash, slot.uvOffset);
            hash = HashAppend(hash, HashFloatBits(slot.uvRotation));
            return hash;
        }

        uint64_t HashRuntimeTextureSlot(
            uint64_t hash,
            const Material& material,
            MaterialTextureUsage usage) {

            const RuntimeTextureSlot& slot = material.GetTextureSlot(usage);
            hash = HashAppend(hash, HashIntSlot(slot.handle));
            hash = HashAppend(hash, static_cast<uint64_t>(std::clamp(slot.texCoord, 0, 1)));
            hash = HashVec2(hash, slot.uvScale);
            hash = HashVec2(hash, slot.uvOffset);
            return HashAppend(hash, HashFloatBits(slot.uvRotation));
        }

        uint64_t BuildTextureSetKey(
            const GpuSceneSurfaceRecord& record,
            const MaterialAsset* materialAsset) {

            uint64_t hash = HashString("texture-set");
            if (record.materialOverride != nullptr) {
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::BaseColor);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::Normal);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::MetallicRoughness);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::Occlusion);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::Emissive);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::Specular);
                hash = HashRuntimeTextureSlot(hash, *record.materialOverride, MaterialTextureUsage::SpecularColor);
                return hash;
            }
            if (materialAsset == nullptr) {
                return HashAppend(hash, HashIntSlot(-1));
            }

            hash = HashTextureSlot(hash, materialAsset->baseColorTexture);
            hash = HashTextureSlot(hash, materialAsset->normalTexture);
            hash = HashTextureSlot(hash, materialAsset->metallicRoughnessTexture);
            hash = HashTextureSlot(hash, materialAsset->occlusionTexture);
            hash = HashTextureSlot(hash, materialAsset->emissiveTexture);
            hash = HashTextureSlot(hash, materialAsset->specularTexture);
            hash = HashTextureSlot(hash, materialAsset->specularColorTexture);
            return hash;
        }

        std::string BuildSurfaceResourceSourceKey(std::string_view tag, uint64_t stableKey) {
            if (stableKey == 0) {
                return {};
            }
            std::string key{ tag };
            key.push_back(':');
            key += std::to_string(stableKey);
            return key;
        }

        std::string BuildSurfaceMeshDebugName(const RUNTIME::SurfaceResourceIds& ids) {
            return
                "SurfaceMesh model=" + std::to_string(ids.modelKey) +
                " mesh=" + std::to_string(ids.meshIndex) +
                " primitive=" + std::to_string(ids.primitiveIndex);
        }

        std::string BuildSurfaceMaterialDebugName(const RUNTIME::SurfaceResourceIds& ids) {
            return
                "SurfaceMaterial model=" + std::to_string(ids.modelKey) +
                " material=" + std::to_string(ids.materialIndex);
        }

        std::string BuildSurfaceClusterGeometryDebugName(const RUNTIME::SurfaceResourceIds& ids) {
            return
                "SurfaceClusterGeometry model=" + std::to_string(ids.modelKey) +
                " cluster=" + std::to_string(ids.clusterGeometryKey);
        }

        void RegisterSurfaceResourceHandles(
            RUNTIME::SurfaceResourceIds& ids,
            const std::string& clusteredGeometryPath) {

            if (!ids.HasStableKeys()) {
                return;
            }

            ids.mesh = RegisterVirtualMeshResource(
                BuildSurfaceResourceSourceKey("surface.mesh", ids.geometryKey),
                BuildSurfaceMeshDebugName(ids));
            ids.material = RegisterVirtualMaterialResource(
                BuildSurfaceResourceSourceKey("surface.material", ids.materialKey),
                BuildSurfaceMaterialDebugName(ids));
            if (ids.clusterGeometryKey == 0) {
                return;
            }

            const std::string clusterSourceKey =
                BuildSurfaceResourceSourceKey("surface.cluster", ids.clusterGeometryKey);
            ids.clusterGeometry = RegisterVirtualClusterGeometryResource(
                clusterSourceKey,
                BuildSurfaceClusterGeometryDebugName(ids));
            if (clusterSourceKey.empty()) {
                return;
            }

            const ClusterGeometryResourceHandle loadedClusterGeometry =
                LoadClusterGeometryResource(clusterSourceKey, clusteredGeometryPath);
            if (loadedClusterGeometry) {
                ids.clusterGeometry = loadedClusterGeometry;
            }
        }

        RUNTIME::SurfaceResourceIds BuildSurfaceResourceIds(
            const GpuSceneSurfaceRecord& record,
            RUNTIME::SurfaceGeometryBackend geometryBackend,
            uint64_t modelKey,
            uint64_t geometryKey,
            uint64_t clusterGeometryKey,
            uint64_t materialKey,
            uint64_t textureSetKey,
            uint64_t shaderKey,
            uint64_t psoKey) {

            RUNTIME::SurfaceResourceIds ids{};
            ids.geometryBackend = geometryBackend;
            ids.modelKey = modelKey;
            ids.geometryKey = geometryKey;
            ids.clusterGeometryKey = clusterGeometryKey;
            ids.materialKey = materialKey;
            ids.textureSetKey = textureSetKey;
            ids.shaderKey = shaderKey;
            ids.pipelineKey = psoKey;
            ids.meshIndex = record.meshIndex;
            ids.primitiveIndex = record.primitiveIndex;
            ids.materialIndex = record.materialIndex;
            RegisterSurfaceResourceHandles(ids, record.clusteredGeometryPath);
            return ids;
        }

        GpuSceneResourceKey BuildResourceKey(const GpuSceneSurfaceRecord& record) {
            GpuSceneResourceKey key{};
            key.materialIndex = record.materialIndex;
            key.meshIndex = record.meshIndex;
            key.primitiveIndex = record.primitiveIndex;
            key.passMask = BuildPassMask(record);
            key.hasMaterialOverride = record.materialOverride != nullptr;
            key.skinned = record.skinned;

            if (record.model == nullptr ||
                record.surface == nullptr ||
                !record.surface->HasMeshPrimitive()) {
                return key;
            }

            const MaterialAsset* materialAsset = ResolveMaterialAsset(record);
            const MeshPrimitive* primitiveAsset = ResolveMeshPrimitiveAsset(record);
            const uint64_t modelKey = BuildModelKey(record);

            const RUNTIME::SurfaceDrawShaderRoute shaderRoute =
                RUNTIME::ResolveSurfaceDrawShaderRoute(
                    record.materialOverride,
                    materialAsset,
                    record.materialFxProfileId);
            const std::string& shaderProfile = shaderRoute.shaderProfileId;
            const uint32_t featureBits = shaderRoute.featureBits;
            bool doubleSided = shaderRoute.doubleSided;
            if (record.materialOverride == nullptr &&
                materialAsset != nullptr &&
                primitiveAsset != nullptr) {
                doubleSided =
                    SURFACE_POLICY::ShouldRenderDoubleSided(*materialAsset, *primitiveAsset) ||
                    shaderRoute.profileDoubleSided;
            }

            const AlphaMode alphaMode = shaderRoute.alphaMode;
            const std::string& pixelShaderId =
                !shaderRoute.pixelShaderId.empty()
                    ? shaderRoute.pixelShaderId
                    : shaderProfile;
            const bool clusterVertexCompatible =
                shaderRoute.vertexShaderId.empty() ||
                shaderRoute.vertexShaderId == "Render3D_StaticVS" ||
                shaderRoute.vertexShaderId == "Render3D_FxWaterVS" ||
                (record.skinned && shaderRoute.vertexShaderId == "Render3D_SkinnedVS");
            const bool usesCustomVertexShader =
                !shaderRoute.vertexShaderId.empty() &&
                shaderRoute.vertexShaderId != "Render3D_StaticVS";
            const bool clusterPixelCompatible =
                pixelShaderId.empty() ||
                pixelShaderId == "PBR" ||
                pixelShaderId == "StaticLit" ||
                pixelShaderId == "StaticFx" ||
                pixelShaderId == "MaterialFx" ||
                pixelShaderId == "Render3D_StaticPS" ||
                pixelShaderId == "Render3D_StaticFxPS" ||
                pixelShaderId == "Render3D_FxWaterPS";

            key.modelKey = modelKey;
            const bool animatedPoseRecord =
                record.hasRuntimeAnimation && !record.skinned;
            key.clusterGeometryKey =
                !animatedPoseRecord
                    ? BuildClusterGeometryKey(record)
                    : 0;
            key.geometryBackend =
                key.clusterGeometryKey != 0
                    ? RUNTIME::SurfaceGeometryBackend::ClusterGeometry
                    : RUNTIME::SurfaceGeometryBackend::TriangleMesh;
            key.geometryKey = HashString("geometry");
            key.geometryKey = HashAppend(key.geometryKey, modelKey);
            key.geometryKey = HashAppend(key.geometryKey, record.meshIndex);
            key.geometryKey = HashAppend(key.geometryKey, record.primitiveIndex);
            key.geometryKey = HashAppend(key.geometryKey, record.skinned ? 1u : 0u);

            key.textureSetKey = BuildTextureSetKey(record, materialAsset);

            key.shaderKey = HashString(shaderProfile);
            key.shaderKey = HashAppend(key.shaderKey, HashString(record.materialFxProfileId));

            key.materialKey = HashString(
                record.materialOverride != nullptr ? "runtime-material" : "asset-material");
            key.materialKey = HashAppend(key.materialKey, modelKey);
            key.materialKey = HashAppend(
                key.materialKey,
                record.materialOverride != nullptr
                    ? HashPointer(record.materialOverride)
                    : record.materialIndex);
            key.materialKey = HashAppend(key.materialKey, key.textureSetKey);
            key.materialKey = HashAppend(key.materialKey, key.shaderKey);
            key.materialKey = HashAppend(key.materialKey, featureBits);
            key.materialKey = HashAppend(key.materialKey, static_cast<uint64_t>(alphaMode));
            key.materialKey = HashAppend(key.materialKey, doubleSided ? 1u : 0u);

            key.psoKey = HashString("pso");
            key.psoKey = HashAppend(key.psoKey, key.shaderKey);
            key.psoKey = HashAppend(key.psoKey, featureBits);
            key.psoKey = HashAppend(key.psoKey, record.skinned ? 1u : 0u);
            key.psoKey = HashAppend(key.psoKey, static_cast<uint64_t>(alphaMode));
            key.psoKey = HashAppend(key.psoKey, doubleSided ? 1u : 0u);

            key.sortKey = HashString("sort");
            key.sortKey = HashAppend(key.sortKey, key.passMask);
            key.sortKey = HashAppend(key.sortKey, key.psoKey);
            key.sortKey = HashAppend(key.sortKey, key.geometryKey);
            key.sortKey = HashAppend(key.sortKey, key.materialKey);
            key.sortKey = HashAppend(key.sortKey, key.textureSetKey);

            key.resources = BuildSurfaceResourceIds(
                record,
                key.geometryBackend,
                key.modelKey,
                key.geometryKey,
                key.clusterGeometryKey,
                key.materialKey,
                key.textureSetKey,
                key.shaderKey,
                key.psoKey);
            key.alphaMasked = alphaMode == AlphaMode::Mask;
            key.transparent = alphaMode == AlphaMode::Blend;
            key.doubleSided = doubleSided;
            key.resourceKeyValid = key.resources.HasStableKeys();
            key.objectDataCompatible = shaderRoute.objectDataCompatible;
            key.materialFx = !record.materialFxProfileId.empty();
            key.waterMaterialFx =
                pixelShaderId == "Render3D_FxWaterPS" ||
                shaderRoute.vertexShaderId == "Render3D_FxWaterVS";
            key.materialFxUsesCustomVertexShader =
                key.materialFx &&
                usesCustomVertexShader &&
                !key.waterMaterialFx;
            key.customVertexShader = usesCustomVertexShader;
            key.depthAware = shaderRoute.depthAware;
            const bool ordinaryMeshletMaterial =
                !key.materialFx &&
                !key.waterMaterialFx &&
                !key.depthAware &&
                !key.transparent;
            const bool materialFxMeshletMaterial =
                key.materialFx &&
                !key.materialFxUsesCustomVertexShader;
            // Cluster-compatible MaterialFX, including the built-in water
            // deformation contract, stays on the mesh-shader route.
            // Mesh shader backend はまぁEstatic opaque / alpha-mask の cluster geometry だけを所有する、E
            key.clusterMainlineEligible =
                !animatedPoseRecord &&
                key.geometryBackend == RUNTIME::SurfaceGeometryBackend::ClusterGeometry &&
                clusterVertexCompatible &&
                clusterPixelCompatible &&
                key.objectDataCompatible &&
                (ordinaryMeshletMaterial || materialFxMeshletMaterial);
            if (key.clusterMainlineEligible) {
                key.backendRoute = RUNTIME::SurfaceBackendRoute::MeshShader;
            } else if (record.skinned) {
                key.backendRoute = RUNTIME::SurfaceBackendRoute::SkinnedVsPs;
            } else {
                key.backendRoute = RUNTIME::SurfaceBackendRoute::StaticVsPs;
            }
            return key;
        }

        uint32_t BuildInstanceFlags(const GpuSceneSurfaceRecord& record) {
            uint32_t flags = 0;
            const bool staticGeometry =
                !record.skinned &&
                !record.hasRuntimeAnimation;
            if (staticGeometry) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry);
            }
            if (record.castShadow) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::CastShadow);
            }
            if (record.receiveShadow) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::ReceiveShadow);
            }
            if (record.key.alphaMasked) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::AlphaMasked);
            }
            if (record.key.transparent) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::Transparent);
            }
            if (record.materialOverride != nullptr) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::MaterialOverride);
            }
            if (record.key.doubleSided) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided);
            }
            if (!record.materialFxProfileId.empty()) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::MaterialFx);
            }
            if (record.key.waterMaterialFx) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::WaterMaterialFx);
            }
            if (record.key.depthAware) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::DepthAware);
            }
            if (record.key.clusterMainlineEligible) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::ClusterMainline);
            }
            if (record.skinned) {
                flags |= ToInstanceFlag(RUNTIME::SurfaceGpuSceneInstanceFlags::Skinned);
            }
            return flags;
        }

        void FillMaterialFxData(
            const GpuSceneSurfaceRecord& record,
            RUNTIME::SurfaceGpuSceneInstance& instance) {

            if (!record.materialFxProfileId.empty()) {
                MaterialFxProfile profile{};
                if (MaterialFxProfile::LoadById(record.materialFxProfileId, profile)) {
                    instance.fxFlags = profile.featureBits;
                    for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                        const DirectX::XMFLOAT4& value = profile.values[i];
                        instance.fxUser[i] = { value.x, value.y, value.z, value.w };
                    }
                }
            }

            if (!record.materialFxValuesInitialized) {
                return;
            }

            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                const DirectX::XMFLOAT4& value = record.materialFxParamValues[i];
                instance.fxUser[i] = { value.x, value.y, value.z, value.w };
            }
        }

        void FillClusterGeometryData(
            const GpuSceneSurfaceRecord& record,
            RUNTIME::SurfaceGpuSceneInstance& instance,
            RUNTIME::SurfaceGpuSceneBuildStats* stats) {

            const RUNTIME::SurfaceResourceIds& resources = record.key.resources;
            if (!resources.clusterGeometry) {
                return;
            }

            if (stats != nullptr) {
                ++stats->clusterResourceInstanceCount;
            }

            const ClusterGeometryResourceRecord* resourceRecord =
                GetClusterGeometryResource(resources.clusterGeometry);
            if (resourceRecord != nullptr &&
                resourceRecord->ready &&
                resourceRecord->srv.IsValid() &&
                resourceRecord->metadataSrv.IsValid()) {
                instance.clusterGeometrySrvDescriptorIndex =
                    resourceRecord->srv.descriptorIndex;
                instance.clusterGeometryMetadataSrvDescriptorIndex =
                    resourceRecord->metadataSrv.descriptorIndex;
                instance.resourceFlags |= ToResourceFlag(
                    RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometryShaderVisible);
                if (stats != nullptr) {
                    ++stats->clusterShaderVisibleInstanceCount;
                }
            }

            const CLUSTER::ClusterGeometrySurfaceRange* range =
                FindClusterGeometrySurfaceRange(
                    resources.clusterGeometry,
                    record.nodeIndex,
                    record.meshIndex,
                    record.primitiveIndex);
            if (range == nullptr) {
                if (stats != nullptr) {
                    ++stats->clusterMissingSurfaceRangeInstanceCount;
                }
                return;
            }

            instance.clusterSurfaceIndex = range->surfaceIndex;
            instance.clusterLodRangeIndex = range->firstLodRange;
            instance.clusterLodRangeCount = range->lodRangeCount;
            instance.clusterSelectedLodIndex = range->selectedLodIndex;
            const CLUSTER::ClusterGeometrySurfaceLodRange* lod0Range =
                FindClusterGeometrySurfaceLodRange(
                    resources.clusterGeometry,
                    range->firstLodRange,
                    range->lodRangeCount,
                    0u);
            if (lod0Range != nullptr) {
                instance.clusterRangeIndex = lod0Range->firstCluster;
                instance.clusterRangeCount = lod0Range->clusterCount;
                instance.clusterIndexCount = lod0Range->indexCount;
                instance.clusterSelectedLodIndex = lod0Range->lodIndex;
                instance.clusterLodFlags = lod0Range->flags;
            } else {
                instance.clusterRangeIndex = range->firstCluster;
                instance.clusterRangeCount = range->clusterCount;
                instance.clusterIndexCount = range->indexCount;
            }
            instance.resourceFlags |= ToResourceFlag(
                RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometrySurfaceRange);
            if (range->lodRangeCount > 0u) {
                instance.resourceFlags |= ToResourceFlag(
                    RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometryLodRanges);
            }
            if (stats != nullptr) {
                ++stats->clusterSurfaceRangeInstanceCount;
            }
        }

        RUNTIME::SurfaceGpuSceneInstance BuildGpuSceneInstance(
            const GpuSceneSurfaceRecord& record,
            uint32_t sourceRecordIndex) {

            RUNTIME::SurfaceGpuSceneInstance instance{};
            instance.world = record.drawWorldMatrix;
            instance.normalMatrix = MATH::BuildNormalMatrixFromWorld(record.drawWorldMatrix);
            // HCMESH は node global めEbake 済みなので、cluster draw では object world だけを渡す、E
            instance.clusterWorld = record.skinned
                ? record.drawWorldMatrix
                : record.objectWorldTransform.GetWorldMatrix();
            instance.clusterNormalMatrix = MATH::BuildNormalMatrixFromWorld(instance.clusterWorld);
            instance.boundsCenterRadius =
                BOUNDS::ComputeCenterRadius(record.worldBounds);
            instance.sourceRecordIndex = sourceRecordIndex;
            instance.sourceSurfaceInstanceIndex = record.sourceSurfaceInstanceIndex;
            instance.objectIdLow = static_cast<uint32_t>(record.objectId.value & 0xffffffffull);
            instance.objectIdHigh = static_cast<uint32_t>((record.objectId.value >> 32) & 0xffffffffull);
            instance.meshIndex = record.meshIndex;
            instance.primitiveIndex = record.primitiveIndex;
            instance.sourceMaterialIndex = record.materialIndex;
            instance.nodeIndex = record.nodeIndex;
            instance.flags = BuildInstanceFlags(record);
            instance.geometryBackend = static_cast<uint32_t>(record.key.geometryBackend);
            if (record.skinned &&
                record.jointPaletteSlot != RUNTIME::kInvalidRenderSurfaceIndex) {
                instance.jointPaletteOffsetBytes =
                    record.jointPaletteSlot *
                    RUNTIME::kSurfaceGpuSceneJointPaletteStrideBytes;
                instance.jointPaletteMatrixCount = record.jointPaletteMatrixCount;
            }

            const RUNTIME::SurfaceResourceIds& resources = record.key.resources;
            if (resources.mesh) {
                instance.meshResourceIndex = resources.mesh.index;
                instance.meshResourceGeneration = resources.mesh.generation;
                instance.resourceFlags |= ToResourceFlag(RUNTIME::SurfaceGpuSceneResourceFlags::Mesh);
            }
            if (resources.material) {
                instance.materialResourceIndex = resources.material.index;
                instance.materialResourceGeneration = resources.material.generation;
                instance.resourceFlags |= ToResourceFlag(RUNTIME::SurfaceGpuSceneResourceFlags::Material);
            }
            if (resources.clusterGeometry) {
                instance.clusterGeometryResourceIndex = resources.clusterGeometry.index;
                instance.resourceFlags |= ToResourceFlag(
                    RUNTIME::SurfaceGpuSceneResourceFlags::ClusterGeometry);
            }
            FillMaterialFxData(record, instance);
            return instance;
        }
    }

    uint64_t BuildGpuSceneStableStringKey(
        std::string_view tag,
        const std::string& value) {

        return BuildStableStringKey(tag, value);
    }

    std::string BuildGpuSceneSurfaceResourceSourceKey(
        std::string_view tag,
        uint64_t stableKey) {

        return BuildSurfaceResourceSourceKey(tag, stableKey);
    }

    std::string BuildGpuSceneClusterGeometrySourceKey(
        const std::string& clusteredGeometryPath) {

        return BuildSurfaceResourceSourceKey(
            "surface.cluster",
            BuildStableStringKey("cluster-geometry-path", clusteredGeometryPath));
    }

    bool GpuSceneSurfaceValidationResult::IsValid() const {
        return
            !invalidSource &&
            !invalidModel &&
            !unsupportedGeometry &&
            !missingDrawMatrix &&
            !invalidBounds &&
            !invalidPrimitiveIndex;
    }

    bool HasValidGpuSceneSubmitPrimitiveTarget(
        const GpuSceneSurfaceRecord& record) {

        return
            record.model != nullptr &&
            record.meshIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
            record.primitiveIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
            record.meshIndex < record.model->meshes.size() &&
            record.primitiveIndex <
                record.model->meshes[record.meshIndex].primitives.size();
    }

    GpuSceneSurfaceRecord BuildGpuSceneSurfaceRecord(
        const RUNTIME::SceneSurfaceInstance& surfaceInstance,
        uint32_t sourceSurfaceInstanceIndex) {

        GpuSceneSurfaceRecord record{};
        record.objectId = surfaceInstance.objectId;
        record.objectVersion = surfaceInstance.objectVersion;
        record.sourceSurfaceInstanceIndex = sourceSurfaceInstanceIndex;
        record.sourceSurface = &surfaceInstance;
        record.model = surfaceInstance.model;
        record.renderModel = surfaceInstance.renderModel;
        record.surface = surfaceInstance.surface;
        record.surfaceIndex = surfaceInstance.surfaceIndex;
        record.nodeIndex = surfaceInstance.nodeIndex;
        record.meshIndex = surfaceInstance.meshIndex;
        record.primitiveIndex = surfaceInstance.primitiveIndex;
        record.materialIndex = surfaceInstance.materialIndex;
        record.objectWorldTransform = surfaceInstance.objectWorldTransform;
        record.drawWorldMatrix = surfaceInstance.drawWorldMatrix;
        record.hasDrawWorldMatrix = surfaceInstance.hasDrawWorldMatrix;
        record.worldBounds = surfaceInstance.worldBounds;
        record.visible = surfaceInstance.visible;
        record.isStatic = surfaceInstance.isStatic;
        record.hasRuntimeAnimation = surfaceInstance.hasRuntimeAnimation;
        record.animationPose = surfaceInstance.animationPose;
        record.animationPoseRevision =
            surfaceInstance.animationPoseRevision;
        record.hasSpecialRenderDebug = surfaceInstance.hasSpecialRenderDebug;
        record.skinned = surfaceInstance.skinned;
        record.castShadow = surfaceInstance.castShadow;
        record.receiveShadow = surfaceInstance.receiveShadow;
        record.clusteredGeometryPath = surfaceInstance.clusteredGeometryPath;
        record.materialOverride = surfaceInstance.materialOverride;
        record.materialOverrideRevision =
            surfaceInstance.materialOverrideRevision;
        record.materialFxProfileId = surfaceInstance.materialFxProfileId;
        record.postGroupMask = surfaceInstance.postGroupMask;
        record.materialFxValuesInitialized = surfaceInstance.materialFxValuesInitialized;
        for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
            record.materialFxParamValues[i] = surfaceInstance.materialFxParamValues[i];
        }

        record.forwardCandidate = record.visible;
        record.shadowCandidate = record.visible && record.castShadow;
        record.key = BuildResourceKey(record);

        const GpuSceneSurfaceValidationResult validation =
            ValidateGpuSceneSurfaceRecord(record);
        record.valid = validation.IsValid();
        if (!record.valid) {
            record.forwardCandidate = false;
            record.shadowCandidate = false;
            record.key.passMask = BuildPassMask(record);
            record.key = BuildResourceKey(record);
        }

        return record;
    }

    GpuSceneSurfaceValidationResult ValidateGpuSceneSurfaceRecord(
        const GpuSceneSurfaceRecord& record) {

        GpuSceneSurfaceValidationResult result{};
        if (record.sourceSurface == nullptr) {
            result.invalidSource = true;
        }
        if (record.model == nullptr || record.renderModel == nullptr || !record.renderModel->valid) {
            result.invalidModel = true;
        }
        if (record.surface == nullptr || !record.surface->HasMeshPrimitive()) {
            result.unsupportedGeometry = true;
        }
        if (!record.hasDrawWorldMatrix) {
            result.missingDrawMatrix = true;
        }
        if (!BOUNDS::IsUsable(record.worldBounds)) {
            result.invalidBounds = true;
        }
        if (!HasValidRenderSurfacePrimitive(
            record.model,
            record.surface != nullptr ? *record.surface : RUNTIME::RenderSurfaceRecord{})) {
            result.invalidPrimitiveIndex = true;
        }
        return result;
    }

    namespace {
        bool IsGpuSceneResidentRecordCommon(
            const GpuSceneSurfaceRecord& record) {

            return
                record.valid &&
                record.key.resourceKeyValid &&
                record.key.objectDataCompatible &&
                !record.hasSpecialRenderDebug &&
                record.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader &&
                (!record.skinned ||
                    (record.jointPaletteSlot != RUNTIME::kInvalidRenderSurfaceIndex &&
                        record.jointPaletteMatrixCount != 0u)) &&
                HasValidGpuSceneSubmitPrimitiveTarget(record);
        }
    }

    bool IsGpuSceneForwardOpaqueResidentRecord(
        const GpuSceneSurfaceRecord& record) {

        if (!record.forwardCandidate ||
            !IsGpuSceneResidentRecordCommon(record)) {
            return false;
        }

        return
            !record.key.depthAware &&
            !record.key.transparent;
    }

    bool IsGpuSceneForwardDepthAwareResidentRecord(
        const GpuSceneSurfaceRecord& record) {

        return
            record.forwardCandidate &&
            IsGpuSceneResidentRecordCommon(record) &&
            record.key.depthAware;
    }

    bool IsGpuSceneForwardTransparentResidentRecord(
        const GpuSceneSurfaceRecord& record) {

        return
            record.forwardCandidate &&
            IsGpuSceneResidentRecordCommon(record) &&
            !record.key.depthAware &&
            record.key.transparent;
    }

    bool IsGpuSceneShadowResidentRecord(
        const GpuSceneSurfaceRecord& record) {

        return
            record.shadowCandidate &&
            IsGpuSceneResidentRecordCommon(record) &&
            !record.key.depthAware &&
            !record.key.transparent;
    }

    RUNTIME::SurfaceGpuSceneMaterialSource BuildGpuSceneMaterialSource(
        const GpuSceneSurfaceRecord& record,
        const RUNTIME::SurfaceGpuSceneInstance& instance) {

        RUNTIME::SurfaceGpuSceneMaterialSource source{};
        source.model = record.model;
        source.materialOverride = record.materialOverride;
        source.sourceRecordIndex = instance.sourceRecordIndex;
        source.sourceSurfaceInstanceIndex = instance.sourceSurfaceInstanceIndex;
        source.materialIndex = record.materialIndex;
        source.materialKey = record.key.materialKey;
        source.materialResource = record.key.resources.material;
        source.materialRevision =
            record.materialOverrideRevision;
        source.world = instance.world;
        source.normalMatrix = instance.normalMatrix;
        source.receiveShadow = record.receiveShadow;
        source.fxFlags = instance.fxFlags;
        for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
            source.fxUser[i] = instance.fxUser[i];
        }
        return source;
    }

    RUNTIME::SurfaceGpuSceneBuildStats BuildGpuSceneInstanceList(
        const std::vector<GpuSceneSurfaceRecord>& records,
        const std::vector<uint32_t>& recordIndices,
        std::vector<RUNTIME::SurfaceGpuSceneInstance>& outInstances,
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>& outMaterialSources) {

        RUNTIME::SurfaceGpuSceneBuildStats stats{};
        outInstances.clear();
        outMaterialSources.clear();
        outInstances.reserve(recordIndices.size());
        outMaterialSources.reserve(recordIndices.size());

        for (const uint32_t recordIndex : recordIndices) {
            if (recordIndex >= records.size()) {
                ++stats.skippedInvalidRecordCount;
                continue;
            }

            const uint32_t localInstanceIndex =
                static_cast<uint32_t>(outInstances.size());
            const GpuSceneSurfaceRecord& record = records[recordIndex];
            RUNTIME::SurfaceGpuSceneInstance instance =
                BuildGpuSceneInstance(record, recordIndex);
            FillClusterGeometryData(record, instance, &stats);
            if (record.key.resources.HasPoolHandles()) {
                ++stats.resourceBackedInstanceCount;
            } else {
                ++stats.missingResourceHandleInstanceCount;
            }

            outInstances.push_back(instance);
            RUNTIME::SurfaceGpuSceneMaterialSource source =
                BuildGpuSceneMaterialSource(record, instance);
            source.localGpuSceneInstanceIndex = localInstanceIndex;
            outMaterialSources.push_back(source);
            ++stats.instanceCount;
            stats.maxCommandInstanceCount =
                (std::max)(stats.maxCommandInstanceCount, 1u);
        }

        stats.commandCount = NUMERIC::SaturateToUint32(outInstances.size());
        return stats;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
