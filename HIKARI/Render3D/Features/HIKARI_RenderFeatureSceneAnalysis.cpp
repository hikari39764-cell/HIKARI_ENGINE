#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"

#include <algorithm>
#include <array>
#include <unordered_set>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

namespace HIKARI::RENDER3D::FEATURES {

namespace {
uint32_t ResolveRequestMask(uint32_t flags) noexcept {
    using namespace MATERIAL::GPU_MATERIAL_PROGRAM_FLAGS;
    uint32_t mask = 0u;
    if ((flags & (RenderFeatureGeometryOutline | EditorSelectionOutline)) != 0u) {
        mask |= RenderFeatureBit(RenderFeatureKind::GeometryOutline);
    }
    if ((flags & RenderFeatureScreenOutline) != 0u) {
        mask |= RenderFeatureBit(RenderFeatureKind::ScreenOutline);
    }
    if ((flags & RenderFeatureVisibilityOverlay) != 0u) {
        mask |= RenderFeatureBit(RenderFeatureKind::VisibilityOverlay);
    }
    if ((flags & RenderFeatureFaceHairShadow) != 0u) {
        mask |= RenderFeatureBit(RenderFeatureKind::FaceHairShadow);
    }
    return mask;
}

uint64_t BuildSurfaceKey(const RUNTIME::SurfaceGpuSceneMaterialSource& source) noexcept {
    return (static_cast<uint64_t>(source.sourceRecordIndex) << 32u) |
           static_cast<uint64_t>(source.sourceSurfaceInstanceIndex);
}
} // namespace

RenderFeatureRequestSet AnalyzeRenderFeatureRequests(const GPUDRIVEN::GpuDrivenSceneSource& source) {
    // 面の材質の種類は並び（組と経路）の作り直しでしか変わらないので、同じ場景の同じ並びなら前の結果を返す。
    static const GPUDRIVEN::GpuDrivenSceneSource* cachedSource = nullptr;
    static uint64_t cachedLayout = 0u;
    static RenderFeatureRequestSet cached{};
    if (cachedSource == &source && cachedLayout == source.layoutVersion && source.layoutVersion != 0u) {
        return cached;
    }
    RenderFeatureRequestSet requests{};
    std::unordered_set<uint64_t> visited{};
    constexpr std::array passes{
        GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
        GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
        GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
    };
    for (GPUDRIVEN::GpuDrivenPassKind passKind : passes) {
        const GPUDRIVEN::GpuDrivenPassSource& pass = source.GetPass(passKind);
        if (pass.materialSources == nullptr) {
            continue;
        }
        for (const RUNTIME::SurfaceGpuSceneMaterialSource& materialSource : *pass.materialSources) {
            if (!visited.emplace(BuildSurfaceKey(materialSource)).second) {
                continue;
            }
            const uint32_t mask = ResolveRequestMask(materialSource.materialProgramFlags);
            requests.requestedMask |= mask;
            if (passKind == GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque) {
                requests.opaqueSurfaceRequestedMask |= mask;
            }
            requests.postGroupMask |= materialSource.postGroupMask;
            if (materialSource.postGroupMask != 0u) {
                ++requests.isolatedSurfaceCount;
            }
            for (uint32_t index = 0u; index < static_cast<uint32_t>(RenderFeatureKind::Count); ++index) {
                if ((mask & (1u << index)) != 0u) {
                    ++requests.surfaceCounts[index];
                }
            }
        }
    }
    cachedSource = &source;
    cachedLayout = source.layoutVersion;
    cached = requests;
    return requests;
}

} // namespace HIKARI::RENDER3D::FEATURES
