#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/GpuDriven/HIKARI_GpuScenePoseBuilder.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "Core/Numeric/HIKARI_IntegerConversion.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        void ResetPassSource(
            GpuDrivenPassSource& pass,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
            const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources,
            uint32_t gpuSceneBaseIndex,
            GpuDrivenBackendKind backend,
            bool clusterEligible) {

            pass.traditionalIndirect.Reset();
            pass.instances = instances;
            pass.materialSources = materialSources;
            pass.gpuSceneBaseIndex = gpuSceneBaseIndex;
            pass.gpuSceneInstanceCount =
                instances != nullptr
                    ? NUMERIC::SaturateToUint32(instances->size())
                    : 0u;
            pass.preferredBackend = backend;
            pass.clusterEligible = clusterEligible;
            pass.dirtyRanges.clear();
        }

        uint64_t HashAppend(uint64_t seed, uint64_t value) {
            constexpr uint64_t kMul = 1099511628211ull;
            seed ^= value;
            seed *= kMul;
            return seed;
        }

        uint64_t BuildSourceLayoutVersion(uint64_t layoutVersion, uint64_t routingVersion) {
            uint64_t value = 1469598103934665603ull;
            value = HashAppend(value, layoutVersion);
            value = HashAppend(value, routingVersion);
            return value;
        }

        uint32_t PassInstanceFlag(GpuDrivenPassKind passKind) {
            using RUNTIME::SurfaceGpuSceneInstanceFlags;
            switch (passKind) {
            case GpuDrivenPassKind::ForwardOpaque:
                return static_cast<uint32_t>(
                    SurfaceGpuSceneInstanceFlags::PassForwardOpaque);
            case GpuDrivenPassKind::DepthPrepass:
                return static_cast<uint32_t>(
                    SurfaceGpuSceneInstanceFlags::PassDepthPrepass);
            case GpuDrivenPassKind::ForwardDepthAware:
                return static_cast<uint32_t>(
                    SurfaceGpuSceneInstanceFlags::PassForwardDepthAware);
            case GpuDrivenPassKind::ForwardTransparent:
                return static_cast<uint32_t>(
                    SurfaceGpuSceneInstanceFlags::PassForwardTransparent);
            case GpuDrivenPassKind::Shadow:
                return static_cast<uint32_t>(
                    SurfaceGpuSceneInstanceFlags::PassShadow);
            default:
                return 0u;
            }
        }

        float ComputeWorldBoundsRadius(const Bounds& bounds) {
            const float ex = (bounds.max.x - bounds.min.x) * 0.5f;
            const float ey = (bounds.max.y - bounds.min.y) * 0.5f;
            const float ez = (bounds.max.z - bounds.min.z) * 0.5f;
            return std::sqrt(ex * ex + ey * ey + ez * ez);
        }

        float ComputeWorldBoundsMainAreaProxy(const Bounds& bounds) {
            float sx = (std::max)(bounds.max.x - bounds.min.x, 0.0f);
            float sy = (std::max)(bounds.max.y - bounds.min.y, 0.0f);
            float sz = (std::max)(bounds.max.z - bounds.min.z, 0.0f);
            if (sx < sy) {
                std::swap(sx, sy);
            }
            if (sy < sz) {
                std::swap(sy, sz);
            }
            if (sx < sy) {
                std::swap(sx, sy);
            }
            return sx * sy;
        }

        float ComputeDepthOccluderScore(const Bounds& bounds, float radius) {
            const float mainArea = ComputeWorldBoundsMainAreaProxy(bounds);
            return std::sqrt((std::max)(mainArea, 0.0f)) + radius * 0.15f;
        }

        constexpr float kDepthVisibilityMinOccluderRadius = 0.35f;
        constexpr size_t kDepthVisibilityMaxOccluderRecords = 8192;
        constexpr float kShadowCasterMinRadius = 0.28f;
        constexpr float kShadowCasterMinMainArea = 0.06f;
        constexpr float kShadowAlphaMaskMinRadius = 1.10f;
        constexpr float kShadowAlphaMaskMinMainArea = 0.35f;
        constexpr float kShadowDoubleSidedMinRadius = 0.85f;
        constexpr float kShadowDoubleSidedMinMainArea = 0.20f;

        struct DepthPrepassOccluderCandidate {
            uint32_t recordIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            float radius = 0.0f;
            float score = 0.0f;
        };

        // Depth prepass は solid な不透明 mainline をカバーして forward の
        // 過描画を消す。alpha mask は prepass 側でテクスチャ sample + clip の
        // PS が必要になり prepass 自体が高くつくため除外する (forward の
        // alpha mask も prepass が書いた solid 深度に対して early-Z が効く)。
        // 両面は DoubleSided bucket + null PS で安価なので含める。
        bool IsDepthPrepassSafeMaterial(const GpuSceneSurfaceRecord& record) {
            return
                !record.key.alphaMasked &&
                !record.key.transparent &&
                !record.key.depthAware &&
                !record.key.materialFx &&
                !record.key.waterMaterialFx;
        }

        // BuildDepthPrepassOccluderRecords の収集規則と dirty patch の
        // membership 判定は必ずこの述語を共有する。
        bool IsDepthPrepassCoverageRecord(const GpuSceneSurfaceRecord& record) {
            if (!IsGpuSceneForwardOpaqueResidentRecord(record) ||
                !IsDepthPrepassSafeMaterial(record)) {
                return false;
            }
            return ComputeWorldBoundsRadius(record.worldBounds) >=
                kDepthVisibilityMinOccluderRadius;
        }

        bool HasShadowCasterSafeMaterial(const GpuSceneSurfaceRecord& record) {
            const bool staticMaterialFx =
                record.key.materialFx &&
                !record.key.waterMaterialFx &&
                !record.key.materialFxUsesCustomVertexShader;
            return
                !record.key.transparent &&
                !record.key.depthAware &&
                !record.key.waterMaterialFx &&
                (!record.key.materialFx || staticMaterialFx);
        }

        bool IsGpuSceneStaticShadowCasterRecord(
            const GpuSceneSurfaceRecord& record) {

            if (!IsGpuSceneShadowResidentRecord(record) ||
                !HasShadowCasterSafeMaterial(record)) {
                return false;
            }

            const float radius = ComputeWorldBoundsRadius(record.worldBounds);
            const float mainArea = ComputeWorldBoundsMainAreaProxy(record.worldBounds);
            if (radius < kShadowCasterMinRadius) {
                return false;
            }
            if (mainArea < kShadowCasterMinMainArea) {
                return false;
            }
            if (record.key.alphaMasked &&
                (radius < kShadowAlphaMaskMinRadius ||
                    mainArea < kShadowAlphaMaskMinMainArea)) {
                return false;
            }
            if (record.key.doubleSided &&
                (radius < kShadowDoubleSidedMinRadius ||
                    mainArea < kShadowDoubleSidedMinMainArea)) {
                return false;
            }
            return true;
        }

        void BuildDepthPrepassOccluderRecords(
            const std::vector<GpuSceneSurfaceRecord>& records,
            const std::vector<uint32_t>& opaqueRecords,
            std::vector<uint32_t>& outOccluders,
            GpuSceneRegistryStats& stats) {

            outOccluders.clear();
            stats.depthPrepassOccluderRecordCount = 0;
            stats.depthPrepassRejectedSmallRecordCount = 0;
            stats.depthPrepassRejectedUnsafeMaterialRecordCount = 0;
            stats.depthPrepassBudgetClippedRecordCount = 0;

            std::vector<DepthPrepassOccluderCandidate> candidates;
            candidates.reserve((std::min)(
                opaqueRecords.size(),
                kDepthVisibilityMaxOccluderRecords * 2u));
            float largestFallbackRadius = -1.0f;
            uint32_t largestFallbackRecord =
                RUNTIME::kInvalidRenderSurfaceIndex;

            for (const uint32_t recordIndex : opaqueRecords) {
                if (recordIndex >= records.size()) {
                    continue;
                }

                const GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsGpuSceneForwardOpaqueResidentRecord(record)) {
                    continue;
                }
                if (!IsDepthPrepassSafeMaterial(record)) {
                    ++stats.depthPrepassRejectedUnsafeMaterialRecordCount;
                    continue;
                }

                const float radius = ComputeWorldBoundsRadius(record.worldBounds);
                if (radius < kDepthVisibilityMinOccluderRadius) {
                    ++stats.depthPrepassRejectedSmallRecordCount;
                    if (radius > largestFallbackRadius) {
                        largestFallbackRadius = radius;
                        largestFallbackRecord = recordIndex;
                    }
                    continue;
                }

                candidates.push_back({
                    recordIndex,
                    radius,
                    ComputeDepthOccluderScore(record.worldBounds, radius)
                });
            }

            if (candidates.size() > kDepthVisibilityMaxOccluderRecords) {
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const DepthPrepassOccluderCandidate& a,
                       const DepthPrepassOccluderCandidate& b) {
                        if (a.score != b.score) {
                            return a.score > b.score;
                        }
                        if (a.radius != b.radius) {
                            return a.radius > b.radius;
                        }
                        return a.recordIndex < b.recordIndex;
                    });
                stats.depthPrepassBudgetClippedRecordCount =
                    NUMERIC::SaturateToUint32(
                        candidates.size() - kDepthVisibilityMaxOccluderRecords);
                candidates.resize(kDepthVisibilityMaxOccluderRecords);
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const DepthPrepassOccluderCandidate& a,
                       const DepthPrepassOccluderCandidate& b) {
                        return a.recordIndex < b.recordIndex;
                    });
            }

            outOccluders.reserve(candidates.size());
            for (const DepthPrepassOccluderCandidate& candidate : candidates) {
                outOccluders.push_back(candidate.recordIndex);
            }

            if (outOccluders.empty() &&
                largestFallbackRecord != RUNTIME::kInvalidRenderSurfaceIndex) {
                outOccluders.push_back(largestFallbackRecord);
            }
            stats.depthPrepassOccluderRecordCount =
                NUMERIC::SaturateToUint32(outOccluders.size());
        }

        void AppendDirtyRange(
            std::vector<GpuSceneDirtyRange>& ranges,
            uint32_t firstInstance,
            uint32_t instanceCount) {

            if (instanceCount == 0u) {
                return;
            }
            if (!ranges.empty()) {
                GpuSceneDirtyRange& last = ranges.back();
                if (last.firstInstance + last.instanceCount == firstInstance) {
                    last.instanceCount += instanceCount;
                    return;
                }
            }

            ranges.push_back({ firstInstance, instanceCount });
        }

        enum class StaticBatchRunKeyKind {
            Pso,
            Material,
            TextureSet,
            Geometry,
            MeshResource,
            MaterialResource,
            ClusterResource,
        };

        uint64_t PackResourceHandle(uint32_t index, uint32_t generation) {
            return
                (static_cast<uint64_t>(generation) << 32) |
                static_cast<uint64_t>(index);
        }

        uint64_t SelectStaticBatchRunKey(
            const GpuSceneSurfaceRecord& record,
            StaticBatchRunKeyKind kind) {

            const RUNTIME::SurfaceResourceIds& resources = record.key.resources;
            switch (kind) {
            case StaticBatchRunKeyKind::Pso:
                return record.key.psoKey;
            case StaticBatchRunKeyKind::Material:
                return record.key.materialKey;
            case StaticBatchRunKeyKind::TextureSet:
                return record.key.textureSetKey;
            case StaticBatchRunKeyKind::Geometry:
                return record.key.geometryKey;
            case StaticBatchRunKeyKind::MeshResource:
                return PackResourceHandle(
                    resources.mesh.index,
                    resources.mesh.generation);
            case StaticBatchRunKeyKind::MaterialResource:
                return PackResourceHandle(
                    resources.material.index,
                    resources.material.generation);
            case StaticBatchRunKeyKind::ClusterResource:
                return PackResourceHandle(
                    resources.clusterGeometry.index,
                    resources.clusterGeometry.generation);
            default:
                return 0;
            }
        }

        uint32_t CountStaticBatchRuns(
            const std::vector<GpuSceneSurfaceRecord>& records,
            const std::vector<uint32_t>& recordIndices,
            StaticBatchRunKeyKind kind) {

            bool hasPrevious = false;
            uint64_t previous = 0;
            uint32_t runs = 0;
            for (uint32_t recordIndex : recordIndices) {
                if (recordIndex >= records.size()) {
                    continue;
                }
                const GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!record.valid || !record.key.resourceKeyValid) {
                    continue;
                }

                const uint64_t current = SelectStaticBatchRunKey(record, kind);
                if (!hasPrevious || current != previous) {
                    ++runs;
                    previous = current;
                    hasPrevious = true;
                }
            }
            return runs;
        }

        bool ComesBeforeForStaticGpuSceneBatching(
            const GpuSceneSurfaceRecord& lhs,
            const GpuSceneSurfaceRecord& rhs) {

            if (lhs.key.psoKey != rhs.key.psoKey) {
                return lhs.key.psoKey < rhs.key.psoKey;
            }
            if (lhs.key.materialKey != rhs.key.materialKey) {
                return lhs.key.materialKey < rhs.key.materialKey;
            }
            if (lhs.key.textureSetKey != rhs.key.textureSetKey) {
                return lhs.key.textureSetKey < rhs.key.textureSetKey;
            }
            if (lhs.key.geometryBackend != rhs.key.geometryBackend) {
                return lhs.key.geometryBackend < rhs.key.geometryBackend;
            }
            if (lhs.key.geometryKey != rhs.key.geometryKey) {
                return lhs.key.geometryKey < rhs.key.geometryKey;
            }
            const RUNTIME::SurfaceResourceIds& lhsResources = lhs.key.resources;
            const RUNTIME::SurfaceResourceIds& rhsResources = rhs.key.resources;
            const uint64_t lhsMesh =
                PackResourceHandle(lhsResources.mesh.index, lhsResources.mesh.generation);
            const uint64_t rhsMesh =
                PackResourceHandle(rhsResources.mesh.index, rhsResources.mesh.generation);
            if (lhsMesh != rhsMesh) {
                return lhsMesh < rhsMesh;
            }
            const uint64_t lhsMaterial =
                PackResourceHandle(
                    lhsResources.material.index,
                    lhsResources.material.generation);
            const uint64_t rhsMaterial =
                PackResourceHandle(
                    rhsResources.material.index,
                    rhsResources.material.generation);
            if (lhsMaterial != rhsMaterial) {
                return lhsMaterial < rhsMaterial;
            }
            const uint64_t lhsCluster =
                PackResourceHandle(
                    lhsResources.clusterGeometry.index,
                    lhsResources.clusterGeometry.generation);
            const uint64_t rhsCluster =
                PackResourceHandle(
                    rhsResources.clusterGeometry.index,
                    rhsResources.clusterGeometry.generation);
            if (lhsCluster != rhsCluster) {
                return lhsCluster < rhsCluster;
            }
            if (lhs.key.sortKey != rhs.key.sortKey) {
                return lhs.key.sortKey < rhs.key.sortKey;
            }
            return lhs.sourceSurfaceInstanceIndex < rhs.sourceSurfaceInstanceIndex;
        }

        bool HasSameStaticGpuSceneBatchIdentity(
            const GpuSceneSurfaceRecord& lhs,
            const GpuSceneSurfaceRecord& rhs) {

            const RUNTIME::SurfaceResourceIds& lhsResources = lhs.key.resources;
            const RUNTIME::SurfaceResourceIds& rhsResources = rhs.key.resources;
            return
                lhs.key.sortKey == rhs.key.sortKey &&
                lhs.key.psoKey == rhs.key.psoKey &&
                lhs.key.materialKey == rhs.key.materialKey &&
                lhs.key.textureSetKey == rhs.key.textureSetKey &&
                lhs.key.geometryBackend == rhs.key.geometryBackend &&
                lhs.key.geometryKey == rhs.key.geometryKey &&
                lhsResources.mesh == rhsResources.mesh &&
                lhsResources.material == rhsResources.material &&
                lhsResources.clusterGeometry == rhsResources.clusterGeometry;
        }

        GpuSceneStaticBatchStats SortStaticResidentRecordsForBatching(
            const std::vector<GpuSceneSurfaceRecord>& records,
            std::vector<uint32_t>& recordIndices,
            bool allowReorder) {

            GpuSceneStaticBatchStats stats{};
            stats.recordCount = NUMERIC::SaturateToUint32(recordIndices.size());
            for (uint32_t recordIndex : recordIndices) {
                if (recordIndex >= records.size()) {
                    continue;
                }
                if (records[recordIndex].key.resources.HasPoolHandles()) {
                    ++stats.resourceBackedRecordCount;
                } else {
                    ++stats.missingResourceHandleRecordCount;
                }
            }

            stats.rawPsoRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Pso);
            stats.rawMaterialRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Material);
            stats.rawTextureSetRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::TextureSet);
            stats.rawGeometryRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Geometry);
            stats.rawMeshResourceRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::MeshResource);
            stats.rawMaterialResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::MaterialResource);
            stats.rawClusterResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::ClusterResource);

            const std::vector<uint32_t> originalOrder = recordIndices;
            if (allowReorder) {
                std::stable_sort(
                    recordIndices.begin(),
                    recordIndices.end(),
                    [&](uint32_t lhsIndex, uint32_t rhsIndex) {
                        if (lhsIndex >= records.size() || rhsIndex >= records.size()) {
                            return lhsIndex < rhsIndex;
                        }
                        const GpuSceneSurfaceRecord& lhs = records[lhsIndex];
                        const GpuSceneSurfaceRecord& rhs = records[rhsIndex];
                        if (ComesBeforeForStaticGpuSceneBatching(lhs, rhs)) {
                            return true;
                        }
                        if (ComesBeforeForStaticGpuSceneBatching(rhs, lhs)) {
                            return false;
                        }
                        return lhsIndex < rhsIndex;
                    });
                stats.sortApplied = true;
            }

            const size_t compareCount =
                (std::min)(originalOrder.size(), recordIndices.size());
            for (size_t i = 0; i < compareCount; ++i) {
                if (originalOrder[i] != recordIndices[i]) {
                    ++stats.reorderedRecordCount;
                }
            }

            stats.sortedPsoRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Pso);
            stats.sortedMaterialRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Material);
            stats.sortedTextureSetRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::TextureSet);
            stats.sortedGeometryRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Geometry);
            stats.sortedMeshResourceRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::MeshResource);
            stats.sortedMaterialResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::MaterialResource);
            stats.sortedClusterResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::ClusterResource);
            return stats;
        }

        RUNTIME::SurfaceDrawIndexedArgs BuildDrawIndexedArgs(
            const GpuSceneSurfaceRecord& record) {

            RUNTIME::SurfaceDrawIndexedArgs args{};
            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                return args;
            }
            const MeshAsset& mesh = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= mesh.primitives.size()) {
                return args;
            }
            args.indexCountPerInstance =
                NUMERIC::SaturateToUint32(mesh.primitives[record.primitiveIndex].indices.size());
            args.instanceCount = 1u;
            return args;
        }

        std::string ResolveTraditionalMaterialFxPixelShaderId(
            std::string pixelShaderId) {

            if (pixelShaderId == "Render3D_FxWaterPS") {
                return "Render3D_FxWaterPS";
            }
            if (pixelShaderId == "Render3D_StaticFxPS" ||
                pixelShaderId == "StaticFx" ||
                pixelShaderId == "MaterialFx") {
                return "Render3D_StaticFxPS";
            }
            return pixelShaderId;
        }

        VFX::VariantKey BuildTraditionalVariantKey(
            const GpuSceneSurfaceRecord& record,
            RUNTIME::SurfaceDrawCommandPass pass) {

            VFX::VariantKey key{};
            key.shaderId = "StaticLit";
            key.pixelShaderId = "StaticLit";
            key.composite = VFX::CompositeMode::Replace;
            key.depthTest = true;
            key.depthWrite =
                pass != RUNTIME::SurfaceDrawCommandPass::DepthAware &&
                !record.key.transparent;
            key.doubleSided = record.key.doubleSided;

            if (record.key.materialFx) {
                MaterialFxProfile profile{};
                if (MaterialFxProfile::LoadById(record.materialFxProfileId, profile)) {
                    key.shaderId =
                        profile.shaderProfileId.empty()
                            ? "StaticFx"
                            : profile.shaderProfileId;
                    key.vertexShaderId = profile.vertexShaderId;
                    const std::string pixelShaderId =
                        !profile.pixelShaderId.empty()
                            ? profile.pixelShaderId
                            : key.shaderId;
                    key.pixelShaderId =
                        ResolveTraditionalMaterialFxPixelShaderId(pixelShaderId);
                    key.featureBits = profile.featureBits;
                    key.composite = profile.composite;
                    key.depthTest = profile.depthTest;
                    key.depthWrite = profile.depthWrite;
                    key.doubleSided =
                        record.key.doubleSided || profile.doubleSided;
                } else {
                    key.shaderId = "StaticFx";
                    key.pixelShaderId = "Render3D_StaticFxPS";
                }
            }

            if (record.key.transparent ||
                pass == RUNTIME::SurfaceDrawCommandPass::DepthAware) {
                key.depthWrite = false;
            }
            return key;
        }

        bool AssignTraditionalBucket(
            GpuSceneRegistry::TraditionalIndirectStream& stream,
            const VFX::VariantKey& variant,
            RUNTIME::SurfaceDrawCommand& command) {

            for (size_t i = 0; i < stream.bucketVariants.size(); ++i) {
                if (stream.bucketVariants[i] == variant) {
                    command.traditionalVariant = variant;
                    command.traditionalBucketIndex = NUMERIC::SaturateToUint32(i);
                    return true;
                }
            }

            if (stream.bucketVariants.size() >=
                kGpuTraditionalCommandBucketCount) {
                return false;
            }

            command.traditionalVariant = variant;
            command.traditionalBucketIndex =
                NUMERIC::SaturateToUint32(stream.bucketVariants.size());
            stream.bucketVariants.push_back(variant);
            return true;
        }

        bool IsValidDrawArgs(const RUNTIME::SurfaceDrawIndexedArgs& args) {
            return args.indexCountPerInstance > 0u && args.instanceCount > 0u;
        }

        bool ShouldPublishStaticTraditionalStreams() {
            return HIKARI::RENDER3D::GetRenderQualitySettings().geometryPipeline ==
                HIKARI::RENDER3D::GeometryPipelineMode::TraditionalVsPs;
        }

        void FillTraditionalDrawCommand(
            const GpuSceneSurfaceRecord& record,
            uint32_t recordIndexInStream,
            uint32_t executableIndex,
            uint32_t gpuSceneInstanceIndex,
            RUNTIME::SurfaceDrawCommandPass pass,
            bool clusterMainlineEligible,
            RUNTIME::SurfaceDrawCommand& command) {

            command = {};
            command.pass = pass;
            command.firstExecutableIndex = executableIndex;
            command.recordCount = 1u;
            command.firstRecordIndex = recordIndexInStream;
            command.firstGpuSceneInstanceIndex = gpuSceneInstanceIndex;
            command.gpuSceneInstanceCount = 1u;
            command.batchKey = RUNTIME::BuildSurfaceDrawBatchKey(
                pass,
                record.key);
            command.resources = record.key.resources;
            command.psoKey = record.key.psoKey;
            command.geometryKey = record.key.geometryKey;
            command.geometryBackend = record.key.geometryBackend;
            command.backendRoute = record.key.backendRoute;
            command.materialKey = record.key.materialKey;
            command.textureSetKey = record.key.textureSetKey;
            command.modelKey = record.key.modelKey;
            command.singleRecord = true;
            command.transparent = record.key.transparent;
            command.alphaMasked = record.key.alphaMasked;
            command.doubleSided = record.key.doubleSided;
            command.materialFx = record.key.materialFx;
            command.waterMaterialFx = record.key.waterMaterialFx;
            command.materialFxUsesCustomVertexShader =
                record.key.materialFxUsesCustomVertexShader;
            command.traditionalVariant =
                BuildTraditionalVariantKey(record, pass);
            command.clusterMainlineEligible = clusterMainlineEligible;
            command.drawArgs = BuildDrawIndexedArgs(record);
            command.drawArgsValid = IsValidDrawArgs(command.drawArgs);
        }

        bool AppendTraditionalRecordInstance(
            const std::vector<GpuSceneSurfaceRecord>& records,
            uint32_t sourceRecordIndex,
            GpuSceneRegistry::TraditionalIndirectStream& stream) {

            std::vector<uint32_t> singleRecordIndex{ sourceRecordIndex };
            std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
            std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};
            (void)BuildGpuSceneInstanceList(
                records,
                singleRecordIndex,
                singleInstance,
                singleMaterial);
            if (singleInstance.empty() || singleMaterial.empty()) {
                return false;
            }

            const uint32_t streamInstanceIndex =
                NUMERIC::SaturateToUint32(stream.instances.size());
            singleMaterial.front().localGpuSceneInstanceIndex = streamInstanceIndex;
            // Material residency is indexed by the global scene record table.
            // The command stream has its own local record index; mixing the two
            // makes pass-local records overwrite unrelated material bindings.
            singleInstance.front().sourceRecordIndex = sourceRecordIndex;
            singleMaterial.front().sourceRecordIndex = sourceRecordIndex;
            singleMaterial.front().sourceSurfaceInstanceIndex =
                singleInstance.front().sourceSurfaceInstanceIndex;
            stream.instances.push_back(singleInstance.front());
            stream.materialSources.push_back(singleMaterial.front());
            return true;
        }

        void AppendStaticTraditionalRecord(
            const std::vector<GpuSceneSurfaceRecord>& records,
            uint32_t recordIndex,
            RUNTIME::SurfaceDrawCommandPass pass,
            GpuSceneRegistry::TraditionalIndirectStream& stream) {

            if (recordIndex >= records.size()) {
                return;
            }
            const GpuSceneSurfaceRecord& record = records[recordIndex];
            if (!HasValidGpuSceneSubmitPrimitiveTarget(record)) {
                return;
            }

            const uint32_t recordIndexInStream = NUMERIC::SaturateToUint32(stream.records.size());
            const uint32_t executableIndex =
                NUMERIC::SaturateToUint32(stream.executableRecordIndices.size());
            const uint32_t gpuSceneInstanceIndex =
                NUMERIC::SaturateToUint32(stream.instances.size());

            stream.records.push_back(record);
            stream.executableRecordIndices.push_back(recordIndexInStream);
            stream.jointPalettes.emplace_back();
            if (!AppendTraditionalRecordInstance(
                records,
                recordIndex,
                stream)) {
                stream.records.pop_back();
                stream.executableRecordIndices.pop_back();
                stream.jointPalettes.pop_back();
                return;
            }

            RUNTIME::SurfaceDrawCommand command{};
            FillTraditionalDrawCommand(
                record,
                recordIndexInStream,
                executableIndex,
                gpuSceneInstanceIndex,
                pass,
                record.key.clusterMainlineEligible,
                command);
            if (!AssignTraditionalBucket(
                stream,
                command.traditionalVariant,
                command)) {
                stream.records.pop_back();
                stream.executableRecordIndices.pop_back();
                stream.jointPalettes.pop_back();
                stream.instances.pop_back();
                stream.materialSources.pop_back();
                return;
            }
            stream.commands.push_back(command);
            ++stream.staticCommandCount;
        }

        void AppendSkinnedTraditionalRecord(
            const std::vector<GpuSceneSurfaceRecord>& records,
            uint32_t recordIndex,
            RUNTIME::SurfaceDrawCommandPass pass,
            GpuSceneRegistry::TraditionalIndirectStream& stream,
            GpuScenePoseBuilder& poseBuilder) {

            if (recordIndex >= records.size()) {
                return;
            }
            const GpuSceneSurfaceRecord& record = records[recordIndex];
            if (record.key.materialFxUsesCustomVertexShader) {
                return;
            }
            const std::vector<MATH::Mat4>* palette =
                poseBuilder.ResolveJointPalette(record);
            if (palette == nullptr || palette->empty()) {
                return;
            }

            const uint32_t recordIndexInStream = NUMERIC::SaturateToUint32(stream.records.size());
            const uint32_t executableIndex =
                NUMERIC::SaturateToUint32(stream.executableRecordIndices.size());
            const uint32_t gpuSceneInstanceIndex =
                NUMERIC::SaturateToUint32(stream.instances.size());

            stream.records.push_back(record);
            stream.executableRecordIndices.push_back(recordIndexInStream);
            stream.jointPalettes.push_back(*palette);
            if (!AppendTraditionalRecordInstance(
                records,
                recordIndex,
                stream)) {
                stream.records.pop_back();
                stream.executableRecordIndices.pop_back();
                stream.jointPalettes.pop_back();
                return;
            }

            RUNTIME::SurfaceDrawCommand command{};
            FillTraditionalDrawCommand(
                record,
                recordIndexInStream,
                executableIndex,
                gpuSceneInstanceIndex,
                pass,
                false,
                command);
            if (!AssignTraditionalBucket(
                stream,
                command.traditionalVariant,
                command)) {
                stream.records.pop_back();
                stream.executableRecordIndices.pop_back();
                stream.jointPalettes.pop_back();
                stream.instances.pop_back();
                stream.materialSources.pop_back();
                return;
            }
            stream.commands.push_back(command);
            ++stream.skinnedCommandCount;
        }

        void AttachTraditionalIndirectView(
            GpuDrivenPassSource& pass,
            const GpuSceneRegistry::TraditionalIndirectStream& stream,
            uint32_t gpuSceneBaseIndex) {

            pass.traditionalIndirect.Reset();
            if (!stream.HasCommands()) {
                return;
            }
            pass.traditionalIndirect.records = &stream.records;
            pass.traditionalIndirect.executableRecordIndices =
                &stream.executableRecordIndices;
            pass.traditionalIndirect.commands = &stream.commands;
            pass.traditionalIndirect.instances = &stream.instances;
            pass.traditionalIndirect.materialSources = &stream.materialSources;
            pass.traditionalIndirect.jointPalettes = &stream.jointPalettes;
            pass.traditionalIndirect.bucketVariants = &stream.bucketVariants;
            pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
            pass.traditionalIndirect.gpuSceneInstanceCount =
                NUMERIC::SaturateToUint32(stream.instances.size());
            pass.traditionalIndirect.staticCommandCount =
                stream.staticCommandCount;
            pass.traditionalIndirect.skinnedCommandCount =
                stream.skinnedCommandCount;
        }

        bool IsGpuSceneSkinnedTraditionalRecordCommon(
            const GpuSceneSurfaceRecord& record) {

            // Skinned surfaces are owned by the mesh-shader deformation path.
            // Missing cooked geometry/palette is a blocked contract, not a hidden VS/PS fallback.
            (void)record;
            return false;
        }

        bool IsGpuSceneForwardSkinnedTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            return
                record.forwardCandidate &&
                IsGpuSceneSkinnedTraditionalRecordCommon(record);
        }

        bool IsGpuSceneShadowSkinnedTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            return
                record.shadowCandidate &&
                IsGpuSceneSkinnedTraditionalRecordCommon(record) &&
                !record.key.transparent;
        }

        bool IsGpuSceneStaticTraditionalRecordCommon(
            const GpuSceneSurfaceRecord& record) {

            return
                record.valid &&
                record.key.resourceKeyValid &&
                !record.skinned &&
                !record.key.materialFx &&
                !record.hasSpecialRenderDebug &&
                HasValidGpuSceneSubmitPrimitiveTarget(record);
        }

        bool IsGpuSceneForwardStaticTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            if (!record.forwardCandidate ||
                !IsGpuSceneStaticTraditionalRecordCommon(record)) {
                return false;
            }

            return record.key.backendRoute == RUNTIME::SurfaceBackendRoute::StaticVsPs;
        }

        bool IsGpuSceneShadowStaticTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            if (!record.shadowCandidate ||
                !IsGpuSceneStaticTraditionalRecordCommon(record) ||
                !HasShadowCasterSafeMaterial(record)) {
                return false;
            }
            if (record.key.backendRoute != RUNTIME::SurfaceBackendRoute::StaticVsPs) {
                return false;
            }

            const float radius = ComputeWorldBoundsRadius(record.worldBounds);
            const float mainArea = ComputeWorldBoundsMainAreaProxy(record.worldBounds);
            if (radius < kShadowCasterMinRadius ||
                mainArea < kShadowCasterMinMainArea) {
                return false;
            }
            if (record.key.alphaMasked &&
                (radius < kShadowAlphaMaskMinRadius ||
                    mainArea < kShadowAlphaMaskMinMainArea)) {
                return false;
            }
            if (record.key.doubleSided &&
                (radius < kShadowDoubleSidedMinRadius ||
                    mainArea < kShadowDoubleSidedMinMainArea)) {
                return false;
            }
            return true;
        }
    }

    void GpuSceneRegistry::TraditionalIndirectStream::Clear() {
        records.clear();
        executableRecordIndices.clear();
        commands.clear();
        instances.clear();
        materialSources.clear();
        jointPalettes.clear();
        bucketVariants.clear();
        staticCommandCount = 0;
        skinnedCommandCount = 0;
    }

    bool GpuSceneRegistry::TraditionalIndirectStream::HasCommands() const {
        return
            !records.empty() &&
            !commands.empty() &&
            !executableRecordIndices.empty() &&
            !bucketVariants.empty() &&
            records.size() == instances.size() &&
            records.size() == materialSources.size() &&
            records.size() == jointPalettes.size();
    }

    void GpuSceneRegistry::Clear() {
        surfaceRecords_.clear();
        meshShaderJointPalettes_.clear();
        forwardOpaqueResidentRecordIndices_.clear();
        depthPrepassOccluderRecordIndices_.clear();
        forwardDepthAwareResidentRecordIndices_.clear();
        forwardTransparentResidentRecordIndices_.clear();
        shadowResidentRecordIndices_.clear();
        forwardOpaqueSkinnedRecordIndices_.clear();
        forwardDepthAwareSkinnedRecordIndices_.clear();
        forwardTransparentSkinnedRecordIndices_.clear();
        shadowSkinnedRecordIndices_.clear();
        forwardOpaqueStaticTraditionalRecordIndices_.clear();
        forwardDepthAwareStaticTraditionalRecordIndices_.clear();
        forwardTransparentStaticTraditionalRecordIndices_.clear();
        shadowStaticTraditionalRecordIndices_.clear();
        forwardOpaqueGpuSceneIndexByRecord_.clear();
        depthPrepassGpuSceneIndexByRecord_.clear();
        forwardDepthAwareGpuSceneIndexByRecord_.clear();
        forwardTransparentGpuSceneIndexByRecord_.clear();
        shadowGpuSceneIndexByRecord_.clear();
        globalGpuSceneIndexByRecord_.clear();
        globalGpuSceneInstances_.clear();
        globalMaterialSources_.clear();
        forwardOpaqueGpuSceneInstances_.clear();
        forwardOpaqueMaterialSources_.clear();
        depthPrepassGpuSceneInstances_.clear();
        depthPrepassMaterialSources_.clear();
        forwardDepthAwareGpuSceneInstances_.clear();
        forwardDepthAwareMaterialSources_.clear();
        forwardTransparentGpuSceneInstances_.clear();
        forwardTransparentMaterialSources_.clear();
        shadowGpuSceneInstances_.clear();
        shadowMaterialSources_.clear();
        forwardOpaqueTraditionalStream_.Clear();
        forwardDepthAwareTraditionalStream_.Clear();
        forwardTransparentTraditionalStream_.Clear();
        shadowTraditionalStream_.Clear();
        sceneSource_.Reset();
        stats_ = {};
        layoutVersion_ = 0;
        routingVersion_ = 0;
        dataVersion_ = 0;
        sourceSurfaceCount_ = 0;
        publishStaticTraditionalStreams_ = false;
    }

    void GpuSceneRegistry::SyncForwardFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        if (input.sceneCache == nullptr) {
            Clear();
            return;
        }

        const uint64_t layoutVersion = input.sceneCache->GetSurfaceVersion();
        const uint64_t routingVersion = input.sceneCache->GetSurfaceRoutingVersion();
        const uint64_t dataVersion = input.sceneCache->GetSurfaceDataVersion();
        const uint32_t surfaceCount =
            NUMERIC::SaturateToUint32(input.sceneCache->GetSurfaceInstances().size());

        const bool layoutChanged =
            layoutVersion_ != layoutVersion ||
            routingVersion_ != routingVersion ||
            sourceSurfaceCount_ != surfaceCount;
        const bool publishStaticTraditionalStreams =
            ShouldPublishStaticTraditionalStreams();
        const bool traditionalStreamPolicyChanged =
            publishStaticTraditionalStreams_ != publishStaticTraditionalStreams;
        if (layoutChanged ||
            traditionalStreamPolicyChanged ||
            !sceneSource_.HasAnyGpuSceneRanges()) {
            RebuildForwardFromSceneCache(input);
            return;
        }

        ClearFrameDirtyRanges();
        if (dataVersion_ == dataVersion) {
            sceneSource_.dirtyBaseSourceVersion = sceneSource_.sourceVersion;
            return;
        }

        const uint64_t patchBaseSourceVersion = dataVersion_;
        if (!TryPatchForwardDataFromSceneCache(input)) {
            RebuildForwardFromSceneCache(input);
            return;
        }

        dataVersion_ = dataVersion;
        sceneSource_.layoutVersion =
            BuildSourceLayoutVersion(layoutVersion_, routingVersion_);
        sceneSource_.sourceVersion = dataVersion_;
        sceneSource_.dirtyBaseSourceVersion = patchBaseSourceVersion;
    }

    void GpuSceneRegistry::RebuildForwardFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        Clear();
        if (input.sceneCache == nullptr) {
            return;
        }
        publishStaticTraditionalStreams_ =
            ShouldPublishStaticTraditionalStreams();

        const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces =
            input.sceneCache->GetSurfaceInstances();
        surfaceRecords_.reserve(surfaces.size());
        for (uint32_t surfaceIndex = 0;
            surfaceIndex < surfaces.size();
            ++surfaceIndex) {

            GpuSceneSurfaceRecord record =
                BuildGpuSceneSurfaceRecord(
                    surfaces[surfaceIndex],
                    surfaceIndex);
            surfaceRecords_.push_back(std::move(record));
        }

        meshShaderJointPalettes_.resize(surfaceRecords_.size());
        GpuScenePoseBuilder meshShaderPoseBuilder{};
        for (uint32_t recordIndex = 0;
            recordIndex < surfaceRecords_.size();
            ++recordIndex) {

            GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            if (!record.skinned ||
                record.key.backendRoute != RUNTIME::SurfaceBackendRoute::MeshShader ||
                recordIndex >= RUNTIME::kSurfaceGpuSceneMaxDeformationPalettes) {
                continue;
            }
            const std::vector<MATH::Mat4>* palette =
                meshShaderPoseBuilder.ResolveJointPalette(record);
            if (palette == nullptr || palette->empty() ||
                palette->size() > RUNTIME::kSurfaceGpuSceneMaxJointMatrices) {
                record.jointPaletteSlot = RUNTIME::kInvalidRenderSurfaceIndex;
                record.jointPaletteMatrixCount = 0u;
                continue;
            }
            meshShaderJointPalettes_[recordIndex] = *palette;
            record.jointPaletteSlot = recordIndex;
            record.jointPaletteMatrixCount = NUMERIC::SaturateToUint32(palette->size());
        }

        stats_.sourceRecordCount = NUMERIC::SaturateToUint32(surfaceRecords_.size());

        std::vector<uint32_t> routedRecordIndices{};
        routedRecordIndices.reserve(surfaceRecords_.size());
        for (uint32_t recordIndex = 0;
            recordIndex < surfaceRecords_.size();
            ++recordIndex) {

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            const bool meshShaderMaterialFxRecord =
                record.key.materialFx &&
                !record.key.materialFxUsesCustomVertexShader;
            if (record.valid && record.shadowCandidate && record.key.resourceKeyValid) {
                if (IsGpuSceneStaticShadowCasterRecord(record)) {
                    shadowResidentRecordIndices_.push_back(recordIndex);
                    if (meshShaderMaterialFxRecord) {
                        ++stats_.shadowMaterialFxMeshShaderRecordCount;
                    }
                    if (record.key.skinned) {
                        ++stats_.shadowSkinnedMeshShaderRecordCount;
                    }
                } else if (IsGpuSceneShadowStaticTraditionalRecord(record)) {
                    shadowStaticTraditionalRecordIndices_.push_back(recordIndex);
                    if (meshShaderMaterialFxRecord) {
                        ++stats_.shadowMaterialFxTraditionalRecordCount;
                    }
                } else if (IsGpuSceneShadowSkinnedTraditionalRecord(record)) {
                    shadowSkinnedRecordIndices_.push_back(recordIndex);
                } else {
                    ++stats_.blockedShadowRecordCount;
                }
            }
            if (record.valid && record.forwardCandidate && record.key.resourceKeyValid) {
                routedRecordIndices.push_back(recordIndex);
            }
        }
        stats_.forwardRoutedRecordCount = NUMERIC::SaturateToUint32(routedRecordIndices.size());

        forwardOpaqueResidentRecordIndices_.reserve(routedRecordIndices.size());
        forwardDepthAwareResidentRecordIndices_.reserve(routedRecordIndices.size());
        forwardTransparentResidentRecordIndices_.reserve(routedRecordIndices.size());
        forwardOpaqueStaticTraditionalRecordIndices_.reserve(routedRecordIndices.size());
        forwardDepthAwareStaticTraditionalRecordIndices_.reserve(routedRecordIndices.size());
        forwardTransparentStaticTraditionalRecordIndices_.reserve(routedRecordIndices.size());
        for (const uint32_t recordIndex : routedRecordIndices) {
            if (recordIndex >= surfaceRecords_.size()) {
                continue;
            }

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            const bool meshShaderMaterialFxRecord =
                record.key.materialFx &&
                !record.key.materialFxUsesCustomVertexShader;
            if (IsGpuSceneForwardOpaqueResidentRecord(record)) {
                forwardOpaqueResidentRecordIndices_.push_back(recordIndex);
                ++stats_.forwardOpaqueClusterCandidateRecordCount;
                if (meshShaderMaterialFxRecord) {
                    ++stats_.forwardMaterialFxMeshShaderRecordCount;
                }
                if (record.key.skinned) {
                    ++stats_.forwardSkinnedMeshShaderRecordCount;
                }
            } else if (IsGpuSceneForwardDepthAwareResidentRecord(record)) {
                forwardDepthAwareResidentRecordIndices_.push_back(recordIndex);
                if (meshShaderMaterialFxRecord) {
                    ++stats_.forwardMaterialFxMeshShaderRecordCount;
                }
                if (record.key.skinned) {
                    ++stats_.forwardSkinnedMeshShaderRecordCount;
                }
            } else if (IsGpuSceneForwardTransparentResidentRecord(record)) {
                forwardTransparentResidentRecordIndices_.push_back(recordIndex);
                if (meshShaderMaterialFxRecord) {
                    ++stats_.forwardMaterialFxMeshShaderRecordCount;
                }
                if (record.key.skinned) {
                    ++stats_.forwardSkinnedMeshShaderRecordCount;
                }
            } else if (IsGpuSceneForwardSkinnedTraditionalRecord(record)) {
                if (record.key.depthAware) {
                    forwardDepthAwareSkinnedRecordIndices_.push_back(recordIndex);
                } else if (record.key.transparent) {
                    forwardTransparentSkinnedRecordIndices_.push_back(recordIndex);
                } else {
                    forwardOpaqueSkinnedRecordIndices_.push_back(recordIndex);
                }
            } else if (IsGpuSceneForwardStaticTraditionalRecord(record)) {
                if (meshShaderMaterialFxRecord) {
                    ++stats_.forwardMaterialFxTraditionalRecordCount;
                }
                if (record.key.depthAware) {
                    forwardDepthAwareStaticTraditionalRecordIndices_.push_back(recordIndex);
                } else if (record.key.transparent) {
                    forwardTransparentStaticTraditionalRecordIndices_.push_back(recordIndex);
                } else {
                    forwardOpaqueStaticTraditionalRecordIndices_.push_back(recordIndex);
                }
            } else if (record.forwardCandidate) {
                ++stats_.unsupportedForwardRecordCount;
                if (meshShaderMaterialFxRecord) {
                    ++stats_.forwardMaterialFxBlockedRecordCount;
                }
                if (record.key.depthAware) {
                    ++stats_.blockedForwardDepthAwareRecordCount;
                } else if (record.key.transparent) {
                    ++stats_.blockedForwardTransparentRecordCount;
                }
            }
        }
        stats_.forwardOpaqueResidentRecordCount =
            NUMERIC::SaturateToUint32(forwardOpaqueResidentRecordIndices_.size());
        stats_.forwardStaticTraditionalRecordCount =
            NUMERIC::SaturateToUint32(
                forwardOpaqueStaticTraditionalRecordIndices_.size() +
                forwardDepthAwareStaticTraditionalRecordIndices_.size() +
                forwardTransparentStaticTraditionalRecordIndices_.size());
        stats_.shadowStaticTraditionalRecordCount =
            NUMERIC::SaturateToUint32(shadowStaticTraditionalRecordIndices_.size());
        stats_.forwardSkinnedTraditionalRecordCount =
            NUMERIC::SaturateToUint32(
                forwardOpaqueSkinnedRecordIndices_.size() +
                forwardDepthAwareSkinnedRecordIndices_.size() +
                forwardTransparentSkinnedRecordIndices_.size());
        stats_.shadowSkinnedTraditionalRecordCount =
            NUMERIC::SaturateToUint32(shadowSkinnedRecordIndices_.size());
        stats_.strictMainlineBlockedRecordCount =
            stats_.blockedForwardDepthAwareRecordCount +
            stats_.blockedForwardTransparentRecordCount +
            stats_.blockedShadowRecordCount;

        stats_.forwardOpaqueBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardOpaqueResidentRecordIndices_,
                true);
        BuildDepthPrepassOccluderRecords(
            surfaceRecords_,
            forwardOpaqueResidentRecordIndices_,
            depthPrepassOccluderRecordIndices_,
            stats_);
        stats_.forwardDepthAwareBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardDepthAwareResidentRecordIndices_,
                true);
        stats_.forwardTransparentBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardTransparentResidentRecordIndices_,
                false);
        stats_.shadowBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                shadowResidentRecordIndices_,
                true);

        const auto buildPass =
            [&](GpuDrivenPassKind passKind,
                const std::vector<uint32_t>& recordIndices,
                std::vector<uint32_t>& indexByRecord,
                std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances,
                std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources) {
            indexByRecord.assign(
                surfaceRecords_.size(),
                RUNTIME::kInvalidRenderSurfaceIndex);
            RUNTIME::SurfaceGpuSceneBuildStats passStats =
                BuildGpuSceneInstanceList(
                surfaceRecords_,
                    recordIndices,
                    instances,
                    materialSources);
            for (uint32_t localInstanceIndex = 0;
                localInstanceIndex < recordIndices.size();
                ++localInstanceIndex) {

                const uint32_t recordIndex = recordIndices[localInstanceIndex];
                if (recordIndex < indexByRecord.size()) {
                    indexByRecord[recordIndex] = localInstanceIndex;
                }
                if (localInstanceIndex < instances.size()) {
                    instances[localInstanceIndex].flags |=
                        PassInstanceFlag(passKind);
                }
            }
            return passStats;
        };

        stats_.forwardOpaqueGpuSceneStats =
            buildPass(
                GpuDrivenPassKind::ForwardOpaque,
                forwardOpaqueResidentRecordIndices_,
                forwardOpaqueGpuSceneIndexByRecord_,
                forwardOpaqueGpuSceneInstances_,
                forwardOpaqueMaterialSources_);
        stats_.depthPrepassGpuSceneStats =
            buildPass(
                GpuDrivenPassKind::DepthPrepass,
                depthPrepassOccluderRecordIndices_,
                depthPrepassGpuSceneIndexByRecord_,
                depthPrepassGpuSceneInstances_,
                depthPrepassMaterialSources_);
        stats_.forwardDepthAwareGpuSceneStats =
            buildPass(
                GpuDrivenPassKind::ForwardDepthAware,
                forwardDepthAwareResidentRecordIndices_,
                forwardDepthAwareGpuSceneIndexByRecord_,
                forwardDepthAwareGpuSceneInstances_,
                forwardDepthAwareMaterialSources_);
        stats_.forwardTransparentGpuSceneStats =
            buildPass(
                GpuDrivenPassKind::ForwardTransparent,
                forwardTransparentResidentRecordIndices_,
                forwardTransparentGpuSceneIndexByRecord_,
                forwardTransparentGpuSceneInstances_,
                forwardTransparentMaterialSources_);
        (void)buildPass(
            GpuDrivenPassKind::Shadow,
            shadowResidentRecordIndices_,
            shadowGpuSceneIndexByRecord_,
            shadowGpuSceneInstances_,
            shadowMaterialSources_);

        globalGpuSceneIndexByRecord_.assign(
            surfaceRecords_.size(),
            RUNTIME::kInvalidRenderSurfaceIndex);
        std::vector<uint32_t> globalRecordIndices{};
        globalRecordIndices.reserve(
            forwardOpaqueResidentRecordIndices_.size() +
            depthPrepassOccluderRecordIndices_.size() +
            forwardDepthAwareResidentRecordIndices_.size() +
            forwardTransparentResidentRecordIndices_.size() +
            shadowResidentRecordIndices_.size());

        const auto appendGlobalRecords =
            [&](const std::vector<uint32_t>& recordIndices) {
            for (const uint32_t recordIndex : recordIndices) {
                if (recordIndex >= globalGpuSceneIndexByRecord_.size()) {
                    continue;
                }
                if (globalGpuSceneIndexByRecord_[recordIndex] !=
                    RUNTIME::kInvalidRenderSurfaceIndex) {
                    continue;
                }
                globalGpuSceneIndexByRecord_[recordIndex] =
                    NUMERIC::SaturateToUint32(globalRecordIndices.size());
                globalRecordIndices.push_back(recordIndex);
            }
        };

        appendGlobalRecords(forwardOpaqueResidentRecordIndices_);
        appendGlobalRecords(depthPrepassOccluderRecordIndices_);
        appendGlobalRecords(forwardDepthAwareResidentRecordIndices_);
        appendGlobalRecords(forwardTransparentResidentRecordIndices_);
        appendGlobalRecords(shadowResidentRecordIndices_);

        (void)BuildGpuSceneInstanceList(
            surfaceRecords_,
            globalRecordIndices,
            globalGpuSceneInstances_,
            globalMaterialSources_);

        const auto markGlobalPass =
            [&](GpuDrivenPassKind passKind,
                const std::vector<uint32_t>& recordIndices) {
            const uint32_t passFlag = PassInstanceFlag(passKind);
            for (const uint32_t recordIndex : recordIndices) {
                if (recordIndex >= globalGpuSceneIndexByRecord_.size()) {
                    continue;
                }
                const uint32_t globalIndex =
                    globalGpuSceneIndexByRecord_[recordIndex];
                if (globalIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
                    globalIndex >= globalGpuSceneInstances_.size() ||
                    globalIndex >= globalMaterialSources_.size()) {
                    continue;
                }
                globalGpuSceneInstances_[globalIndex].flags |= passFlag;
                globalMaterialSources_[globalIndex].localGpuSceneInstanceIndex =
                    globalIndex;
            }
        };

        markGlobalPass(
            GpuDrivenPassKind::ForwardOpaque,
            forwardOpaqueResidentRecordIndices_);
        markGlobalPass(
            GpuDrivenPassKind::DepthPrepass,
            depthPrepassOccluderRecordIndices_);
        markGlobalPass(
            GpuDrivenPassKind::ForwardDepthAware,
            forwardDepthAwareResidentRecordIndices_);
        markGlobalPass(
            GpuDrivenPassKind::ForwardTransparent,
            forwardTransparentResidentRecordIndices_);
        markGlobalPass(
            GpuDrivenPassKind::Shadow,
            shadowResidentRecordIndices_);

        const auto buildTraditionalStreamStats =
            [](const TraditionalIndirectStream& stream) {
            RUNTIME::SurfaceGpuSceneBuildStats streamStats{};
            streamStats.commandCount = NUMERIC::SaturateToUint32(stream.commands.size());
            streamStats.instanceCount = NUMERIC::SaturateToUint32(stream.instances.size());
            streamStats.maxCommandInstanceCount = stream.instances.empty() ? 0u : 1u;
            for (const RUNTIME::SurfaceGpuSceneInstance& instance : stream.instances) {
                if ((instance.resourceFlags &
                    static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::Mesh)) != 0u &&
                    (instance.resourceFlags &
                    static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::Material)) != 0u) {
                    ++streamStats.resourceBackedInstanceCount;
                } else {
                    ++streamStats.missingResourceHandleInstanceCount;
                }
            }
            return streamStats;
        };

        const auto buildTraditionalStream =
            [&](const std::vector<uint32_t>& mainlineStaticRecordIndices,
                const std::vector<uint32_t>& sidecarStaticRecordIndices,
                const std::vector<uint32_t>& skinnedRecordIndices,
                RUNTIME::SurfaceDrawCommandPass pass,
                TraditionalIndirectStream& stream) {
            stream.Clear();
            for (const uint32_t recordIndex : sidecarStaticRecordIndices) {
                AppendStaticTraditionalRecord(
                    surfaceRecords_,
                    recordIndex,
                    pass,
                    stream);
            }

            if (publishStaticTraditionalStreams_) {
                std::unordered_set<uint32_t> sidecarRecordSet{};
                sidecarRecordSet.reserve(sidecarStaticRecordIndices.size());
                for (const uint32_t recordIndex : sidecarStaticRecordIndices) {
                    sidecarRecordSet.insert(recordIndex);
                }
                for (const uint32_t recordIndex : mainlineStaticRecordIndices) {
                    if (sidecarRecordSet.find(recordIndex) !=
                        sidecarRecordSet.end()) {
                        continue;
                    }
                    AppendStaticTraditionalRecord(
                        surfaceRecords_,
                        recordIndex,
                        pass,
                        stream);
                }
            }

            GpuScenePoseBuilder poseBuilder{};
            for (const uint32_t recordIndex : skinnedRecordIndices) {
                AppendSkinnedTraditionalRecord(
                    surfaceRecords_,
                    recordIndex,
                    pass,
                    stream,
                    poseBuilder);
            }

            return buildTraditionalStreamStats(stream);
        };

        stats_.forwardOpaqueTraditionalGpuSceneStats =
            buildTraditionalStream(
                forwardOpaqueResidentRecordIndices_,
                forwardOpaqueStaticTraditionalRecordIndices_,
                forwardOpaqueSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Forward,
                forwardOpaqueTraditionalStream_);
        stats_.forwardOpaqueSkinnedTraditionalGpuSceneStats =
            stats_.forwardOpaqueTraditionalGpuSceneStats;
        stats_.forwardDepthAwareTraditionalGpuSceneStats =
            buildTraditionalStream(
                forwardDepthAwareResidentRecordIndices_,
                forwardDepthAwareStaticTraditionalRecordIndices_,
                forwardDepthAwareSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::DepthAware,
                forwardDepthAwareTraditionalStream_);
        stats_.forwardDepthAwareSkinnedTraditionalGpuSceneStats =
            stats_.forwardDepthAwareTraditionalGpuSceneStats;
        stats_.forwardTransparentTraditionalGpuSceneStats =
            buildTraditionalStream(
                forwardTransparentResidentRecordIndices_,
                forwardTransparentStaticTraditionalRecordIndices_,
                forwardTransparentSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Forward,
                forwardTransparentTraditionalStream_);
        stats_.forwardTransparentSkinnedTraditionalGpuSceneStats =
            stats_.forwardTransparentTraditionalGpuSceneStats;
        stats_.shadowSkinnedTraditionalGpuSceneStats =
            buildTraditionalStream(
                shadowResidentRecordIndices_,
                shadowStaticTraditionalRecordIndices_,
                shadowSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Shadow,
                shadowTraditionalStream_);

        layoutVersion_ = input.sceneCache->GetSurfaceVersion();
        routingVersion_ = input.sceneCache->GetSurfaceRoutingVersion();
        dataVersion_ = input.sceneCache->GetSurfaceDataVersion();
        sourceSurfaceCount_ = NUMERIC::SaturateToUint32(surfaces.size());
        SuppressCpuForwardViews(input);
        RebuildForwardSceneSource();
    }

    void GpuSceneRegistry::SuppressCpuForwardViews(
        const GpuSceneRegistrySyncInput& input) {

        stats_.strictGpuDrivenMainline = true;
        stats_.cpuForwardViewSuppressedCount =
            input.sceneCache != nullptr ? 1u : 0u;
    }

    bool GpuSceneRegistry::TryPatchForwardDataFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        if (ShouldPublishStaticTraditionalStreams()) {
            return false;
        }

        if (input.sceneCache == nullptr) {
            return false;
        }
        const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces =
            input.sceneCache->GetSurfaceInstances();
        if (surfaces.size() != surfaceRecords_.size()) {
            return false;
        }

        std::vector<uint32_t> dirtySurfaceIndices =
            input.sceneCache->GetDirtySurfaceIndices();
        std::sort(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end());
        dirtySurfaceIndices.erase(
            std::unique(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end()),
            dirtySurfaceIndices.end());

        std::vector<uint32_t> singleRecordIndex{};
        singleRecordIndex.reserve(1);
        std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};
        GpuScenePoseBuilder meshShaderPoseBuilder{};

        for (const uint32_t surfaceIndex : dirtySurfaceIndices) {
            if (surfaceIndex >= surfaces.size() ||
                surfaceIndex >= surfaceRecords_.size()) {
                return false;
            }

            GpuSceneSurfaceRecord newRecord =
                BuildGpuSceneSurfaceRecord(surfaces[surfaceIndex], surfaceIndex);
            if (newRecord.skinned &&
                newRecord.key.backendRoute == RUNTIME::SurfaceBackendRoute::MeshShader &&
                surfaceIndex < RUNTIME::kSurfaceGpuSceneMaxDeformationPalettes) {
                const std::vector<MATH::Mat4>* palette =
                    meshShaderPoseBuilder.ResolveJointPalette(newRecord);
                if (palette != nullptr && !palette->empty() &&
                    palette->size() <= RUNTIME::kSurfaceGpuSceneMaxJointMatrices) {
                    if (meshShaderJointPalettes_.size() != surfaceRecords_.size()) {
                        return false;
                    }
                    meshShaderJointPalettes_[surfaceIndex] = *palette;
                    newRecord.jointPaletteSlot = surfaceIndex;
                    newRecord.jointPaletteMatrixCount = NUMERIC::SaturateToUint32(palette->size());
                }
            }
            const GpuSceneSurfaceRecord& oldRecord = surfaceRecords_[surfaceIndex];
            if (IsGpuSceneForwardSkinnedTraditionalRecord(oldRecord) ||
                IsGpuSceneForwardSkinnedTraditionalRecord(newRecord) ||
                IsGpuSceneForwardStaticTraditionalRecord(oldRecord) ||
                IsGpuSceneForwardStaticTraditionalRecord(newRecord) ||
                IsGpuSceneShadowSkinnedTraditionalRecord(oldRecord) ||
                IsGpuSceneShadowSkinnedTraditionalRecord(newRecord) ||
                IsGpuSceneShadowStaticTraditionalRecord(oldRecord) ||
                IsGpuSceneShadowStaticTraditionalRecord(newRecord)) {
                return false;
            }
            const bool oldOpaque = IsGpuSceneForwardOpaqueResidentRecord(oldRecord);
            const bool newOpaque = IsGpuSceneForwardOpaqueResidentRecord(newRecord);
            const bool oldDepthOccluder =
                IsDepthPrepassCoverageRecord(oldRecord);
            const bool newDepthOccluder =
                IsDepthPrepassCoverageRecord(newRecord);
            const bool oldDepthAware =
                IsGpuSceneForwardDepthAwareResidentRecord(oldRecord);
            const bool newDepthAware =
                IsGpuSceneForwardDepthAwareResidentRecord(newRecord);
            const bool oldTransparent =
                IsGpuSceneForwardTransparentResidentRecord(oldRecord);
            const bool newTransparent =
                IsGpuSceneForwardTransparentResidentRecord(newRecord);
            const bool oldShadow =
                IsGpuSceneStaticShadowCasterRecord(oldRecord);
            const bool newShadow =
                IsGpuSceneStaticShadowCasterRecord(newRecord);
            if (oldOpaque != newOpaque ||
                oldDepthOccluder != newDepthOccluder ||
                oldDepthAware != newDepthAware ||
                oldTransparent != newTransparent ||
                oldShadow != newShadow ||
                !HasSameStaticGpuSceneBatchIdentity(oldRecord, newRecord)) {
                return false;
            }
            if ((oldDepthOccluder || newDepthOccluder) &&
                stats_.depthPrepassBudgetClippedRecordCount != 0u) {
                return false;
            }

            surfaceRecords_[surfaceIndex] = std::move(newRecord);
            if (!newOpaque &&
                !newDepthOccluder &&
                !newDepthAware &&
                !newTransparent &&
                !newShadow) {
                continue;
            }

            singleRecordIndex.clear();
            singleRecordIndex.push_back(surfaceIndex);
            const RUNTIME::SurfaceGpuSceneBuildStats buildStats =
                BuildGpuSceneInstanceList(
                    surfaceRecords_,
                    singleRecordIndex,
                    singleInstance,
                    singleMaterial);
            if (buildStats.instanceCount != 1u ||
                singleInstance.size() != 1u ||
                singleMaterial.size() != 1u) {
                return false;
            }

            if (surfaceIndex >= globalGpuSceneIndexByRecord_.size()) {
                return false;
            }
            const uint32_t globalInstanceIndex =
                globalGpuSceneIndexByRecord_[surfaceIndex];
            if (globalInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
                globalInstanceIndex >= globalGpuSceneInstances_.size() ||
                globalInstanceIndex >= globalMaterialSources_.size()) {
                return false;
            }

            RUNTIME::SurfaceGpuSceneInstance patchedInstance =
                singleInstance[0];
            if (newOpaque) {
                patchedInstance.flags |=
                    PassInstanceFlag(GpuDrivenPassKind::ForwardOpaque);
            }
            if (newDepthOccluder) {
                patchedInstance.flags |=
                    PassInstanceFlag(GpuDrivenPassKind::DepthPrepass);
            }
            if (newDepthAware) {
                patchedInstance.flags |=
                    PassInstanceFlag(GpuDrivenPassKind::ForwardDepthAware);
            }
            if (newTransparent) {
                patchedInstance.flags |=
                    PassInstanceFlag(GpuDrivenPassKind::ForwardTransparent);
            }
            if (newShadow) {
                patchedInstance.flags |=
                    PassInstanceFlag(GpuDrivenPassKind::Shadow);
            }
            globalGpuSceneInstances_[globalInstanceIndex] = patchedInstance;

            RUNTIME::SurfaceGpuSceneMaterialSource materialSource =
                singleMaterial[0];
            materialSource.localGpuSceneInstanceIndex = globalInstanceIndex;
            globalMaterialSources_[globalInstanceIndex] = materialSource;

            const GpuDrivenPassKind dirtyPass =
                newOpaque ? GpuDrivenPassKind::ForwardOpaque :
                newDepthOccluder ? GpuDrivenPassKind::DepthPrepass :
                newDepthAware ? GpuDrivenPassKind::ForwardDepthAware :
                newTransparent ? GpuDrivenPassKind::ForwardTransparent :
                GpuDrivenPassKind::Shadow;
            AppendDirtyRange(
                sceneSource_.GetPass(dirtyPass).dirtyRanges,
                globalInstanceIndex,
                1u);
        }

        return true;
    }

    void GpuSceneRegistry::RebuildForwardSceneSource() {
        sceneSource_.meshShaderJointPalettes = &meshShaderJointPalettes_;
        const uint32_t sharedPrimaryCount =
            NUMERIC::SaturateToUint32(globalGpuSceneInstances_.size());
        uint32_t cursor = sharedPrimaryCount;

        const auto resetSharedPrimaryPass =
            [&](GpuDrivenPassKind passKind,
                const std::vector<uint32_t>& recordIndices,
                GpuDrivenBackendKind backend,
                bool clusterEligible) {
            GpuDrivenPassSource& pass = sceneSource_.GetPass(passKind);
            if (recordIndices.empty() || globalGpuSceneInstances_.empty()) {
                ResetPassSource(
                    pass,
                    nullptr,
                    nullptr,
                    0u,
                    backend,
                    clusterEligible);
                return;
            }

            ResetPassSource(
                pass,
                &globalGpuSceneInstances_,
                &globalMaterialSources_,
                0u,
                backend,
                clusterEligible);
        };

        resetSharedPrimaryPass(
            GpuDrivenPassKind::ForwardOpaque,
            forwardOpaqueResidentRecordIndices_,
            GpuDrivenBackendKind::MeshShader,
            true);
        AttachTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardOpaque),
            forwardOpaqueTraditionalStream_,
            cursor);
        cursor += NUMERIC::SaturateToUint32(forwardOpaqueTraditionalStream_.instances.size());

        resetSharedPrimaryPass(
            GpuDrivenPassKind::DepthPrepass,
            depthPrepassOccluderRecordIndices_,
            GpuDrivenBackendKind::MeshShader,
            true);

        resetSharedPrimaryPass(
            GpuDrivenPassKind::ForwardDepthAware,
            forwardDepthAwareResidentRecordIndices_,
            GpuDrivenBackendKind::MeshShader,
            true);
        AttachTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardDepthAware),
            forwardDepthAwareTraditionalStream_,
            cursor);
        cursor += NUMERIC::SaturateToUint32(forwardDepthAwareTraditionalStream_.instances.size());

        resetSharedPrimaryPass(
            GpuDrivenPassKind::ForwardTransparent,
            forwardTransparentResidentRecordIndices_,
            GpuDrivenBackendKind::MeshShader,
            true);
        AttachTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardTransparent),
            forwardTransparentTraditionalStream_,
            cursor);
        cursor += NUMERIC::SaturateToUint32(forwardTransparentTraditionalStream_.instances.size());

        resetSharedPrimaryPass(
            GpuDrivenPassKind::Shadow,
            shadowResidentRecordIndices_,
            GpuDrivenBackendKind::MeshShader,
            true);
        AttachTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow),
            shadowTraditionalStream_,
            cursor);
        cursor += NUMERIC::SaturateToUint32(shadowTraditionalStream_.instances.size());

        sceneSource_.layoutVersion =
            BuildSourceLayoutVersion(layoutVersion_, routingVersion_);
        sceneSource_.sourceVersion = dataVersion_;
        sceneSource_.dirtyBaseSourceVersion = dataVersion_;
        sceneSource_.sourceInstanceCount = cursor;
        sceneSource_.sourceRecordCount = NUMERIC::SaturateToUint32(surfaceRecords_.size());
    }

    void GpuSceneRegistry::ClearFrameDirtyRanges() {
        for (GpuDrivenPassSource& pass : sceneSource_.passes) {
            pass.dirtyRanges.clear();
        }
    }

    const GpuDrivenSceneSource& GpuSceneRegistry::GetSceneSource() const {
        return sceneSource_;
    }

    bool GpuSceneRegistry::HasShadowPassSource() const {
        const GpuDrivenPassSource& shadow =
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow);
        return shadow.HasPrimaryGpuSceneInstances();
    }

    const std::vector<GpuSceneSurfaceRecord>& GpuSceneRegistry::GetSurfaceRecords() const {
        return surfaceRecords_;
    }

    const GpuSceneRegistryStats& GpuSceneRegistry::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
