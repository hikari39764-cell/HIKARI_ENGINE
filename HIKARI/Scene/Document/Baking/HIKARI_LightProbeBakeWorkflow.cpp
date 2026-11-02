#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"

#include <string>
#include "Core/HIKARI_Logger.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Tools/Baking/HIKARI_LightProbeBaker.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"

namespace HIKARI {
using namespace DOCUMENT_SCENE_BAKING;

// 探針は光照贴图と同じ DXR の経路で焼く：場面を用意し、探針ごとに 16x16 の八面体の方向へ標本を積み、読み戻して
// SH9 と可視性の moment にまとめ、manifest を更新する。
bool DocumentSceneBakeCoordinator::ProcessLightProbeBakeJob(DocumentSceneBase& scene) {
    if (!lightProbeJob_) {
        return false;
    }
    LightProbeJob& job = *lightProbeJob_;
    const GFX::Context graphics = GFX::GetCurrentContext();

    const auto failJob = [&](std::string message) {
        AddBakeError(job.report, std::move(message));
        job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
        hasLastReport_ = true;
    };

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Failed) {
        queuedBakeItems_.clear();
        lightProbeJob_.reset();
        batchItemCount_ = 0u;
        batchCompletedItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = false;
        return false;
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
        std::string message{};
        RENDER3D::LIGHTING::TRANSPORT::LightingTransportBakeSettings settings{};
        settings.sampleCount = job.request.sampleCount;
        settings.bounceCount = job.request.bounceCount;
        if (!job.baker.PrepareScene(graphics, job.transportScene, &message) ||
            !job.baker.BeginProbeTarget(graphics, job.positions, job.visibilityDistance, settings, &message)) {
            failJob(message);
            return false;
        }
        job.report.lightProbeMessages.push_back(message);
        job.state = TOOLS::BAKING::LightingBakeJobState::Tracing;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
        hasLastReport_ = true;
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Tracing) {
        if (!job.baker.RecordSamples(graphics, 4u)) {
            failJob("Failed to record DXR light probe samples.");
            return false;
        }
        const TOOLS::BAKING::ProgressiveLightmapBakeStatus& status = job.baker.GetStatus();
        job.report.transportCompletedSamples = status.completedSamples;
        job.report.transportRequestedSamples = status.requestedSamples;
        if (status.completedSamples >= status.requestedSamples) {
            std::string message{};
            if (!job.baker.QueueReadback(graphics, &message)) {
                failJob(message);
                return false;
            }
            job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
            SetBakeJobState(job.report, job.state);
        }
        lastReport_ = job.report;
        return true;
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
        if (!job.baker.TryReadProbes(graphics, job.cells)) {
            if (job.baker.GetStatus().state == TOOLS::BAKING::ProgressiveLightmapBakeState::Failed) {
                failJob(job.baker.GetStatus().message);
            }
            return false;
        }
        // 輸送用の GPU 資源は退役の列へ渡し、場面の写しも捨てる。
        job.baker.Reset();
        job.transportScene = {};
        job.state = TOOLS::BAKING::LightingBakeJobState::ProjectingSH;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::ProjectingSH) {
        TOOLS::BAKING::LightProbeBaker baker{};
        const TOOLS::BAKING::LightProbeBakeResult bake =
            baker.BuildVolume(job.request, job.cells, job.visibilityDistance);
        job.cells = {};

        job.report.lightProbeBaked = bake.success;
        job.report.lightProbeVolumeWritten = bake.volumeWritten;
        job.report.lightProbeDebugJsonWritten = bake.debugJsonWritten;
        job.report.lightProbeProbeCount = bake.probeCount;
        job.report.lightProbeBuriedCount = bake.buriedProbeCount;
        job.report.lightProbeVolumePath = bake.volumePath;
        job.report.lightProbeDebugJsonPath = bake.debugJsonPath;
        job.report.lightProbeMessages.insert(job.report.lightProbeMessages.end(), bake.messages.begin(),
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

        ASSETS::LIGHTING::LightingBakeManifest manifest = LoadOrCreateLightingBakeManifest(
            job.report.manifestPath, job.request.projectRoot, job.request.sceneGuid, job.report.warnings);
        ReplaceLightProbeVolumeRecord(manifest, bake.record);
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
        job.report.lightProbeRecordWritten = true;
        job.report.bakeFolderCreated = true;
        job.report.reflectionProbeRecordCount = static_cast<uint32_t>(manifest.reflectionProbes.size());
        job.report.lightProbeRecordCount = static_cast<uint32_t>(manifest.lightProbes.size());
        job.report.lightmapRecordCount = static_cast<uint32_t>(manifest.lightmaps.size());
        job.report.messages.push_back(manifestMessage);
        job.report.messages.push_back("Light probe volume bake manifest updated: " +
                                      job.report.manifestPath.generic_string());

        scene.RefreshLightingRuntime();
        job.report.lightProbeRuntimeLoaded = RENDER3D::LIGHTPROBE::IsValid();

        job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
        SetBakeJobState(job.report, job.state);
        ++batchCompletedItemCount_;
        job.report.completedItemCount = batchCompletedItemCount_ + batchReusedItemCount_;
        lastReport_ = job.report;
        HIKARI_LOG_INFO("[LightingBake] light probe volume path traced scene=" + job.request.sceneGuid +
                        " buried=" + std::to_string(job.report.lightProbeBuriedCount) +
                        " manifest=" + job.report.manifestPath.generic_string());
        lightProbeJob_.reset();
        return StartNextQueuedBake(scene);
    }

    return false;
}

} // namespace HIKARI
