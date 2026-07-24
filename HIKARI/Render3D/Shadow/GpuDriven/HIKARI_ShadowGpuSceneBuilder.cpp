#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "HIKARI_Services.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_OwnedTraditionalIndirectStream.h"

namespace HIKARI::SHADOW::INTERNAL {

    using OwnedShadowTraditionalIndirectStream =
        RENDER3D::GPUDRIVEN::OwnedTraditionalIndirectStream;

    OwnedShadowTraditionalIndirectStream gShadowStaticTraditionalIndirectStream{};
    OwnedShadowTraditionalIndirectStream gShadowDynamicTraditionalIndirectStream{};

    void ClearShadowTraditionalIndirectStreams() {
        gShadowStaticTraditionalIndirectStream.Clear();
        gShadowDynamicTraditionalIndirectStream.Clear();
    }

    Mesh* GetOrCreatePrimitiveMesh(const MeshPrimitive& primitive) {
        auto found = gShadowRendererState.primitiveMeshCache.find(&primitive);
        if (found != gShadowRendererState.primitiveMeshCache.end()) {
            return found->second.get();
        }

        if (primitive.staticVertices.empty() || primitive.indices.empty()) {
            return nullptr;
        }

        std::vector<VertexStatic3D> vertices;
        vertices.reserve(primitive.staticVertices.size());
        for (const Vertex3D& src : primitive.staticVertices) {
            VertexStatic3D dst{};
            dst.position = src.position;
            dst.normal = src.normal;
            dst.tangent = src.tangent;
            if (MATH::Length({ dst.tangent.x, dst.tangent.y, dst.tangent.z }) <= 1e-6f) {
                dst.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
            }
            dst.u = src.uv0.x;
            dst.v = src.uv0.y;
            dst.uv1 = src.uv1;
            vertices.push_back(dst);
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, vertices, primitive.indices)) {
            return nullptr;
        }
        Mesh* raw = mesh.get();
        gShadowRendererState.primitiveMeshCache.emplace(&primitive, std::move(mesh));
        return raw;
    }

    Mesh* GetOrCreateSkinnedPrimitiveMesh(const MeshPrimitive& primitive) {
        auto found = gShadowRendererState.primitiveSkinnedMeshCache.find(&primitive);
        if (found != gShadowRendererState.primitiveSkinnedMeshCache.end()) {
            return found->second.get();
        }

        if (primitive.skinnedVertices.empty() || primitive.indices.empty()) {
            return nullptr;
        }

        std::vector<VertexSkinnedGpu3D> vertices;
        vertices.reserve(primitive.skinnedVertices.size());
        for (const SkinnedVertex3D& src : primitive.skinnedVertices) {
            VertexSkinnedGpu3D dst{};
            dst.position = src.position;
            dst.normal = src.normal;
            dst.tangent = src.tangent;
            dst.uv0 = src.uv0;
            dst.uv1 = src.uv1;
            dst.color0 = src.color0;
            for (size_t i = 0; i < 4; ++i) {
                dst.joints[i] = src.joints[i];
                dst.weights[i] = src.weights[i];
            }
            vertices.push_back(dst);
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateSkinned(SERVICES::gCtx.device, vertices, primitive.indices)) {
            return nullptr;
        }
        Mesh* raw = mesh.get();
        gShadowRendererState.primitiveSkinnedMeshCache.emplace(&primitive, std::move(mesh));
        return raw;
    }

    const MeshPrimitive* ResolveShadowTraditionalRecordPrimitive(
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {

        if (record.model == nullptr ||
            record.meshIndex >= record.model->meshes.size()) {
            return nullptr;
        }

        const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
        if (record.primitiveIndex >= meshAsset.primitives.size()) {
            return nullptr;
        }

        return &meshAsset.primitives[record.primitiveIndex];
    }

    void UploadShadowMeshShaderJointPalettes() {
        if (gShadowRendererState.gpuDrivenSceneSource == nullptr ||
            gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes == nullptr) {
            return;
        }
        const auto& palettes =
            *gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes;
        const size_t paletteCount =
            (std::min)(palettes.size(), static_cast<size_t>(kMaxShadowCasterObjects));
        for (size_t paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
            if (!palettes[paletteIndex].empty()) {
                (void)UploadShadowJointPalette(
                    paletteIndex,
                    palettes[paletteIndex]);
            }
        }
    }

    void HydrateShadowTraditionalIndirectStream(
        OwnedShadowTraditionalIndirectStream& stream) {

        if (stream.records.empty() ||
            stream.executableRecordIndices.empty() ||
            stream.commands.empty()) {
            return;
        }

        for (RENDER3D::RUNTIME::SurfaceDrawCommand& command :
            stream.commands) {

            command.triangleMeshView = {};
            command.jointPaletteGpuAddress = 0;
            if (command.recordCount == 0 ||
                command.firstExecutableIndex >=
                    stream.executableRecordIndices.size()) {
                continue;
            }

            const uint32_t recordIndex =
                stream.executableRecordIndices[command.firstExecutableIndex];
            if (recordIndex >= stream.records.size()) {
                continue;
            }

            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                stream.records[recordIndex];
            const MeshPrimitive* primitive =
                ResolveShadowTraditionalRecordPrimitive(record);
            if (primitive == nullptr) {
                continue;
            }

            const bool hasJointPalette =
                command.firstRecordIndex !=
                    RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                command.firstRecordIndex < stream.jointPalettes.size() &&
                !stream.jointPalettes[command.firstRecordIndex].empty();
            const bool skinnedCommand = record.skinned && hasJointPalette;
            Mesh* mesh = skinnedCommand
                ? GetOrCreateSkinnedPrimitiveMesh(*primitive)
                : GetOrCreatePrimitiveMesh(*primitive);
            if (mesh == nullptr || !mesh->IsValid()) {
                continue;
            }

            command.triangleMeshView.vertexBuffer = mesh->GetVBView();
            command.triangleMeshView.indexBuffer = mesh->GetIBView();
            if (!command.HasTriangleMeshGpuView() || !skinnedCommand) {
                continue;
            }

            const size_t paletteSlot =
                static_cast<size_t>(stream.gpuSceneBaseIndex) +
                static_cast<size_t>(command.firstGpuSceneInstanceIndex);
            ShadowFrameResources& frame = GetActiveShadowFrameResources();
            if (paletteSlot >= kMaxShadowCasterObjects ||
                frame.jointPaletteMapped == nullptr ||
                frame.jointPaletteCB == nullptr) {
                continue;
            }

            (void)UploadShadowJointPalette(
                paletteSlot,
                stream.jointPalettes[command.firstRecordIndex]);
            command.jointPaletteGpuAddress =
                ResolveShadowJointPaletteAddress(paletteSlot);
        }
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* GetSourceShadowPass() {
        if (gShadowRendererState.gpuDrivenSceneSource == nullptr) {
            return nullptr;
        }
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
            gShadowRendererState.gpuDrivenSceneSource->GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        return pass.HasGpuSceneInstances()
            ? &pass
            : nullptr;
    }

    bool BuildShadowGpuDrivenSceneSources() {
        CPU_PROFILE::ScopedCpuTimer cpuTimer(
            CPU_PROFILE::Pass::ShadowSourceSync);

        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* sourcePass =
            GetSourceShadowPass();
        if (sourcePass == nullptr ||
            !sourcePass->HasGpuSceneInstances()) {

            gShadowRendererState.shadowSceneSource.Reset();
            gShadowRendererState.staticShadowSceneSource.Reset();
            gShadowRendererState.dynamicShadowSceneSource.Reset();
            gShadowRendererState.activeShadowSceneSource.Reset();
            gShadowRendererState.staticShadowPrimaryInstances.clear();
            gShadowRendererState.staticShadowPrimaryMaterialSources.clear();
            gShadowRendererState.dynamicShadowPrimaryInstances.clear();
            gShadowRendererState.dynamicShadowPrimaryMaterialSources.clear();
            gShadowStaticTraditionalIndirectStream.Clear();
            gShadowDynamicTraditionalIndirectStream.Clear();
            InvalidateShadowSourceCache();
            return false;
        }

        if (CanReuseShadowSourceCache()) {
            gShadowRendererState.frameHasStaticShadowWork =
                gShadowRendererState.staticShadowSceneSource.sourceInstanceCount != 0u;
            gShadowRendererState.frameHasDynamicShadowWork =
                gShadowRendererState.dynamicShadowSceneSource.sourceInstanceCount != 0u;
            gShadowRendererState.debugStats.shadowStaticSourceInstanceCount =
                gShadowRendererState.staticShadowSceneSource.sourceInstanceCount;
            gShadowRendererState.debugStats.shadowDynamicSourceInstanceCount =
                gShadowRendererState.dynamicShadowSceneSource.sourceInstanceCount;
            return gShadowRendererState.shadowSceneSource.sourceInstanceCount != 0u;
        }

        gShadowRendererState.shadowSceneSource.Reset();
        gShadowRendererState.staticShadowSceneSource.Reset();
        gShadowRendererState.dynamicShadowSceneSource.Reset();
        gShadowRendererState.activeShadowSceneSource.Reset();
        gShadowRendererState.shadowSceneSource.meshShaderJointPalettes =
            gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes;
        gShadowRendererState.staticShadowSceneSource.meshShaderJointPalettes =
            gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes;
        gShadowRendererState.dynamicShadowSceneSource.meshShaderJointPalettes =
            gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes;
        gShadowRendererState.staticShadowPrimaryInstances.clear();
        gShadowRendererState.staticShadowPrimaryMaterialSources.clear();
        gShadowRendererState.dynamicShadowPrimaryInstances.clear();
        gShadowRendererState.dynamicShadowPrimaryMaterialSources.clear();
        gShadowStaticTraditionalIndirectStream.Clear();
        gShadowDynamicTraditionalIndirectStream.Clear();

        RENDER3D::GPUDRIVEN::GpuDrivenPassSource& staticPass =
            gShadowRendererState.staticShadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        staticPass.Reset();
        staticPass.gpuSceneBaseIndex = 0;
        staticPass.preferredBackend =
            RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
        staticPass.clusterEligible = sourcePass->clusterEligible;

        RENDER3D::GPUDRIVEN::GpuDrivenPassSource& dynamicPass =
            gShadowRendererState.dynamicShadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        dynamicPass.Reset();
        dynamicPass.gpuSceneBaseIndex = 0;
        dynamicPass.preferredBackend =
            RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
        dynamicPass.clusterEligible = sourcePass->clusterEligible;

        if (sourcePass->instances != nullptr) {
            const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sourceMaterials =
                sourcePass->materialSources;
            for (size_t sourceIndex = 0;
                sourceIndex < sourcePass->instances->size();
                ++sourceIndex) {

                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    (*sourcePass->instances)[sourceIndex];
                if (!IsPrimaryShadowCaster(instance)) {
                    continue;
                }

                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* material =
                    sourceMaterials != nullptr &&
                        sourceIndex < sourceMaterials->size()
                    ? &(*sourceMaterials)[sourceIndex]
                    : nullptr;
                if (IsStaticPrimaryShadowCaster(instance)) {
                    AppendShadowPrimaryInstance(
                        instance,
                        material,
                        gShadowRendererState.staticShadowPrimaryInstances,
                        gShadowRendererState.staticShadowPrimaryMaterialSources);
                } else {
                    AppendShadowPrimaryInstance(
                        instance,
                        material,
                        gShadowRendererState.dynamicShadowPrimaryInstances,
                        gShadowRendererState.dynamicShadowPrimaryMaterialSources);
                }
            }
        }

        if (!gShadowRendererState.staticShadowPrimaryInstances.empty()) {
            staticPass.instances = &gShadowRendererState.staticShadowPrimaryInstances;
            staticPass.materialSources = &gShadowRendererState.staticShadowPrimaryMaterialSources;
            staticPass.gpuSceneInstanceCount =
                static_cast<uint32_t>((std::min)(
                    gShadowRendererState.staticShadowPrimaryInstances.size(),
                    static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        if (!gShadowRendererState.dynamicShadowPrimaryInstances.empty()) {
            dynamicPass.instances = &gShadowRendererState.dynamicShadowPrimaryInstances;
            dynamicPass.materialSources = &gShadowRendererState.dynamicShadowPrimaryMaterialSources;
            dynamicPass.gpuSceneInstanceCount =
                static_cast<uint32_t>((std::min)(
                    gShadowRendererState.dynamicShadowPrimaryInstances.size(),
                    static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        const size_t staticTraditionalCount =
            static_cast<size_t>(sourcePass->traditionalIndirect.staticCommandCount);
        if (staticTraditionalCount != 0u &&
            gShadowStaticTraditionalIndirectStream.CopyRangeFrom(
                sourcePass->traditionalIndirect,
                0u,
                staticTraditionalCount,
                false)) {
            HydrateShadowTraditionalIndirectStream(
                gShadowStaticTraditionalIndirectStream);
            gShadowStaticTraditionalIndirectStream.gpuSceneBaseIndex =
                staticPass.gpuSceneInstanceCount;
            gShadowStaticTraditionalIndirectStream.AttachTo(staticPass);
        }
        if (staticPass.gpuSceneInstanceCount == 0u &&
            staticPass.traditionalIndirect.HasCommands()) {
            staticPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
        }

        gShadowRendererState.staticShadowSceneSource.sourceInstanceCount =
            staticPass.gpuSceneInstanceCount +
            staticPass.traditionalIndirect.gpuSceneInstanceCount;
        gShadowRendererState.staticShadowSceneSource.layoutVersion =
            BuildShadowSourceLayoutHash(staticPass);
        gShadowRendererState.staticShadowSceneSource.sourceVersion =
            BuildShadowSourceContentHash(staticPass);
        gShadowRendererState.staticShadowSceneSource.dirtyBaseSourceVersion = 0u;

        const size_t skinnedTraditionalCount =
            static_cast<size_t>(sourcePass->traditionalIndirect.skinnedCommandCount);
        if (skinnedTraditionalCount != 0u &&
            gShadowDynamicTraditionalIndirectStream.CopyRangeFrom(
                sourcePass->traditionalIndirect,
                staticTraditionalCount,
                skinnedTraditionalCount,
                true)) {
            HydrateShadowTraditionalIndirectStream(
                gShadowDynamicTraditionalIndirectStream);
            gShadowDynamicTraditionalIndirectStream.gpuSceneBaseIndex =
                dynamicPass.gpuSceneInstanceCount;
            gShadowDynamicTraditionalIndirectStream.AttachTo(dynamicPass);
        }
        if (dynamicPass.gpuSceneInstanceCount == 0u &&
            dynamicPass.traditionalIndirect.HasCommands()) {
            dynamicPass.preferredBackend =
                RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
        }

        gShadowRendererState.dynamicShadowSceneSource.sourceInstanceCount =
            dynamicPass.gpuSceneInstanceCount +
            dynamicPass.traditionalIndirect.gpuSceneInstanceCount;
        gShadowRendererState.dynamicShadowSceneSource.layoutVersion =
            BuildShadowSourceLayoutHash(dynamicPass);
        gShadowRendererState.dynamicShadowSceneSource.sourceVersion =
            BuildShadowSourceContentHash(dynamicPass);
        gShadowRendererState.dynamicShadowSceneSource.dirtyBaseSourceVersion = 0u;

        RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
            gShadowRendererState.shadowSceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        shadowPass = *sourcePass;
        shadowPass.gpuSceneBaseIndex = 0;
        shadowPass.preferredBackend =
            RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
        shadowPass.traditionalIndirect.gpuSceneBaseIndex =
            shadowPass.gpuSceneInstanceCount;

        gShadowRendererState.shadowSceneSource.layoutVersion =
            gShadowRendererState.gpuDrivenSceneSource != nullptr
                ? gShadowRendererState.gpuDrivenSceneSource->layoutVersion
                : 0u;
        gShadowRendererState.shadowSceneSource.sourceVersion =
            gShadowRendererState.gpuDrivenSceneSource != nullptr
                ? gShadowRendererState.gpuDrivenSceneSource->sourceVersion
                : 0u;
        gShadowRendererState.shadowSceneSource.dirtyBaseSourceVersion =
            gShadowRendererState.gpuDrivenSceneSource != nullptr
                ? gShadowRendererState.gpuDrivenSceneSource->dirtyBaseSourceVersion
                : 0u;
        gShadowRendererState.shadowSceneSource.sourceInstanceCount =
            shadowPass.gpuSceneInstanceCount +
            shadowPass.traditionalIndirect.gpuSceneInstanceCount;
        gShadowRendererState.shadowSceneSource.meshShaderJointPalettes =
            gShadowRendererState.gpuDrivenSceneSource->meshShaderJointPalettes;

        gShadowRendererState.frameHasStaticShadowWork =
            gShadowRendererState.staticShadowSceneSource.sourceInstanceCount != 0u;
        gShadowRendererState.frameHasDynamicShadowWork =
            gShadowRendererState.dynamicShadowSceneSource.sourceInstanceCount != 0u;
        gShadowRendererState.debugStats.shadowStaticSourceInstanceCount =
            gShadowRendererState.staticShadowSceneSource.sourceInstanceCount;
        gShadowRendererState.debugStats.shadowDynamicSourceInstanceCount =
            gShadowRendererState.dynamicShadowSceneSource.sourceInstanceCount;

        gShadowRendererState.shadowSourceCache.Capture(*gShadowRendererState.gpuDrivenSceneSource);
        return gShadowRendererState.shadowSceneSource.sourceInstanceCount != 0;
    }

    void SyncShadowGpuDrivenBackendAvailability() {
        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gShadowRendererState.meshletRenderBackend.GetStats();
        availability.meshShaderForwardPipelineReady =
            meshletStats.shadowPipelineReady;
        availability.traditionalIndirectPipelineReady =
            gShadowRendererState.rootSig != nullptr &&
            gShadowRendererState.skinnedRootSig != nullptr &&
            gShadowRendererState.staticPso != nullptr &&
            gShadowRendererState.skinnedPso != nullptr;
        gShadowRendererState.gpuDrivenLayer.SetBackendAvailability(availability);
    }

} // namespace HIKARI::SHADOW::INTERNAL
