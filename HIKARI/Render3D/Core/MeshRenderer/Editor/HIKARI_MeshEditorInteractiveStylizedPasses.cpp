#include "Render3D/Core/HIKARI_MeshRenderer.h"

#if defined(HIKARI_WITH_EDITOR)

#include <array>

#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

namespace {
constexpr std::array kMaterialFeaturePasses{
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
};

bool ExecuteEditorMaterialScopedFeaturePasses(const GFX::Context& graphics, const MeshPassResources& passResources,
                                              MeshDrawPassKind drawPass,
                                              RENDER3D::MESHLET::MeshletPipelineKind meshletPipeline,
                                              RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
                                              const MeshFrameBindingOverrides& bindingOverrides) {
    bool hasSource = false;
    bool allSourcesExecuted = true;
    for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind sourcePass : kMaterialFeaturePasses) {
        if (!layer.GetPassExecutionState(sourcePass).hasSource ||
            layer.GetPassExecutionPlan(sourcePass).gpuBackendCount == 0u) {
            continue;
        }
        hasSource = true;
        allSourcesExecuted &= ExecuteEditorInteractiveGeometryBackendPlan(graphics, sourcePass, passResources, drawPass,
                                                                          meshletPipeline, layer, bindingOverrides)
                                  .gpuBackendExecuted;
    }
    return hasSource && allSourcesExecuted;
}
} // namespace

bool ExecuteEditorInteractiveStylizedSurfacePass(const GFX::Context& graphics, uint32_t screenWidth,
                                                 uint32_t screenHeight, const EditorInteractiveRenderSettings& settings,
                                                 const MeshPassResources& passResources,
                                                 RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
                                                 const MeshFrameBindingOverrides& bindingOverrides) {
    if (!settings.writeStylizedFeatureMask) {
        if (settings.stylizedSurfaceBuffer != nullptr) {
            settings.stylizedSurfaceBuffer->DiscardResources();
        }
        return true;
    }
    if (settings.stylizedSurfaceBuffer == nullptr || settings.sceneDsv.ptr == 0u ||
        !settings.stylizedSurfaceBuffer->EnsureSize(graphics, screenWidth, screenHeight, true)) {
        return false;
    }

    settings.stylizedSurfaceBuffer->BeginWrite(graphics.cmdList, settings.sceneDsv);
    const GeometryBackendExecutionResult result = ExecuteEditorInteractiveGeometryBackendPlan(
        graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux, passResources,
        MeshDrawPassKind::GeometryAuxFeatureMask, RENDER3D::MESHLET::MeshletPipelineKind::GeometryAuxFeatureMask, layer,
        bindingOverrides);
    settings.stylizedSurfaceBuffer->EndWrite(graphics.cmdList);

    const bool targetReady = settings.targetAccess.rebind == nullptr || settings.targetAccess.Rebind();
    return result.gpuBackendExecuted && targetReady;
}

bool ExecuteEditorInteractiveStylizedGeometryPasses(const GFX::Context& graphics,
                                                    const EditorInteractiveRenderSettings& settings,
                                                    const MeshPassResources& passResources,
                                                    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
                                                    const MeshFrameBindingOverrides& bindingOverrides) {
    const auto rebindTarget = [&settings]() {
        return settings.targetAccess.rebind == nullptr || settings.targetAccess.Rebind();
    };
    if (settings.renderGeometryOutline) {
        if (!rebindTarget() || !ExecuteEditorMaterialScopedFeaturePasses(
                                   graphics, passResources, MeshDrawPassKind::GeometryOutline,
                                   RENDER3D::MESHLET::MeshletPipelineKind::GeometryOutline, layer, bindingOverrides)) {
            return false;
        }
    }
    if (settings.renderVisibilityOverlay) {
        if (!rebindTarget() ||
            !ExecuteEditorMaterialScopedFeaturePasses(graphics, passResources, MeshDrawPassKind::VisibilityOverlay,
                                                      RENDER3D::MESHLET::MeshletPipelineKind::VisibilityOverlay, layer,
                                                      bindingOverrides)) {
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::MESHRENDERER::INTERNAL

#endif
