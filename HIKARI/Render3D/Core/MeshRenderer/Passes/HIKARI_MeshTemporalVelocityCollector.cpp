#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <utility>

#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;
    bool GatherTemporalVelocityDraws(
        const GFX::Context& graphics,
        std::vector<TemporalVelocityDraw>& outDraws,
        uint32_t maximumDrawCount,
        uint32_t maximumSkinnedDrawCount) {

        outDraws.clear();
        uint32_t skinnedDrawCount = 0u;
        const auto appendDraw =
            [&](TemporalVelocityDraw draw) {
                if (outDraws.size() >= maximumDrawCount ||
                    (draw.skinned &&
                     skinnedDrawCount >= maximumSkinnedDrawCount)) {
                    outDraws.clear();
                    return false;
                }
                if (draw.skinned) {
                    ++skinnedDrawCount;
                }
                outDraws.push_back(std::move(draw));
                return true;
            };
        const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& opaquePass =
            GetSceneSourcePass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);

        // Rigid dynamic surfaces may be owned by the mesh-shader mainline.
        // Expose their triangle source only to the temporal sidecar instead of
        // forcing the complete forward pass through a second geometry pass.
        if (opaquePass.instances != nullptr &&
            opaquePass.materialSources != nullptr) {
            const auto& instances = *opaquePass.instances;
            const auto& materialSources = *opaquePass.materialSources;
            const size_t count = (std::min)(instances.size(), materialSources.size());
            for (size_t index = 0; index < count; ++index) {
                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    instances[index];
                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                    materialSources[index];
                const uint32_t flags = instance.flags;
                if ((flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::PassForwardOpaque)) == 0u ||
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry)) != 0u ||
                    source.model == nullptr ||
                    instance.meshIndex >= source.model->meshes.size()) {
                    continue;
                }

                const MeshAsset& meshAsset = source.model->meshes[instance.meshIndex];
                if (instance.primitiveIndex >= meshAsset.primitives.size()) {
                    continue;
                }
                const MeshPrimitive& primitive = meshAsset.primitives[instance.primitiveIndex];
                if (primitive.layout != VertexLayoutKind::StaticPNTT) {
                    continue;
                }
                Mesh* mesh = gMeshRendererState.primitiveCache.GetOrCreateStatic(
                    graphics.device,
                    primitive,
                    &gMeshRendererState.debugStats);
                if (mesh == nullptr || !mesh->IsValid()) {
                    continue;
                }

                TemporalVelocityDraw draw{};
                draw.objectId =
                    (static_cast<uint64_t>(instance.objectIdHigh) << 32u) |
                    static_cast<uint64_t>(instance.objectIdLow);
                draw.objectLifetimeGeneration =
                    source.objectLifetimeGeneration;
                draw.objectVersion = source.objectVersion;
                draw.sourceSurfaceInstanceIndex =
                    source.sourceSurfaceInstanceIndex;
                draw.world = instance.world;
                draw.vertexBuffer = mesh->GetVBView();
                draw.indexBuffer = mesh->GetIBView();
                draw.indexCount = mesh->GetIndexCount();
                draw.doubleSided =
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided)) != 0u;
                draw.alphaMasked =
                    (flags & static_cast<uint32_t>(
                        RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::AlphaMasked)) != 0u;
                if (!appendDraw(std::move(draw))) {
                    return false;
                }
            }
        }

        // Skinned records carry the current palette in the traditional
        // sidecar. They are appended separately and deduplicated by object and
        // geometry identity.
        const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView stream =
            gMeshRendererState.traditionalIndirectOwner.GetView(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
        if (stream.commands == nullptr ||
            stream.executableRecordIndices == nullptr ||
            stream.records == nullptr ||
            stream.jointPalettes == nullptr) {
            return true;
        }
        for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command : *stream.commands) {
            if (!command.drawArgsValid ||
                !command.HasTriangleMeshGpuView() ||
                command.firstExecutableIndex >= stream.executableRecordIndices->size()) {
                continue;
            }
            const uint32_t recordIndex =
                (*stream.executableRecordIndices)[command.firstExecutableIndex];
            if (recordIndex >= stream.records->size() ||
                recordIndex >= stream.jointPalettes->size()) {
                continue;
            }
            const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record =
                (*stream.records)[recordIndex];
            if (!record.skinned || (*stream.jointPalettes)[recordIndex].empty()) {
                continue;
            }

            TemporalVelocityDraw draw{};
            draw.objectId = record.objectId.value;
            draw.objectLifetimeGeneration =
                record.objectLifetimeGeneration;
            draw.objectVersion = record.objectVersion;
            draw.sourceSurfaceInstanceIndex =
                record.sourceSurfaceInstanceIndex;
            draw.world = record.drawWorldMatrix;
            draw.vertexBuffer = command.triangleMeshView.vertexBuffer;
            draw.indexBuffer = command.triangleMeshView.indexBuffer;
            draw.indexCount = command.drawArgs.indexCountPerInstance;
            draw.startIndex = command.drawArgs.startIndexLocation;
            draw.baseVertex = command.drawArgs.baseVertexLocation;
            draw.skinned = true;
            draw.doubleSided = record.key.doubleSided;
            draw.alphaMasked = record.key.alphaMasked;
            draw.jointPalette = &(*stream.jointPalettes)[recordIndex];
            if (!appendDraw(std::move(draw))) {
                return false;
            }
        }
        return true;
    }


} // namespace HIKARI::MESHRENDERER
