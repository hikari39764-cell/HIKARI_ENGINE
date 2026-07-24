#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <string>

#include "HIKARI_3D.h"
#include "HIKARI_Services.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {

    bool DocumentSceneBakeCoordinator::RenderProbeCaptureFace(
        DocumentSceneBase& scene,
        TOOLS::BAKING::ProbeCubemapCaptureTarget& captureTarget,
        const Camera3D& faceCamera,
        const SceneEnvironment& captureEnvironment,
        uint32_t faceIndex,
        ProbeCaptureKind kind) {

        if (!captureTarget.BeginFace(
                faceIndex,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
                1.0f)) {
            return false;
        }

        const bool isLightProbe = kind == ProbeCaptureKind::LightProbe;
        const std::string eventName =
            std::string(
                isLightProbe
                    ? "LightProbe.CaptureFace"
                    : "ReflectionProbe.CaptureFace") +
            DOCUMENT_SCENE_BAKING::ProbeFaceName(faceIndex);
        GFX::PIX::ScopedGpuEvent pixFace(
            SERVICES::gCtx.cmdList,
            GFX::PIX::kColorRender,
            eventName.c_str());

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

            SKYRENDERER::Render(
                faceCamera,
                captureEnvironment,
                scene.state_->assets.Models(),
                scene.state_->lighting.sky);
            MODELRENDERER::RenderOpaqueForReflectionProbeCapture(
                faceCamera,
                captureEnvironment,
                captureTarget.GetResolution(),
                captureTarget.GetResolution(),
                isLightProbe
                    ? MODELRENDERER::ModelRendererFrameKind::LightProbeCapture
                    : MODELRENDERER::ModelRendererFrameKind::ReflectionProbeCapture);
        };

        REFLECTION::ScopedReflectionProbeSamplingSuppress
            reflectionSuppress{};
        if (isLightProbe) {
            RENDER3D::LIGHTPROBE::ScopedLightProbeVolumeSamplingSuppress
                lightProbeSuppress{};
            renderFace();
        } else {
            renderFace();
        }

        captureTarget.EndFace(faceIndex);
        return true;
    }

} // namespace HIKARI
