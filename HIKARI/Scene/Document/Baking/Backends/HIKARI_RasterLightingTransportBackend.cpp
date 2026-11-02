#include "Scene/Document/Baking/Backends/HIKARI_RasterLightingTransportBackend.h"

#include <string>

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"
#include "Tools/Baking/HIKARI_ProbeCubemapCaptureTarget.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {

namespace {

class ScopedRenderSubmissionPurpose final {
public:
    explicit ScopedRenderSubmissionPurpose(RENDER3D::RenderViewPurpose purpose)
        : previous_(RenderSubmissionSystem::GetRenderViewPurpose()) {
        RenderSubmissionSystem::SetRenderViewPurpose(purpose);
    }

    ~ScopedRenderSubmissionPurpose() { RenderSubmissionSystem::SetRenderViewPurpose(previous_); }

    ScopedRenderSubmissionPurpose(const ScopedRenderSubmissionPurpose&) = delete;
    ScopedRenderSubmissionPurpose& operator=(const ScopedRenderSubmissionPurpose&) = delete;

private:
    RENDER3D::RenderViewPurpose previous_ = RENDER3D::RenderViewPurpose::Game;
};

} // namespace

bool RasterLightingTransportBackend::RenderProbeFace(DocumentSceneBase& scene,
                                                     TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
                                                     const Camera3D& faceCamera,
                                                     const SceneEnvironment& captureEnvironment,
                                                     uint32_t faceIndex) const {
    ScopedRenderSubmissionPurpose submissionPurpose{RENDER3D::RenderViewPurpose::ReflectionProbeCapture};
    const std::string eventName = std::string("ReflectionProbe.CaptureFace") + ProbeFaceName(faceIndex);
    GFX::PIX::ScopedGpuEvent pixFace(GFX::GetCurrentContext().cmdList, GFX::PIX::kColorRender, eventName.c_str());

    bool faceBegun = false;
    const RENDER3D::LIGHTING::SceneLightingSet lighting =
        LIGHTING_AUTHORING::BuildRuntimeSceneLightingSet(scene.GetSceneDocument(), scene.GetWorld());

    const auto renderFace = [&]() {
        RENDERER3D::DEBUG::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        RenderSubmissionSystem::SetAssetContext(&scene.state_->assets.Registry(),
                                                scene.state_->assets.Database().GetProjectRoot(),
                                                &scene.state_->assets.Models());
        scene.state_->runtime.systemScheduler.PreRender(scene.state_->runtime.world, frame);

        const GFX::Context graphics = GFX::GetCurrentContext();
        SHADOW::BeginFrame(graphics, captureEnvironment, faceCamera);
        SHADOW::RenderDirectionalShadowMap(graphics);
        if (!captureTarget.BeginFace(faceIndex, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f)) {
            return false;
        }
        faceBegun = true;
        SKYRENDERER::Render(graphics, faceCamera, captureEnvironment, scene.state_->lighting.sky);
        MODELRENDERER::RenderOpaqueForReflectionProbeCapture(graphics, faceCamera, captureEnvironment, lighting,
                                                             captureTarget.GetResolution(),
                                                             captureTarget.GetResolution());
        return true;
    };

    REFLECTION::ScopedReflectionProbeSamplingSuppress reflectionSuppress{};
    const bool rendered = renderFace();

    if (faceBegun) {
        captureTarget.EndFace(faceIndex);
    }
    return rendered;
}

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
