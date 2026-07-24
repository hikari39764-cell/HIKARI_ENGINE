#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <filesystem>
#include <algorithm>
#include <string>

#include "HIKARI_Services.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Tools/Baking/HIKARI_LightProbeBaker.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"

namespace HIKARI {
    using namespace DOCUMENT_SCENE_BAKING;

    bool DocumentSceneBakeCoordinator::ProcessLightProbeBakeJob(DocumentSceneBase& scene) {
        if (!lightProbeJob_) {
            return false;
        }

        LightProbeJob& job = *lightProbeJob_;
        LightProbeVolumeSettings settings = job.request.settings;
        ClampLightProbeVolumeSettings(settings);

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
            job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
            job.nextProbeIndex = 0;
            job.nextFaceIndex = 0;
            job.probeCapturePaths.clear();
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            hasLastReport_ = true;

            const uint32_t resolution = NormalizeLightProbeCaptureResolution(settings.captureResolution);
            if (!job.captureTarget.Initialize(resolution, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                AddBakeError(job.report, "Failed to initialize light probe capture target.");
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return false;
            }
            job.report.lightProbeCaptureResolution = resolution;
            job.report.lightProbeProbeCount = GetLightProbeVolumeProbeCount(settings);
            job.report.lightProbeMessages.push_back(
                "Capture exclusions: reflection/light probe sampling suppressed; SSAO/post/debug/VFX helpers excluded.");
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Capturing) {
            const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
            if (job.nextProbeIndex >= probeCount) {
                job.state = TOOLS::BAKING::LightingBakeJobState::ProjectingSH;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
            } else {
                const SceneEnvironment captureEnvironment =
                    MakeProbeCaptureEnvironment(
                        scene.state_->lighting.environment);

                const uint32_t probe = job.nextProbeIndex;
                const uint32_t face = job.nextFaceIndex;
                const Camera3D faceCamera = MakeProbeFaceCamera(
                    GetLightProbePositionByIndex(settings, probe),
                    face,
                    GetLightProbeCaptureFarPlane(settings));
                bool captureOk = RenderProbeCaptureFace(
                    scene,
                    job.captureTarget,
                    faceCamera,
                    captureEnvironment,
                    face,
                    ProbeCaptureKind::LightProbe);

                std::string readbackMessage{};
                if (captureOk) {
                    captureOk = job.captureTarget.QueueReadbackFace(face, &readbackMessage);
                }

                POST::PostSystem::RebindCurrentRenderTarget();
                RENDERER3D::Reset();
                MODELRENDERER::Reset();
                SKYRENDERER::Reset();

                if (!captureOk) {
                    AddBakeError(job.report, readbackMessage.empty()
                        ? "Failed to render light probe volume scene capture."
                        : readbackMessage);
                    job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                    SetBakeJobState(job.report, job.state);
                    lastReport_ = job.report;
                    return true;
                }

                job.fenceValue = SERVICES::gCtx.currentFrameRetireFenceValue;
                job.report.gpuFenceValue = job.fenceValue;
                job.report.lightProbeCurrentProbeIndex = probe;
                job.report.lightProbeCurrentFaceIndex = face;
                ++job.report.lightProbeCapturedFaceCount;
                ++job.report.lightProbeQueuedReadbackFaceCount;
                job.report.lightProbeMessages.push_back(
                    "Probe " + std::to_string(probe) +
                    " face " + std::string(ProbeFaceName(face)) +
                    " captured.");
                ++job.nextFaceIndex;

                if (job.nextFaceIndex >= 6u) {
                    job.report.lightProbeMessages.push_back(
                        "Light probe GPU capture submitted. probe=" +
                        std::to_string(probe) +
                        " fence=" + std::to_string(job.fenceValue));
                    job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
                }
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                HIKARI_LOG_INFO("[LightingBake] light probe capture face submitted probe=" +
                    std::to_string(probe) +
                    " face=" + std::to_string(face) +
                    " fence=" + std::to_string(job.fenceValue));
                // Capture 闕ｳ・ｭ邵ｺ・ｯ鬨ｾ螢ｼ・ｸ・ｸ隰蜀怜愛邵ｺ・ｧ CameraCB 郢ｧ蜑・ｽｸ鬆大ｶ檎ｸｺ髦ｪ・邵ｺ・ｪ邵ｺ繝ｻﾂ繝ｻ
                return true;
            }
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
            if (!SERVICES::gCore.IsFenceComplete(job.fenceValue)) {
                lastReport_ = job.report;
                return false;
            }

            const uint32_t probe = job.nextProbeIndex;
            const std::filesystem::path capturePath =
                LightProbeCapturePath(job.request.projectRoot, job.request.sceneGuid, probe);

            std::string saveMessage{};
            if (!job.captureTarget.SaveReadbackToCubemapDds(capturePath, &saveMessage)) {
                AddBakeError(job.report, saveMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return false;
            }

            job.probeCapturePaths.push_back(capturePath);
            job.report.lightProbeMessages.push_back(saveMessage);
            job.report.bakeFolderCreated = true;
            ++job.nextProbeIndex;
            job.nextFaceIndex = 0;

            if (job.nextProbeIndex < GetLightProbeVolumeProbeCount(settings)) {
                job.state = TOOLS::BAKING::LightingBakeJobState::Capturing;
                job.report.lightProbeCurrentProbeIndex = job.nextProbeIndex;
                job.report.lightProbeCurrentFaceIndex = 0;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return false;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::ProjectingSH;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::ProjectingSH) {
            TOOLS::BAKING::LightProbeBaker baker{};
            const TOOLS::BAKING::LightProbeBakeResult bake =
                baker.FinalizeCapturedVolume(job.request, job.probeCapturePaths);

            job.report.lightProbeBaked = bake.success;
            job.report.lightProbeVolumeWritten = bake.volumeWritten;
            job.report.lightProbeDebugJsonWritten = bake.debugJsonWritten;
            job.report.lightProbeProbeCount = bake.probeCount;
            job.report.lightProbeCaptureResolution = bake.captureResolution;
            job.report.lightProbeVolumePath = bake.volumePath;
            job.report.lightProbeDebugJsonPath = bake.debugJsonPath;
            job.report.lightProbeMessages.insert(
                job.report.lightProbeMessages.end(),
                bake.messages.begin(),
                bake.messages.end());
            job.report.warnings.insert(job.report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
            job.report.errors.insert(job.report.errors.end(), bake.errors.begin(), bake.errors.end());

            if (!bake.success) {
                job.report.success = false;
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return false;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::Saving;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;

            ASSETS::LIGHTING::LightingBakeManifest manifest =
                LoadOrCreateLightingBakeManifest(
                    job.report.manifestPath,
                    job.request.projectRoot,
                    job.request.sceneGuid,
                    job.report.warnings);
            ReplaceLightProbeVolumeRecord(manifest, bake.record);

            std::string manifestMessage{};
            if (!ASSETS::LIGHTING::SaveLightingBakeManifest(
                    job.report.manifestPath,
                    manifest,
                    &manifestMessage)) {
                AddBakeError(job.report, manifestMessage);
                job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return false;
            }

            job.report.manifestWritten = true;
            job.report.lightProbeRecordWritten = true;
            job.report.reflectionProbeRecordCount =
                static_cast<uint32_t>(manifest.reflectionProbes.size());
            job.report.lightProbeRecordCount =
                static_cast<uint32_t>(manifest.lightProbes.size());
            job.report.lightmapRecordCount =
                static_cast<uint32_t>(manifest.lightmaps.size());
            job.report.messages.push_back(manifestMessage);
            job.report.messages.push_back("Light probe volume bake manifest updated: " +
                job.report.manifestPath.generic_string());

            scene.RefreshLightingRuntime();
            job.report.lightProbeRuntimeLoaded = RENDER3D::LIGHTPROBE::IsValid();

            job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            HIKARI_LOG_INFO("[LightingBake] light probe volume scene capture finalized scene=" +
                job.request.sceneGuid +
                " manifest=" + job.report.manifestPath.generic_string());
        }

        return false;
    }


} // namespace HIKARI
