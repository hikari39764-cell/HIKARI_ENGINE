#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"

#include <string>

#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

namespace HIKARI {
using namespace DOCUMENT_SCENE_BAKING;

DocumentSceneBakeCoordinator::LightmapJob::~LightmapJob() {
    if (outputsCommitted || outputDirectory.empty())
        return;
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(LightmapOutputDirectory(projectRoot, sceneGuid), ec);
    if (ec)
        return;
    const auto owned = std::filesystem::weakly_canonical(outputDirectory, ec);
    if (!ec && owned.parent_path() == root && owned.filename().string().starts_with(recordId + "_")) {
        std::filesystem::remove_all(owned, ec);
    }
}

bool DocumentSceneBakeCoordinator::ProcessLightmapBakeJob(DocumentSceneBase& scene) {
    if (!lightmapJob_) {
        return false;
    }
    LightmapJob& job = *lightmapJob_;
    const GFX::Context graphics = GFX::GetCurrentContext();

    const auto failJob = [&](std::string message) {
        AddBakeError(job.report, std::move(message));
        job.state = TOOLS::BAKING::LightingBakeJobState::Failed;
        SetBakeJobState(job.report, job.state);
        lastReport_ = job.report;
        queuedBakeItems_.clear();
        batchItemCount_ = 0u;
        batchCompletedItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = false;
    };

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Failed) {
        lightmapJob_.reset();
        return false;
    }

    if (job.state != TOOLS::BAKING::LightingBakeJobState::Saving) {
        const auto& target = job.transportScene.targets[job.targetIndex];
        const auto& page = target.pages[job.pageIndex];

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Requested) {
            std::string message{};
            if (!job.scenePrepared && !job.baker.PrepareScene(graphics, job.transportScene, &message)) {
                failJob(message);
                return false;
            }
            job.scenePrepared = true;
            if (!job.baker.BeginTarget(graphics, job.transportScene, page, job.settings, &message)) {
                failJob(message);
                return false;
            }
            job.report.messages.push_back(message);
            job.state = TOOLS::BAKING::LightingBakeJobState::Tracing;
            SetBakeJobState(job.report, job.state);
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::Tracing) {
            if (!job.baker.RecordSamples(graphics, 4u)) {
                failJob("Failed to record progressive DXR lightmap samples.");
                return false;
            }
            const TOOLS::BAKING::ProgressiveLightmapBakeStatus& status = job.baker.GetStatus();
            job.report.lightmapCompletedSamples =
                static_cast<uint32_t>(job.pageIndex) * status.requestedSamples + status.completedSamples;
            job.report.lightmapRequestedSamples = static_cast<uint32_t>(target.pages.size()) * status.requestedSamples;
            job.report.lightmapCoveredTexelCount = status.coveredTexelCount;
            job.report.lightmapOverlapTexelCount = status.overlapTexelCount;
            lastReport_ = job.report;
            hasLastReport_ = true;

            if (status.completedSamples >= status.requestedSamples) {
                std::string message{};
                if (!job.baker.QueueReadback(graphics, &message)) {
                    failJob(message);
                    return false;
                }
                job.report.messages.push_back(message);
                job.state = TOOLS::BAKING::LightingBakeJobState::WaitingGpu;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
            }
            return true;
        }

        if (job.state == TOOLS::BAKING::LightingBakeJobState::WaitingGpu) {
            std::string message{};
            if (!job.baker.TrySave(graphics, job.outputPath, &message)) {
                if (job.baker.GetStatus().state == TOOLS::BAKING::ProgressiveLightmapBakeState::Failed) {
                    failJob(message);
                }
                return false;
            }

            job.report.messages.push_back(message);
            job.report.bakeFolderCreated = true;
            ASSETS::LIGHTING::LightmapBakePage savedPage{};
            savedPage.texturePath = PROJECT_PATHS::MakeProjectRelativeString(job.projectRoot, job.outputPath);
            savedPage.width = savedPage.height = job.settings.outputResolution;
            savedPage.uvSet = page.uvSet;
            savedPage.surfaceBindings = page.surfaceBindings;
            job.completedPages.push_back(std::move(savedPage));
            if (++job.pageIndex < target.pages.size()) {
                job.settings.outputResolution = target.pages[job.pageIndex].outputResolution;
                job.outputPath = job.outputDirectory / ("page_" + std::to_string(job.pageIndex) + ".dds");
                job.report.lightmapPath = job.outputPath;
                job.report.lightmapResolution = job.settings.outputResolution;
                job.report.currentItemName = job.objectName + " (" + std::to_string(job.pageIndex + 1) + "/" +
                                             std::to_string(target.pages.size()) + ")";
                job.state = TOOLS::BAKING::LightingBakeJobState::Requested;
                SetBakeJobState(job.report, job.state);
                lastReport_ = job.report;
                return true;
            }

            job.state = TOOLS::BAKING::LightingBakeJobState::Saving;
            SetBakeJobState(job.report, job.state);

            // 最終 page の保存後、輸送用 GPU 資源の退役を先に完了させる。
            job.baker.Reset();
            job.resourceReleaseFence = graphics.currentFrameRetireFenceValue;
            job.transportScene = {};
            job.report.messages.push_back("Waiting for bake resource retirement before loading lightmaps.");
            lastReport_ = job.report;
            return true;
        }
    }

    if (job.state == TOOLS::BAKING::LightingBakeJobState::Saving) {
        const uint64_t completedFence = graphics.frameFence != nullptr ? graphics.frameFence->GetCompletedValue() : 0u;
        if (completedFence == UINT64_MAX) {
            failJob("GPU device was removed while retiring lightmap bake resources.");
            return false;
        }
        if (job.resourceReleaseFence != 0u && completedFence < job.resourceReleaseFence)
            return true;
        if (graphics.deferredReleaseQueue != nullptr)
            graphics.deferredReleaseQueue->Collect(completedFence);

        ASSETS::LIGHTING::LightingBakeManifest manifest = LoadOrCreateLightingBakeManifest(
            job.report.manifestPath, job.projectRoot, job.sceneGuid, job.report.warnings);
        ASSETS::LIGHTING::LightmapBakeRecord record{};
        record.id = job.recordId;
        record.name = job.objectName;
        record.objectId = job.objectId.value;
        record.pages = job.completedPages;
        record.encoding = "RGB16F_DiffuseIrradiance";
        record.authoringHash = job.authoringHash;
        record.captureSceneHash = job.sceneHash;
        record.dependencyHash = job.dependencyHash;
        record.bakeSet = job.bakeSet;
        record.scenario = job.scenario;
        std::vector<ASSETS::LIGHTING::LightmapBakeRecord> previousRecords;
        for (const auto& previous : manifest.lightmaps)
            if (previous.id == record.id || previous.objectId == record.objectId)
                previousRecords.push_back(previous);
        ReplaceLightmapRecord(manifest, record);
        manifest.sceneHash = job.sceneHash;
        manifest.dependencyHash = job.dependencyHash;
        manifest.activeScenario = job.scenario;

        std::string manifestMessage{};
        if (!ASSETS::LIGHTING::SaveLightingBakeManifest(job.report.manifestPath, manifest, &manifestMessage)) {
            failJob(manifestMessage);
            return false;
        }

        job.outputsCommitted = true;
        job.report.lightmapBaked = true;
        job.report.manifestWritten = true;
        job.report.lightmapRecordWritten = true;
        job.report.reflectionProbeRecordCount = static_cast<uint32_t>(manifest.reflectionProbes.size());
        job.report.lightProbeRecordCount = static_cast<uint32_t>(manifest.lightProbes.size());
        job.report.lightmapRecordCount = static_cast<uint32_t>(manifest.lightmaps.size());
        job.report.messages.push_back(manifestMessage);

        // Manifest commit 後に旧 page の cache だけを無効化する。Descriptor の解放は既存 fence owner に委譲する。
        for (const auto& previous : previousRecords) {
            for (size_t i = 0; i < previous.pages.size(); ++i) {
                const auto path = PROJECT_PATHS::ResolveProjectPath(job.projectRoot, previous.pages[i].texturePath);
                RENDER3D::EvictImportedTextureResourceWithColorSpace(
                    ASSETS::LIGHTING::BuildLightmapPageResourceKey(previous.id, i), path.generic_string(),
                    RENDER3D::TextureResourceColorSpace::Linear);
                // 自分の generated root 内の旧 DDS のみ削除する。共有 page は保持する。
                bool referenced = false;
                for (const auto& live : manifest.lightmaps)
                    for (const auto& livePage : live.pages)
                        referenced |= livePage.texturePath == previous.pages[i].texturePath;
                std::error_code ec;
                const auto root =
                    std::filesystem::weakly_canonical(LightmapOutputDirectory(job.projectRoot, job.sceneGuid), ec);
                if (ec)
                    continue;
                const auto oldPath = std::filesystem::weakly_canonical(path, ec);
                if (!ec && !referenced && oldPath.extension() == ".dds" &&
                    (oldPath.parent_path() == root ||
                     (oldPath.parent_path().parent_path() == root &&
                      oldPath.parent_path().filename().string().starts_with(previous.id + "_")))) {
                    std::filesystem::remove(oldPath, ec);
                    if (oldPath.parent_path() != root)
                        std::filesystem::remove(oldPath.parent_path(), ec);
                }
            }
        }
        scene.RefreshLightingRuntime();
        job.report.lightmapRuntimeLoaded = true;
        for (const auto& saved : record.pages)
            for (const auto& binding : saved.surfaceBindings) {
                const auto& identity = binding.surface;
                job.report.lightmapRuntimeLoaded &=
                    RENDER3D::LIGHTMAP::Find(job.objectId.value, identity.nodeIndex, identity.meshIndex,
                                             identity.primitiveIndex) != nullptr;
            }
        // Lightmap Descriptor は Surface Record のみに影響する。
        // Model / MaterialFx / Procedural / Shadow Cache は破棄しない。
        RenderSubmissionSystem::InvalidateSurfaceResourceBindings();

        job.state = TOOLS::BAKING::LightingBakeJobState::Completed;
        SetBakeJobState(job.report, job.state);
        ++batchCompletedItemCount_;
        job.report.completedItemCount = batchCompletedItemCount_ + batchReusedItemCount_;
        lastReport_ = job.report;
        hasLastReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] DXR lightmap finalized object=" + std::to_string(job.objectId.value) +
                        " output=" + job.outputPath.generic_string());
        lightmapJob_.reset();
        return StartNextQueuedBake(scene);
    }

    return false;
}

} // namespace HIKARI
