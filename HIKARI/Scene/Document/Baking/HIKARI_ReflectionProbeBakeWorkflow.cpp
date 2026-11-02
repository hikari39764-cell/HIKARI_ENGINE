#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Backends/HIKARI_RasterLightingTransportBackend.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <string>
#include "Core/HIKARI_Logger.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"

namespace HIKARI {
using namespace DOCUMENT_SCENE_BAKING;

bool DocumentSceneBakeCoordinator::ProcessReflectionProbeBakeJob(DocumentSceneBase& scene) {
    if (!reflectionProbeJob_) {
        return false;
    }
    if (reflectionProbeJob_->state == TOOLS::BAKING::LightingBakeJobState::Failed) {
        queuedBakeItems_.clear();
        reflectionProbeJob_.reset();
        batchItemCount_ = 0u;
        batchCompletedItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = false;
        return false;
    }

    ReflectionProbeJob& job = *reflectionProbeJob_;
    if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
        job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
        job.nextFaceIndex = 0;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
        hasLastReport_ = true;

        const uint32_t resolution = std::clamp(job.request.resolution, 32u, 256u);
        if (!job.captureTarget.Initialize(resolution, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
            AddBakeError(job.report, "Failed to initialize reflection probe capture target.");
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }
        job.report.reflectionProbeCaptureResolution = resolution;
        job.report.reflectionProbeCaptureFormat = "R16G16B16A16_FLOAT";
        job.report.messages.push_back(
            "Capture exclusions: reflection probe sampling suppressed; SSAO/post/debug/VFX helpers excluded.");
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Capturing) {
        if (job.nextFaceIndex >= 6u) {
            job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }

        const SceneEnvironment captureEnvironment = MakeProbeCaptureEnvironment(scene.state_->lighting.environment);

        const uint32_t face = job.nextFaceIndex;
        const Camera3D faceCamera =
            MakeProbeFaceCamera(job.request.position, face, (std::max)(4.0f, job.request.radius));
        const DOCUMENT_SCENE_BAKING::RasterLightingTransportBackend transportBackend{};
        bool captureOk =
            transportBackend.RenderProbeFace(scene, job.captureTarget, faceCamera, captureEnvironment, face);

        std::string readbackMessage{};
        if (captureOk) {
            captureOk = job.captureTarget.QueueReadbackFace(face, &readbackMessage);
        }

        POST::PostSystem::RebindCurrentRenderTarget();
        RENDERER3D::DEBUG::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

        if (!captureOk) {
            AddBakeError(job.report, readbackMessage.empty() ? "Failed to render reflection probe scene capture."
                                                             : readbackMessage);
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return true;
        }

        job.fenceValue = GFX::GetCurrentContext().currentFrameRetireFenceValue;
        job.report.gpuFenceValue = job.fenceValue;
        job.report.reflectionProbeSceneCaptured = true;
        job.report.reflectionProbeUsedSourceOverride = false;
        job.report.reflectionProbeCapturedFaceCount =
            (std::max)(job.report.reflectionProbeCapturedFaceCount, face + 1u);
        ++job.report.reflectionProbeQueuedReadbackFaceCount;
        job.report.messages.push_back(readbackMessage);
        job.report.messages.push_back("Face " + std::string(ProbeFaceName(face)) + " captured.");
        ++job.nextFaceIndex;

        if (job.nextFaceIndex >= 6u) {
            job.report.reflectionProbeCaptured = true;
            job.report.messages.push_back("Reflection probe GPU capture submitted. fence=" +
                                          std::to_string(job.fenceValue));
            job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
        }
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
        HIKARI_LOG_INFO("[LightingBake] reflection probe capture face submitted face=" + std::to_string(face) +
                        " fence=" + std::to_string(job.fenceValue));
        // Capture 中は通常描画で CameraCB を上書きしない。
        return true;
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
        if (!GFX::IsCurrentFenceComplete(job.fenceValue)) {
            lastReport_ = job.report;
            return false;
        }

        job.state = TOOLS::BAKING::LightingBakeJobState::Finalizing;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;

        if (job.report.reflectionProbeCapturedFaceCount != 6u ||
            job.report.reflectionProbeQueuedReadbackFaceCount != 6u) {
            AddBakeError(job.report, "Reflection probe capture did not queue all six faces.");
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }

        std::string saveMessage{};
        if (!job.captureTarget.SaveReadbackToCubemapDds(job.report.reflectionProbeCapturePath, &saveMessage)) {
            AddBakeError(job.report, saveMessage);
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }

        job.report.messages.push_back(saveMessage);

        TOOLS::BAKING::ReflectionProbeBaker baker{};
        const TOOLS::BAKING::ReflectionProbeBakeResult bake =
            baker.FinalizeCapturedProbe(job.request, job.report.reflectionProbeCapturePath);

        job.report.reflectionProbeCaptured = bake.captured;
        job.report.reflectionProbePrefiltered = bake.prefiltered;
        job.report.reflectionProbeCaptureValidated = bake.captureValidated;
        job.report.reflectionProbePrefilterValidated = bake.prefilterValidated;
        job.report.reflectionProbeCapturedFaceCount = bake.capturedFaceCount;
        job.report.reflectionProbeCaptureResolution = bake.captureResolution;
        job.report.reflectionProbeCaptureMipCount = bake.captureMipCount;
        job.report.reflectionProbeCaptureFormat = bake.captureFormat;
        job.report.reflectionProbePrefilteredMipCount = bake.prefilteredMipCount;
        job.report.reflectionProbePrefilteredFormat = bake.prefilteredFormat;
        job.report.reflectionProbeFaceSummaries = bake.faceSummaries;
        job.report.bakeFolderCreated = bake.success;
        job.report.reflectionProbeCapturePath = bake.capturePath;
        job.report.reflectionProbePrefilteredPath = bake.prefilteredPath;
        job.report.reflectionProbeBrdfLutPath = bake.brdfLutPath;
        job.report.messages.insert(job.report.messages.end(), bake.messages.begin(), bake.messages.end());
        job.report.warnings.insert(job.report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
        job.report.errors.insert(job.report.errors.end(), bake.errors.begin(), bake.errors.end());

        if (!bake.success) {
            job.report.success = false;
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }

        ASSETS::LIGHTING::LightingBakeManifest manifest = LoadOrCreateLightingBakeManifest(
            job.report.manifestPath, job.request.projectRoot, job.request.sceneGuid, job.report.warnings);
        ReplaceReflectionProbeRecord(manifest, bake.record);
        manifest.sceneHash = job.request.sceneHash;
        manifest.dependencyHash = job.request.dependencyHash;
        manifest.activeScenario = job.request.scenario;

        std::string manifestMessage{};
        if (!ASSETS::LIGHTING::SaveLightingBakeManifest(job.report.manifestPath, manifest, &manifestMessage)) {
            AddBakeError(job.report, manifestMessage);
            job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            return false;
        }

        job.report.manifestWritten = true;
        job.report.reflectionProbeRecordWritten = true;
        job.report.reflectionProbeRecordCount = static_cast<uint32_t>(manifest.reflectionProbes.size());
        job.report.lightProbeRecordCount = static_cast<uint32_t>(manifest.lightProbes.size());
        job.report.lightmapRecordCount = static_cast<uint32_t>(manifest.lightmaps.size());
        job.report.messages.push_back(manifestMessage);
        job.report.messages.push_back("Reflection probe bake manifest updated: " +
                                      job.report.manifestPath.generic_string());

        scene.RefreshTextureRuntimeByPath(bake.record.captureCubemapPath);
        scene.RefreshTextureRuntimeByPath(bake.record.prefilteredCubemapPath);
        scene.RefreshTextureRuntimeByPath(bake.record.brdfLutPath);
        scene.RefreshLightingRuntime();

        job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
        SetBakeJobState(job.report, job.state);
        ++batchCompletedItemCount_;
        job.report.completedItemCount = batchCompletedItemCount_ + batchReusedItemCount_;
        lastReport_ = job.report;
        HIKARI_LOG_INFO("[LightingBake] reflection probe scene capture finalized scene=" + job.request.sceneGuid +
                        " manifest=" + job.report.manifestPath.generic_string());
        reflectionProbeJob_.reset();
        return StartNextQueuedBake(scene);
    }

    return false;
}

} // namespace HIKARI
