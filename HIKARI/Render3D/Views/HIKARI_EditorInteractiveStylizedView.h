#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <array>
#include <cstdint>
#include <memory>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Features/HIKARI_RenderFeatureTypes.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"

namespace HIKARI {
class Camera3D;
class RenderTarget2D;
struct SceneEnvironment;
} // namespace HIKARI

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D::SURFACE {
struct SceneSurfaceSet;
}

namespace HIKARI::RENDER3D::LIGHTING {
struct SceneLightingSet;
}

namespace HIKARI::RENDER3D::EDITORVIEW {

struct EditorInteractiveStylizedPreparation {
    FEATURES::RenderFeatureRequestSet requests{};
    bool featureMaskBackendReady = false;
    bool geometryOutlineBackendReady = false;
    bool visibilityOverlayBackendReady = false;
    bool writeFeatureMask = false;
    bool renderGeometryOutline = false;
    bool renderVisibilityOverlay = false;
};

class EditorInteractiveStylizedView final {
public:
    EditorInteractiveStylizedView() = default;
    ~EditorInteractiveStylizedView();

    EditorInteractiveStylizedView(const EditorInteractiveStylizedView&) = delete;
    EditorInteractiveStylizedView& operator=(const EditorInteractiveStylizedView&) = delete;

    EditorInteractiveStylizedPreparation Prepare(const GPUDRIVEN::GpuDrivenSceneSource& sceneSource) const;
    SURFACE::SceneSurfaceBuffer& GetSurfaceBuffer() noexcept { return surfaceBuffer_; }
    const FEATURES::RenderFeaturePlan& GetPlan() const noexcept { return plan_; }

    bool Resolve(RenderTarget2D& sceneTarget, uint64_t targetGeneration, RenderViewId viewId, const Camera3D& camera,
                 const SceneEnvironment& environment, uint32_t width, uint32_t height,
                 const EditorInteractiveStylizedPreparation& preparation, RenderTarget2D*& outDisplaySource);
    void Shutdown();

private:
    bool EnsureCompositeTarget(uint32_t width, uint32_t height);
    bool PublishSceneSurfaces(RenderTarget2D& sceneTarget, uint64_t targetGeneration, RenderViewId viewId,
                              SURFACE::SceneSurfaceSet& outSurfaces);

    std::unique_ptr<RenderTarget2D> compositeTarget_{};
    SURFACE::SceneSurfaceBuffer surfaceBuffer_{};
    FEATURES::RenderFeaturePlan plan_{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> sceneColorSrvs_{};
    std::array<RenderResourceView, GFX::kFrameResourceCount> sceneDepthSrvs_{};
    std::array<uint64_t, GFX::kFrameResourceCount> sceneSrvGenerations_{};
    GFX::Context lastGraphicsContext_{};
};

void ShutdownEditorInteractiveStylizedSharedResources();

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
