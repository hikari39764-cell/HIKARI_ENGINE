#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_OwnedTraditionalIndirectStream.h"

namespace HIKARI::SHADOW::INTERNAL {

using OwnedShadowTraditionalIndirectStream = RENDER3D::GPUDRIVEN::OwnedTraditionalIndirectStream;

void ClearShadowTraditionalIndirectStreams(ShadowRendererState& state) {
    state.staticTraditionalIndirectStream.Clear();
    state.dynamicTraditionalIndirectStream.Clear();
}

Mesh* GetOrCreatePrimitiveMesh(ShadowRendererState& state, const GFX::Context& graphics,
                               const MeshPrimitive& primitive) {
    auto found = state.primitiveMeshCache.find(&primitive);
    if (found != state.primitiveMeshCache.end()) {
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
        if (MATH::Length({dst.tangent.x, dst.tangent.y, dst.tangent.z}) <= 1e-6f) {
            dst.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        }
        dst.u = src.uv0.x;
        dst.v = src.uv0.y;
        dst.uv1 = src.uv1;
        vertices.push_back(dst);
    }

    auto mesh = std::make_unique<Mesh>();
    if (!mesh->CreateStatic(graphics.device, vertices, primitive.indices)) {
        return nullptr;
    }
    Mesh* raw = mesh.get();
    state.primitiveMeshCache.emplace(&primitive, std::move(mesh));
    return raw;
}

Mesh* GetOrCreateSkinnedPrimitiveMesh(ShadowRendererState& state, const GFX::Context& graphics,
                                      const MeshPrimitive& primitive) {
    auto found = state.primitiveSkinnedMeshCache.find(&primitive);
    if (found != state.primitiveSkinnedMeshCache.end()) {
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
    if (!mesh->CreateSkinned(graphics.device, vertices, primitive.indices)) {
        return nullptr;
    }
    Mesh* raw = mesh.get();
    state.primitiveSkinnedMeshCache.emplace(&primitive, std::move(mesh));
    return raw;
}

const MeshPrimitive* ResolveShadowTraditionalRecordPrimitive(const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record) {
    if (record.model == nullptr || record.meshIndex >= record.model->meshes.size()) {
        return nullptr;
    }

    const MeshAsset& meshAsset = record.model->meshes[record.meshIndex];
    if (record.primitiveIndex >= meshAsset.primitives.size()) {
        return nullptr;
    }

    return &meshAsset.primitives[record.primitiveIndex];
}

bool HydrateShadowTraditionalIndirectStream(ShadowRendererState& state, const GFX::Context& graphics,
                                            OwnedShadowTraditionalIndirectStream& stream) {
    if (stream.records.empty() || stream.executableRecordIndices.empty() || stream.commands.empty()) {
        return true;
    }

    for (RENDER3D::RUNTIME::SurfaceDrawCommand& command : stream.commands) {
        command.triangleMeshView = {};
        command.jointPaletteGpuAddress = 0;
        if (command.recordCount == 0 || command.firstExecutableIndex >= stream.executableRecordIndices.size()) {
            continue;
        }

        const uint32_t recordIndex = stream.executableRecordIndices[command.firstExecutableIndex];
        if (recordIndex >= stream.records.size()) {
            continue;
        }

        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = stream.records[recordIndex];
        const MeshPrimitive* primitive = ResolveShadowTraditionalRecordPrimitive(record);
        if (primitive == nullptr) {
            continue;
        }

        const bool hasJointPalette = command.firstRecordIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex &&
                                     command.firstRecordIndex < stream.jointPalettes.size() &&
                                     !stream.jointPalettes[command.firstRecordIndex].empty();
        const bool skinnedCommand = record.skinned && hasJointPalette;
        Mesh* mesh = skinnedCommand ? GetOrCreateSkinnedPrimitiveMesh(state, graphics, *primitive)
                                    : GetOrCreatePrimitiveMesh(state, graphics, *primitive);
        if (mesh == nullptr || !mesh->IsValid()) {
            continue;
        }

        command.triangleMeshView.vertexBuffer = mesh->GetVBView();
        command.triangleMeshView.indexBuffer = mesh->GetIBView();
        if (!command.HasTriangleMeshGpuView() || !skinnedCommand) {
            continue;
        }

        command.jointPaletteGpuAddress =
            MESHRENDERER::UploadJointPalettes(graphics, GetActiveShadowFrameResources(state).frameUploads,
                                              &stream.jointPalettes[command.firstRecordIndex], 1u);
        if (command.jointPaletteGpuAddress == 0u)
            return false;
    }
    return true;
}

bool PrepareShadowDeformation(ShadowRendererState& state, const GFX::Context& graphics) {
    ShadowFrameResources& frame = GetActiveShadowFrameResources(state);
    frame.meshJointPaletteAddress = 0u;
    const auto* source = state.gpuDrivenSceneSource;
    if (source == nullptr)
        return false;
    frame.meshJointPaletteAddress =
        RENDER3D::GPUDRIVEN::UploadGpuSceneJointPaletteSnapshot(graphics, source->meshShaderJointPalettes);
    return frame.meshJointPaletteAddress != 0u &&
           HydrateShadowTraditionalIndirectStream(state, graphics, state.staticTraditionalIndirectStream) &&
           HydrateShadowTraditionalIndirectStream(state, graphics, state.dynamicTraditionalIndirectStream);
}

const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* GetSourceShadowPass(ShadowRendererState& state) {
    if (state.gpuDrivenSceneSource == nullptr) {
        return nullptr;
    }
    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
        state.gpuDrivenSceneSource->GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    return pass.HasGpuSceneInstances() ? &pass : nullptr;
}

namespace {
bool PublishShadowSceneSources(ShadowRendererState& state, const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& sourcePass) {
    RENDER3D::GPUDRIVEN::GpuDrivenPassSource& shadowPass =
        state.shadowSceneSource.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    shadowPass = sourcePass;
    shadowPass.gpuSceneBaseIndex = 0;
    shadowPass.preferredBackend = RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
    shadowPass.traditionalIndirect.gpuSceneBaseIndex = shadowPass.gpuSceneInstanceCount;

    state.shadowSceneSource.layoutVersion =
        state.gpuDrivenSceneSource != nullptr ? state.gpuDrivenSceneSource->layoutVersion : 0u;
    state.shadowSceneSource.sourceVersion =
        state.gpuDrivenSceneSource != nullptr ? state.gpuDrivenSceneSource->sourceVersion : 0u;
    state.shadowSceneSource.dirtyBaseSourceVersion =
        state.gpuDrivenSceneSource != nullptr ? state.gpuDrivenSceneSource->dirtyBaseSourceVersion : 0u;
    state.shadowSceneSource.sourceInstanceCount =
        shadowPass.gpuSceneInstanceCount + shadowPass.traditionalIndirect.gpuSceneInstanceCount;
    for (auto* partition :
         {&state.shadowSceneSource, &state.staticShadowSceneSource, &state.dynamicShadowSceneSource}) {
        partition->meshShaderJointPalettes = state.gpuDrivenSceneSource->meshShaderJointPalettes;
        partition->waterSurfaceGpuData = state.gpuDrivenSceneSource->waterSurfaceGpuData;
        partition->terrainSurfaceGpuData = state.gpuDrivenSceneSource->terrainSurfaceGpuData;
        partition->terrainSurfaceHeights = state.gpuDrivenSceneSource->terrainSurfaceHeights;
        partition->terrainSurfaceHeightVersion = state.gpuDrivenSceneSource->terrainSurfaceHeightVersion;
    }

    state.frameHasStaticShadowWork = state.staticShadowSceneSource.sourceInstanceCount != 0u;
    state.frameHasDynamicShadowWork = state.dynamicShadowSceneSource.sourceInstanceCount != 0u;
    state.debugStats.shadowStaticSourceInstanceCount = state.staticShadowSceneSource.sourceInstanceCount;
    state.debugStats.shadowDynamicSourceInstanceCount = state.dynamicShadowSceneSource.sourceInstanceCount;

    state.shadowSourceCache.Capture(*state.gpuDrivenSceneSource);
    return state.shadowSceneSource.sourceInstanceCount != 0;
}

bool TryPatchShadowSceneSources(ShadowRendererState& state,
                                const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& sourcePass) {
    if (!state.shadowSourceCache.CanPatch(state.gpuDrivenSceneSource) || sourcePass.instances == nullptr ||
        sourcePass.gpuSceneBaseIndex != 0u || sourcePass.instances->size() != state.shadowPrimaryLocations.size() ||
        sourcePass.traditionalIndirect.HasCommands() || sourcePass.traditionalIndirect.gpuSceneInstanceCount != 0u ||
        (sourcePass.materialSources != nullptr && sourcePass.materialSources->size() != sourcePass.instances->size()))
        return false;

    const auto materialAt = [&](size_t index) {
        return sourcePass.materialSources != nullptr ? &(*sourcePass.materialSources)[index] : nullptr;
    };
    // 書き換え前に全 dirty range と partition の不変条件を確認する。
    for (const auto& range : sourcePass.dirtyRanges) {
        const size_t end = static_cast<size_t>(range.firstInstance) + range.instanceCount;
        if (end > state.shadowPrimaryLocations.size())
            return false;
        for (size_t i = range.firstInstance; i < end; ++i) {
            const auto& instance = (*sourcePass.instances)[i];
            const auto& location = state.shadowPrimaryLocations[i];
            const bool caster = IsPrimaryShadowCaster(instance);
            if (caster != (location.localIndex != RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex) ||
                (caster && location.staticCaster != IsStaticPrimaryShadowCaster(instance, materialAt(i))))
                return false;
        }
    }

    bool staticChanged = false;
    bool dynamicChanged = false;
    for (const auto& range : sourcePass.dirtyRanges) {
        const size_t end = static_cast<size_t>(range.firstInstance) + range.instanceCount;
        for (size_t i = range.firstInstance; i < end; ++i) {
            const auto& location = state.shadowPrimaryLocations[i];
            if (location.localIndex == RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex)
                continue;
            auto& instances =
                location.staticCaster ? state.staticShadowPrimaryInstances : state.dynamicShadowPrimaryInstances;
            auto& materials = location.staticCaster ? state.staticShadowPrimaryMaterialSources
                                                    : state.dynamicShadowPrimaryMaterialSources;
            const bool changed =
                UpdateShadowPrimaryInstance((*sourcePass.instances)[i], materialAt(i), location.localIndex,
                                            instances[location.localIndex], materials[location.localIndex]);
            (location.staticCaster ? staticChanged : dynamicChanged) |= changed;
            ++state.debugStats.shadowSourceUpdatedInstanceCount;
        }
    }
    const auto updateVersion = [&](RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& partition, bool changed) {
        if (!changed)
            return;
        const auto& pass = partition.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
        partition.layoutVersion = BuildShadowSourceLayoutHash(pass);
        partition.sourceVersion = BuildShadowSourceContentHash(pass);
        partition.dirtyBaseSourceVersion = 0u;
    };
    updateVersion(state.staticShadowSceneSource, staticChanged);
    updateVersion(state.dynamicShadowSceneSource, dynamicChanged);
    ++state.debugStats.shadowSourcePatchCount;
    return true;
}
} // namespace

bool BuildShadowGpuDrivenSceneSources(ShadowRendererState& state) {
    CPU_PROFILE::ScopedCpuTimer cpuTimer(CPU_PROFILE::Pass::ShadowSourceSync);

    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource* sourcePass = GetSourceShadowPass(state);
    if (sourcePass == nullptr || !sourcePass->HasGpuSceneInstances()) {
        state.shadowSceneSource.Reset();
        state.staticShadowSceneSource.Reset();
        state.dynamicShadowSceneSource.Reset();
        state.activeShadowSceneSource.Reset();
        state.staticShadowPrimaryInstances.clear();
        state.staticShadowPrimaryMaterialSources.clear();
        state.dynamicShadowPrimaryInstances.clear();
        state.dynamicShadowPrimaryMaterialSources.clear();
        state.staticTraditionalIndirectStream.Clear();
        state.dynamicTraditionalIndirectStream.Clear();
        InvalidateShadowSourceCache(state);
        return false;
    }

    if (CanReuseShadowSourceCache(state)) {
        ++state.debugStats.shadowSourceReuseCount;
        return PublishShadowSceneSources(state, *sourcePass);
    }
    if (TryPatchShadowSceneSources(state, *sourcePass))
        return PublishShadowSceneSources(state, *sourcePass);

    ++state.debugStats.shadowSourceFullSyncCount;
    state.shadowPrimaryLocations.assign(sourcePass->instances != nullptr ? sourcePass->instances->size() : 0u, {});
    state.shadowSceneSource.Reset();
    state.staticShadowSceneSource.Reset();
    state.dynamicShadowSceneSource.Reset();
    state.activeShadowSceneSource.Reset();
    state.staticShadowPrimaryInstances.clear();
    state.staticShadowPrimaryMaterialSources.clear();
    state.dynamicShadowPrimaryInstances.clear();
    state.dynamicShadowPrimaryMaterialSources.clear();
    state.staticTraditionalIndirectStream.Clear();
    state.dynamicTraditionalIndirectStream.Clear();

    RENDER3D::GPUDRIVEN::GpuDrivenPassSource& staticPass =
        state.staticShadowSceneSource.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    staticPass.Reset();
    staticPass.gpuSceneBaseIndex = 0;
    staticPass.preferredBackend = RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
    staticPass.clusterEligible = sourcePass->clusterEligible;

    RENDER3D::GPUDRIVEN::GpuDrivenPassSource& dynamicPass =
        state.dynamicShadowSceneSource.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    dynamicPass.Reset();
    dynamicPass.gpuSceneBaseIndex = 0;
    dynamicPass.preferredBackend = RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::MeshShader;
    dynamicPass.clusterEligible = sourcePass->clusterEligible;

    if (sourcePass->instances != nullptr) {
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource>* sourceMaterials =
            sourcePass->materialSources;
        for (size_t sourceIndex = 0; sourceIndex < sourcePass->instances->size(); ++sourceIndex) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance = (*sourcePass->instances)[sourceIndex];
            if (!IsPrimaryShadowCaster(instance)) {
                continue;
            }

            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* material =
                sourceMaterials != nullptr && sourceIndex < sourceMaterials->size() ? &(*sourceMaterials)[sourceIndex]
                                                                                    : nullptr;
            const bool staticCaster = IsStaticPrimaryShadowCaster(instance, material);
            state.shadowPrimaryLocations[sourceIndex] = {
                static_cast<uint32_t>(staticCaster ? state.staticShadowPrimaryInstances.size()
                                                   : state.dynamicShadowPrimaryInstances.size()),
                staticCaster};
            ++state.debugStats.shadowSourceUpdatedInstanceCount;
            if (staticCaster) {
                AppendShadowPrimaryInstance(instance, material, state.staticShadowPrimaryInstances,
                                            state.staticShadowPrimaryMaterialSources);
            } else {
                AppendShadowPrimaryInstance(instance, material, state.dynamicShadowPrimaryInstances,
                                            state.dynamicShadowPrimaryMaterialSources);
            }
        }
    }

    if (!state.staticShadowPrimaryInstances.empty()) {
        staticPass.instances = &state.staticShadowPrimaryInstances;
        staticPass.materialSources = &state.staticShadowPrimaryMaterialSources;
        staticPass.gpuSceneInstanceCount = static_cast<uint32_t>((std::min)(
            state.staticShadowPrimaryInstances.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
    }

    if (!state.dynamicShadowPrimaryInstances.empty()) {
        dynamicPass.instances = &state.dynamicShadowPrimaryInstances;
        dynamicPass.materialSources = &state.dynamicShadowPrimaryMaterialSources;
        dynamicPass.gpuSceneInstanceCount = static_cast<uint32_t>((std::min)(
            state.dynamicShadowPrimaryInstances.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
    }

    const size_t staticTraditionalCount = static_cast<size_t>(sourcePass->traditionalIndirect.staticCommandCount);
    if (staticTraditionalCount != 0u &&
        state.staticTraditionalIndirectStream.CopyRangeFrom(sourcePass->traditionalIndirect, 0u, staticTraditionalCount,
                                                            &IsStaticPrimaryShadowCaster)) {
        state.staticTraditionalIndirectStream.gpuSceneBaseIndex = staticPass.gpuSceneInstanceCount;
        state.staticTraditionalIndirectStream.AttachTo(staticPass);
    }
    if (staticPass.gpuSceneInstanceCount == 0u && staticPass.traditionalIndirect.HasCommands()) {
        staticPass.preferredBackend = RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
    }

    state.staticShadowSceneSource.sourceInstanceCount =
        staticPass.gpuSceneInstanceCount + staticPass.traditionalIndirect.gpuSceneInstanceCount;
    state.staticShadowSceneSource.layoutVersion = BuildShadowSourceLayoutHash(staticPass);
    state.staticShadowSceneSource.sourceVersion = BuildShadowSourceContentHash(staticPass);
    state.staticShadowSceneSource.dirtyBaseSourceVersion = 0u;

    const size_t totalTraditionalCount =
        staticTraditionalCount + static_cast<size_t>(sourcePass->traditionalIndirect.skinnedCommandCount);
    if (totalTraditionalCount != 0u && state.dynamicTraditionalIndirectStream.CopyRangeFrom(
                                           sourcePass->traditionalIndirect, 0u, totalTraditionalCount,
                                           [](const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance,
                                              const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource* material) {
                                               return IsPrimaryShadowCaster(instance) &&
                                                      !IsStaticPrimaryShadowCaster(instance, material);
                                           })) {
        state.dynamicTraditionalIndirectStream.gpuSceneBaseIndex = dynamicPass.gpuSceneInstanceCount;
        state.dynamicTraditionalIndirectStream.AttachTo(dynamicPass);
    }
    if (dynamicPass.gpuSceneInstanceCount == 0u && dynamicPass.traditionalIndirect.HasCommands()) {
        dynamicPass.preferredBackend = RENDER3D::GPUDRIVEN::GpuDrivenBackendKind::TraditionalIndirect;
    }

    state.dynamicShadowSceneSource.sourceInstanceCount =
        dynamicPass.gpuSceneInstanceCount + dynamicPass.traditionalIndirect.gpuSceneInstanceCount;
    state.dynamicShadowSceneSource.layoutVersion = BuildShadowSourceLayoutHash(dynamicPass);
    state.dynamicShadowSceneSource.sourceVersion = BuildShadowSourceContentHash(dynamicPass);
    state.dynamicShadowSceneSource.dirtyBaseSourceVersion = 0u;

    return PublishShadowSceneSources(state, *sourcePass);
}

void SyncShadowGpuDrivenBackendAvailability(ShadowRendererState& state) {
    RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
    const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats = state.meshletRenderBackend.GetStats();
    availability.meshShaderForwardPipelineReady = meshletStats.shadowPipelineReady;
    availability.traditionalIndirectPipelineReady =
        state.rootSig != nullptr && state.skinnedRootSig != nullptr && state.depthPipelines.IsReady();
    state.gpuDrivenLayer.SetBackendAvailability(availability);
}

} // namespace HIKARI::SHADOW::INTERNAL
