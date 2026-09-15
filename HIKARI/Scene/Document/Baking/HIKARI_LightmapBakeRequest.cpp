#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Scene/Document/Baking/HIKARI_LightingTransportSceneBuilder.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {
    using namespace DOCUMENT_SCENE_BAKING;

    bool DocumentSceneBakeCoordinator::RequestLightmapBake(
        DocumentSceneBase& scene,
        SceneObjectId targetObjectId) {

        if (TOOLS::BAKING::IsLightingBakeJobRunning(
                GetJobState())) {
            lastReport_.warnings.push_back(
                "A lighting bake job is already running.");
            hasLastReport_ = true;
            return false;
        }

        TOOLS::BAKING::LightingBakeReport report{};
        report.action =
            TOOLS::BAKING::LightingBakeAction::BakeLightmaps;
        report.target =
            TOOLS::BAKING::LightingBakeTarget::LightmapsOnly;
        report.lightingTransportBackend = "DxrPathTracing";
        SetBakeJobState(
            report,
            TOOLS::BAKING::LightingBakeJobState::Requested);

        if (scene.state_->assets.Database().
                GetProjectRoot().empty()) {
            scene.state_->assets.Database().Initialize({});
        }
        const std::filesystem::path projectRoot =
            scene.state_->assets.Database().GetProjectRoot();
        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!scene.state_->identity.currentSceneAssetGuid.
                IsValid()) {
            AddBakeError(
                report,
                "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (targetObjectId.value == 0u) {
            AddBakeError(
                report,
                "Surface lightmap bake requires a stable target object.");
        }

        const RENDER3D::LIGHTING::SceneLightingSet lighting =
            LIGHTING_AUTHORING::BuildRuntimeSceneLightingSet(
                scene.state_->identity.document,
                scene.state_->runtime.world);
        RenderSubmissionSystem::SetRenderViewPurpose(
            RENDER3D::RenderViewPurpose::Game);
        RenderSubmissionSystem::SetAssetContext(
            &scene.state_->assets.Registry(),
            projectRoot,
            &scene.state_->assets.Models());
        scene.state_->runtime.systemScheduler.PreRender(
            scene.state_->runtime.world,
            HIKARI::TIME::GetFrameContext());
        RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportScene transportScene =
                BuildLightingTransportScene(
                    scene.state_->identity.document,
                    scene.state_->lighting.environment,
                    lighting,
                    RenderSubmissionSystem::
                        GetSceneRenderCache(),
                    &scene.GetSkyManager());
        const auto target = std::find_if(
            transportScene.targets.begin(),
            transportScene.targets.end(),
            [targetObjectId](const auto& candidate) {
                return candidate.objectId ==
                    targetObjectId.value;
            });
        if (target == transportScene.targets.end()) {
            AddBakeError(
                report,
                "Target is not a visible Static model included in lighting bake.");
        } else if (!target->eligible) {
            AddBakeError(
                report,
                target->name + ": " +
                    target->eligibilityMessage);
        }

        auto job = std::make_unique<LightmapJob>();
        report.dxrBackendAvailable =
            job->baker.GetCapabilities(
                GFX::GetCurrentContext()).available;
        if (!report.dxrBackendAvailable) {
            AddBakeError(
                report,
                "DXR 1.1 and Shader Model 6.5 are required for progressive lightmap baking. Runtime Probe/IBL fallback remains active.");
        }
        if (!report.errors.empty()) {
            SetBakeJobState(
                report,
                TOOLS::BAKING::LightingBakeJobState::Failed);
            lastReport_ = std::move(report);
            hasLastReport_ = true;
            return false;
        }

        const ASSETS::LIGHTING::LightingBakeHashSnapshot hashes =
            ASSETS::LIGHTING::BuildLightingBakeHashSnapshot(
                scene.state_->identity.document,
                scene.state_->assets.Database());
        job->state =
            TOOLS::BAKING::LightingBakeJobState::Requested;
        job->report = report;
        job->transportScene = std::move(transportScene);
        job->targetIndex = static_cast<size_t>(
            std::distance(
                job->transportScene.targets.begin(),
                std::find_if(
                    job->transportScene.targets.begin(),
                    job->transportScene.targets.end(),
                    [targetObjectId](const auto& candidate) {
                        return candidate.objectId ==
                            targetObjectId.value;
                    })));
        job->objectId = targetObjectId;
        job->projectRoot = projectRoot;
        job->sceneGuid =
            scene.state_->identity.currentSceneAssetGuid.value;
        job->sceneHash = hashes.sceneHash;
        job->dependencyHash = hashes.dependencyHash;
        job->recordId =
            "lightmap_object_" +
            std::to_string(targetObjectId.value);
        const auto authoringHash =
            hashes.recordAuthoringHashes.find(
                job->recordId);
        job->authoringHash =
            authoringHash !=
                hashes.recordAuthoringHashes.end()
            ? authoringHash->second
            : std::string{};
        const auto& resolvedTarget =
            job->transportScene.targets[job->targetIndex];
        job->objectName = resolvedTarget.name;
        job->scenario = lighting.activeScenario.empty()
            ? "Default"
            : lighting.activeScenario;
        job->settings.sampleCount =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultSampleCount;
        job->settings.bounceCount =
            ASSETS::LIGHTING::
                kSurfaceLightmapDefaultBounceCount;
        job->settings.outputResolution =
            resolvedTarget.pages.front().outputResolution;
        job->outputDirectory =
            LightmapOutputDirectory(
                projectRoot,
                job->sceneGuid) /
            (job->recordId + "_" + GenerateAssetGuid().value);
        job->outputPath = job->outputDirectory / "page_0.dds";

        job->report.bakeRoot =
            ASSETS::LIGHTING::BuildLightingBakeRoot(
                projectRoot,
                job->sceneGuid);
        job->report.manifestPath =
            ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                projectRoot,
                job->sceneGuid);
        job->report.lightmapPath = job->outputPath;
        job->report.lightmapResolution =
            job->settings.outputResolution;
        job->report.lightmapRequestedSamples =
            job->settings.sampleCount * static_cast<uint32_t>(resolvedTarget.pages.size());
        if (batchItemCount_ == 0u) {
            batchItemCount_ = 1u;
            batchCompletedItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
        }
        job->report.bakeItemCount = batchItemCount_;
        job->report.dirtyItemCount =
            static_cast<uint32_t>(
                queuedBakeItems_.size()) + 1u;
        job->report.completedItemCount =
            batchCompletedItemCount_;
        job->report.reusedItemCount =
            batchReusedItemCount_;
        job->report.currentItemName = job->objectName;
        job->report.messages.push_back(
            "Surface lightmap uses UV1, static opaque geometry, RGB16F diffuse irradiance, and per-record runtime fallback.");
        job->report.messages.push_back(
            resolvedTarget.eligibilityMessage);

        lightmapJob_ = std::move(job);
        lastReport_ = lightmapJob_->report;
        hasLastReport_ = true;
        return true;
    }

} // namespace HIKARI
