#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"

namespace HIKARI::MESHRENDERER {

using namespace INTERNAL;
namespace {
// 包囲（中心と半径）が視錐の外か：包む立方体の 8 隅がそろって同じ側面の外か、そろってカメラの後ろ。
// 同次座標の平面の判定なので、カメラの後ろの隅にもそのまま使える。
bool OutsideView(const MATH::Vec4& bounds, const MATH::Mat4& viewProj) {
    if (!MATH::IsFinite(bounds)) {
        return false;
    }
    const float radius = std::abs(bounds.w) * 1.05f;
    uint32_t outside = 0x1Fu;
    for (uint32_t corner = 0u; corner < 8u && outside != 0u; ++corner) {
        const auto clip = viewProj.TransformPoint({bounds.x + ((corner & 1u) ? radius : -radius),
                                                   bounds.y + ((corner & 2u) ? radius : -radius),
                                                   bounds.z + ((corner & 4u) ? radius : -radius), 1.0f});
        if (!MATH::IsFinite(clip)) {
            return false;
        }
        uint32_t mask = 0u;
        mask |= clip.x < -clip.w ? 1u : 0u;
        mask |= clip.x > clip.w ? 2u : 0u;
        mask |= clip.y < -clip.w ? 4u : 0u;
        mask |= clip.y > clip.w ? 8u : 0u;
        mask |= clip.w <= 0.0f ? 16u : 0u;
        outside &= mask;
    }
    return outside != 0u;
}
} // namespace

bool GatherTemporalVelocityDraws(const GFX::Context& graphics, std::vector<TemporalVelocityDraw>& outDraws,
                                 uint32_t maximumDrawCount, uint32_t maximumSkinnedDrawCount,
                                 const MATH::Mat4* cullViewProj) {
    outDraws.clear();
    uint32_t skinnedDrawCount = 0u;
    const auto appendDraw = [&](TemporalVelocityDraw draw) {
        if (outDraws.size() >= maximumDrawCount || (draw.skinned && skinnedDrawCount >= maximumSkinnedDrawCount)) {
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
        GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);

    // Rigid dynamic surfaces may be owned by the mesh-shader mainline.
    // Expose their triangle source only to the temporal sidecar instead of
    // forcing the complete forward pass through a second geometry pass.
    if (opaquePass.instances != nullptr && opaquePass.materialSources != nullptr) {
        const auto& instances = *opaquePass.instances;
        const auto& materialSources = *opaquePass.materialSources;
        const size_t count = (std::min)(instances.size(), materialSources.size());
        for (size_t index = 0; index < count; ++index) {
            const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance = instances[index];
            const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source = materialSources[index];
            const uint32_t flags = instance.flags;
            // 形もワールド変換も変わらない面はカメラの運動ベクトルで足りる。動く剛体（物理・スクリプト）は描く。
            const uint32_t staticFlags =
                static_cast<uint32_t>(RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticGeometry) |
                static_cast<uint32_t>(RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::StaticTransform);
            if ((flags & static_cast<uint32_t>(RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::PassForwardOpaque)) ==
                    0u ||
                (flags & staticFlags) == staticFlags || source.model == nullptr ||
                instance.meshIndex >= source.model->meshes.size()) {
                continue;
            }

            // 画面に写らない面（テレビの画面の中の舞台など）は運動を描かない。
            if (cullViewProj != nullptr && OutsideView(instance.boundsCenterRadius, *cullViewProj)) {
                continue;
            }
            const MeshAsset& meshAsset = source.model->meshes[instance.meshIndex];
            if (instance.primitiveIndex >= meshAsset.primitives.size()) {
                continue;
            }
            const MeshPrimitive& primitive = meshAsset.primitives[instance.primitiveIndex];
            const bool skinned = primitive.layout == VertexLayoutKind::SkinnedPNTTJW;
            const bool shellFallback = instance.shellSettings.x >= 2.0f && instance.shellSettings.y > 0.0f;
            if (skinned && (!shellFallback ||
                            (flags & static_cast<uint32_t>(
                                         RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::ClusterMainline)) == 0u))
                continue;
            if (primitive.layout != VertexLayoutKind::StaticPNTT && !skinned) {
                continue;
            }
            // Shell は圧縮形状と元三角形の EQUAL 一致を保証できないため、範囲だけを失効処理へ渡す。
            if (shellFallback) {
                TemporalVelocityDraw draw{};
                draw.shellSettings = instance.shellSettings;
                draw.boundsCenterRadius = instance.boundsCenterRadius;
                if (!appendDraw(std::move(draw)))
                    return false;
                continue;
            }
            Mesh* mesh = gMeshRendererState.primitiveCache.GetOrCreateStatic(graphics.device, primitive,
                                                                             &gMeshRendererState.debugStats);
            if (mesh == nullptr || !mesh->IsValid()) {
                continue;
            }

            TemporalVelocityDraw draw{};
            draw.objectId =
                (static_cast<uint64_t>(instance.objectIdHigh) << 32u) | static_cast<uint64_t>(instance.objectIdLow);
            draw.objectLifetimeGeneration = source.objectLifetimeGeneration;
            draw.objectVersion = source.objectVersion;
            draw.sourceSurfaceInstanceIndex = source.sourceSurfaceInstanceIndex;
            draw.world = instance.world;
            draw.vertexBuffer = mesh->GetVBView();
            draw.indexBuffer = mesh->GetIBView();
            draw.indexCount = mesh->GetIndexCount();
            draw.doubleSided =
                (flags & static_cast<uint32_t>(RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::DoubleSided)) != 0u;
            draw.alphaMasked =
                (flags & static_cast<uint32_t>(RENDER3D::RUNTIME::SurfaceGpuSceneInstanceFlags::AlphaMasked)) != 0u;
            if (!appendDraw(std::move(draw))) {
                return false;
            }
        }
    }

    // Skinned records carry the current palette in the traditional
    // sidecar. They are appended separately and deduplicated by object and
    // geometry identity.
    const RENDER3D::GPUDRIVEN::GpuDrivenTraditionalIndirectView stream =
        gMeshRendererState.traditionalIndirectOwner.GetView(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
    if (stream.commands == nullptr || stream.executableRecordIndices == nullptr || stream.records == nullptr ||
        stream.jointPalettes == nullptr) {
        return true;
    }
    for (const RENDER3D::RUNTIME::SurfaceDrawCommand& command : *stream.commands) {
        if (!command.drawArgsValid || !command.HasTriangleMeshGpuView() ||
            command.firstExecutableIndex >= stream.executableRecordIndices->size()) {
            continue;
        }
        const uint32_t recordIndex = (*stream.executableRecordIndices)[command.firstExecutableIndex];
        if (recordIndex >= stream.records->size() || recordIndex >= stream.jointPalettes->size()) {
            continue;
        }
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord& record = (*stream.records)[recordIndex];
        if (!record.skinned || (*stream.jointPalettes)[recordIndex].empty()) {
            continue;
        }

        TemporalVelocityDraw draw{};
        draw.objectId = record.objectId.value;
        draw.objectLifetimeGeneration = record.objectLifetimeGeneration;
        draw.objectVersion = record.objectVersion;
        draw.sourceSurfaceInstanceIndex = record.sourceSurfaceInstanceIndex;
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
        if (stream.instances != nullptr && command.firstGpuSceneInstanceIndex < stream.instances->size()) {
            const auto& instance = (*stream.instances)[command.firstGpuSceneInstanceIndex];
            if (cullViewProj != nullptr && OutsideView(instance.boundsCenterRadius, *cullViewProj)) {
                continue;
            }
            draw.shellSettings = instance.shellSettings;
            draw.boundsCenterRadius = instance.boundsCenterRadius;
        }
        if (!appendDraw(std::move(draw))) {
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::MESHRENDERER
