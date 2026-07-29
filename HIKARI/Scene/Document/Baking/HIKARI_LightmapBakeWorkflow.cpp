#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"

#include <string>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
    using namespace DOCUMENT_SCENE_BAKING;

    bool DocumentSceneBakeCoordinator::ProcessLightmapBakeJob(
        DocumentSceneBase& scene) {

        if (!lightmapJob_) {
            return false;
        }
        LightmapJob& job = *lightmapJob_;
        const GFX::Context graphics =
            GFX::GetCurrentContext();

        const auto failJob = [&](std::string message) {
            AddBakeError(job.report, std::move(message));
            job.state =
                TOOLS::BAKING::LightingBakeJobState::Failed;
            SetBakeJobState(job.report, job.state);
            lastReport_ = job.report;
            queuedBakeItems_.clear();
            batchItemCount_ = 0u;
            batchCompletedItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
            batchForce_ = false;
        };

        if (job.state ==
            TOOLS::BAKING::LightingBakeJobState::Failed) {
            lightmapJob_.reset();
            return false;
        }

        if (job.state ==
            TOOLS::BAKING::LightingBakeJobState::Requested) {
            std::string message{};
            if (!job.baker.PrepareScene(
                    graphics,
                    job.transportScene,
                    &message)) {
                failJob(message);
                return false;
            }
            if (!job.baker.BeginTarget(
                    graphics,
                    job.transportScene,
                    job.transportScene.targets[
                        job.targetIndex],
                    job.settings,
                    &message)) {
                failJob(message);
                return false;
            }
            job.report.messages.push_back(message);
            job.state =
                TOOLS::BAKING::LightingBakeJobState::Tracing;
            SetBakeJobState(job.report, job.state);
        }

        if (job.state ==
            TOOLS::BAKING::LightingBakeJobState::Tracing) {
            if (!job.baker.RecordSamples(graphics, 4u)) {
                failJob(
                    "Failed to record progressive DXR lightmap samples.");
                return false;
            }
            const TOOLS::BAKING::
                ProgressiveLightmapBakeStatus& status =
                    job.baker.GetStatus();
            job.report.lightmapCompletedSamples =
                status.completedSamples;
            job.report.lightmapRequestedSamples =
                status.requestedSamples;
            job.report.lightmapCoveredTexelCount =
                status.coveredTexelCount;
            job.report.lightmapOverlapTexelCount =
                status.overlapTexelCount;
            lastReport_ = job.report;
            hasLastReport_ = true;

            if (status.completedSamples >=
                status.requestedSamples) {
                std::string message{};
                if (!job.baker.QueueReadback(
                        graphics,
                        &message)) {
                    failJob(message);
                    return false;
                }
                job.report.messages.push_back(message);
                job.state =
                    TOOLS::BAKING::LightingBakeJobState::
                        WaitingGpu;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
            }
            return true;
        }

        if (job.state ==
            TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
            std::string message{};
            if (!job.baker.TrySave(
                    graphics,
                    job.outputPath,
                    &message)) {
                if (job.baker.GetStatus().state ==
                    TOOLS::BAKING::
                        ProgressiveLightmapBakeState::Failed) {
                    failJob(message);
                }
                return false;
            }

            job.report.messages.push_back(message);
            job.report.lightmapBaked = true;
            job.report.bakeFolderCreated = true;
            job.state =
                TOOLS::BAKING::LightingBakeJobState::Saving;
            SetBakeJobState(job.report, job.state);

            ASSETS::LIGHTING::LightingBakeManifest manifest =
                LoadOrCreateLightingBakeManifest(
                    job.report.manifestPath,
                    job.projectRoot,
                    job.sceneGuid,
                    job.report.warnings);
            ASSETS::LIGHTING::LightmapBakeRecord record{};
            record.id = job.recordId;
            record.name = job.objectName;
            record.objectId = job.objectId.value;
            record.texturePath =
                PROJECT_PATHS::MakeProjectRelativeString(
                    job.projectRoot,
                    job.outputPath);
            record.width = job.settings.outputResolution;
            record.height = job.settings.outputResolution;
            record.uvSet =
                ASSETS::LIGHTING::kSurfaceLightmapUvSet;
            record.surfaceBindings =
                job.transportScene.targets[
                    job.targetIndex].surfaceBindings;
            record.encoding =
                "RGB16F_DiffuseIrradiance";
            record.authoringHash = job.authoringHash;
            record.captureSceneHash = job.sceneHash;
            record.dependencyHash = job.dependencyHash;
            record.bakeSet = job.bakeSet;
            record.scenario = job.scenario;
            ReplaceLightmapRecord(manifest, record);
            manifest.sceneHash = job.sceneHash;
            manifest.dependencyHash = job.dependencyHash;
            manifest.activeScenario = job.scenario;

            std::string manifestMessage{};
            if (!ASSETS::LIGHTING::SaveLightingBakeManifest(
                    job.report.manifestPath,
                    manifest,
                    &manifestMessage)) {
                failJob(manifestMessage);
                return false;
            }

            job.report.manifestWritten = true;
            job.report.lightmapRecordWritten = true;
            job.report.reflectionProbeRecordCount =
                static_cast<uint32_t>(
                    manifest.reflectionProbes.size());
            job.report.lightProbeRecordCount =
                static_cast<uint32_t>(
                    manifest.lightProbes.size());
            job.report.lightmapRecordCount =
                static_cast<uint32_t>(
                    manifest.lightmaps.size());
            job.report.messages.push_back(manifestMessage);

            scene.RefreshLightingRuntime();
            const auto& surfaceBindings =
                job.transportScene.targets[
                    job.targetIndex].surfaceBindings;
            job.report.lightmapRuntimeLoaded =
                !surfaceBindings.empty() &&
                RENDER3D::LIGHTMAP::Find(
                    job.objectId.value,
                    surfaceBindings.front().
                        surface.nodeIndex,
                    surfaceBindings.front().
                        surface.meshIndex,
                    surfaceBindings.front().
                        surface.primitiveIndex) != nullptr;
            RenderSubmissionSystem::InvalidateSceneResources(false);

            job.state =
                TOOLS::BAKING::LightingBakeJobState::Completed;
            SetBakeJobState(job.report, job.state);
            ++batchCompletedItemCount_;
            job.report.completedItemCount =
                batchCompletedItemCount_ +
                batchReusedItemCount_;
            lastReport_ = job.report;
            hasLastReport_ = true;
            HIKARI_LOG_INFO(
                "[LightingBake] DXR lightmap finalized object=" +
                std::to_string(job.objectId.value) +
                " output=" +
                job.outputPath.generic_string());
            lightmapJob_.reset();
            return StartNextQueuedBake(scene);
        }

        return false;
    }

} // namespace HIKARI
