#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Gfx/HIKARI_GfxContext.h"

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <string>
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/HIKARI_Renderer3D.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {

    namespace {
        class ScopedRenderSubmissionPurpose final {
        public:
            explicit ScopedRenderSubmissionPurpose(
                RENDER3D::RenderViewPurpose purpose)
                : previous_(
                    RenderSubmissionSystem::GetRenderViewPurpose()) {

                RenderSubmissionSystem::SetRenderViewPurpose(purpose);
            }

            ~ScopedRenderSubmissionPurpose() {
                RenderSubmissionSystem::SetRenderViewPurpose(previous_);
            }

            ScopedRenderSubmissionPurpose(
                const ScopedRenderSubmissionPurpose&) = delete;
            ScopedRenderSubmissionPurpose& operator=(
                const ScopedRenderSubmissionPurpose&) = delete;

        private:
            RENDER3D::RenderViewPurpose previous_ =
                RENDER3D::RenderViewPurpose::Game;
        };
    } // namespace

    bool DocumentSceneBakeCoordinator::RenderProbeCaptureFace(
        DocumentSceneBase& scene,
        TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
        const Camera3D& faceCamera,
        const SceneEnvironment& captureEnvironment,
        uint32_t faceIndex,
        ProbeCaptureKind kind) {

        const bool isLightProbe = kind == ProbeCaptureKind::LightProbe;
        ScopedRenderSubmissionPurpose submissionPurpose{
            isLightProbe
                ? RENDER3D::RenderViewPurpose::LightProbeCapture
                : RENDER3D::RenderViewPurpose::ReflectionProbeCapture
        };
        const std::string eventName =
            std::string(
                isLightProbe
                    ? "LightProbe.CaptureFace"
                    : "ReflectionProbe.CaptureFace") +
            DOCUMENT_SCENE_BAKING::ProbeFaceName(faceIndex);
        GFX::PIX::ScopedGpuEvent pixFace(
            GFX::GetCurrentContext().cmdList,
            GFX::PIX::kColorRender,
            eventName.c_str());

        bool faceBegun = false;
        const auto renderFace = [&]() {
            RENDERER3D::Reset();
            MODELRENDERER::Reset();
            SKYRENDERER::Reset();

            const FrameContext& frame = HIKARI::TIME::GetFrameContext();
            RenderSubmissionSystem::SetAssetContext(
                &scene.state_->assets.Registry(),
                scene.state_->assets.Database().GetProjectRoot());
            scene.state_->runtime.systemScheduler.PreRender(
                scene.state_->runtime.world,
                frame);

            const GFX::Context graphics = GFX::GetCurrentContext();
            SHADOW::BeginFrame(
                graphics,
                captureEnvironment,
                faceCamera);
            SHADOW::RenderDirectionalShadowMap(graphics);
            if (!captureTarget.BeginFace(
                    faceIndex,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    1.0f)) {
                return false;
            }
            faceBegun = true;
            SKYRENDERER::Render(
                graphics, faceCamera, captureEnvironment,
                scene.state_->assets.Models(),
                scene.state_->lighting.sky);
            MODELRENDERER::RenderOpaqueForReflectionProbeCapture(
                graphics, faceCamera, captureEnvironment,
                captureTarget.GetResolution(),
                captureTarget.GetResolution(),
                isLightProbe
                    ? MODELRENDERER::ModelRendererFrameKind::LightProbeCapture
                    : MODELRENDERER::ModelRendererFrameKind::ReflectionProbeCapture);
            return true;
        };

        bool rendered = false;
        REFLECTION::ScopedReflectionProbeSamplingSuppress
            reflectionSuppress{};
        if (isLightProbe) {
            RENDER3D::LIGHTPROBE::ScopedLightProbeVolumeSamplingSuppress
                lightProbeSuppress{};
            rendered = renderFace();
        } else {
            rendered = renderFace();
        }

        if (faceBegun) {
            captureTarget.EndFace(faceIndex);
        }
        return rendered;
    }

} // namespace HIKARI
