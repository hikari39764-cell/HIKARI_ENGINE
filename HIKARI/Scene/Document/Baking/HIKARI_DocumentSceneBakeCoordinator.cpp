#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {
    using namespace DOCUMENT_SCENE_BAKING;

    bool DocumentSceneBakeCoordinator::RequestReflectionProbeBake(
        DocumentSceneBase& scene,
        SceneObjectId targetObjectId) {
        if (reflectionProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                reflectionProbeJob_->state)) {
            lastReport_ = reflectionProbeJob_->report;
            lastReport_.warnings.push_back("Reflection probe bake is already running.");
            hasLastReport_ = true;
            return false;
        }
        if (lightProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                lightProbeJob_->state)) {
            lastReport_ = lightProbeJob_->report;
            lastReport_.warnings.push_back("Light probe volume bake is already running.");
            hasLastReport_ = true;
            return false;
        }

        TOOLS::BAKING::LightingBakeReport report{};
        report.action = TOOLS::BAKING::LightingBakeAction::BakeReflectionProbes;
        report.target = TOOLS::BAKING::LightingBakeTarget::ReflectionProbesOnly;
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Requested);

        if (scene.state_->assets.Database().GetProjectRoot().empty()) {
            scene.state_->assets.Database().Initialize({});
        }

        LIGHTING_AUTHORING::SynchronizeLegacyLightingViews(
            scene.state_->identity.document);
        const std::vector<LIGHTING_AUTHORING::ReflectionProbeAuthoring>
            probes = LIGHTING_AUTHORING::CollectReflectionProbes(
                scene.state_->identity.document);
        const auto probeIt = std::find_if(
            probes.begin(),
            probes.end(),
            [targetObjectId](
                const LIGHTING_AUTHORING::ReflectionProbeAuthoring&
                    probe) {
                return targetObjectId.value != 0u
                    ? probe.objectId == targetObjectId
                    : probe.settings.enabled;
            });
        const LIGHTING_AUTHORING::ReflectionProbeAuthoring* probe =
            probeIt == probes.end() ? nullptr : &*probeIt;

        const std::filesystem::path projectRoot = scene.state_->assets.Database().GetProjectRoot();
        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!scene.state_->identity.currentSceneAssetGuid.IsValid()) {
            AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (probe == nullptr || !probe->settings.enabled) {
            AddBakeError(
                report,
                "No enabled reflection probe matches the bake request.");
        }
        if (probe != nullptr && probe->settings.radius <= 0.0f) {
            AddBakeError(report, "Reflection probe radius must be greater than zero.");
        }

        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        const std::string probeId = probe != nullptr
            ? "reflection_probe_" +
                std::to_string(probe->objectId.value)
            : "reflection_probe_invalid";
        report.reflectionProbeCapturePath =
            ReflectionProbeOutputDirectory(
                projectRoot,
                scene.state_->identity.currentSceneAssetGuid.value) /
            (probeId + "_capture.dds");
        report.reflectionProbePrefilteredPath =
            ReflectionProbeOutputDirectory(
                projectRoot,
                scene.state_->identity.currentSceneAssetGuid.value) /
            (probeId + "_prefiltered.dds");
        report.reflectionProbeBrdfLutPath =
            (projectRoot / "Library" / "Generated" / "IBL" /
                "brdf_lut.dds").lexically_normal();
        report.reflectionProbeCaptureMode = "SceneCapture";
        report.reflectionProbeCaptureResolution = 128;

        if (!report.errors.empty()) {
            SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
            lastReport_ = report;
            hasLastReport_ = true;
            HIKARI_LOG_WARN("[LightingBake] reflection probe bake request failed.");
            return false;
        }

        auto job = std::make_unique<ReflectionProbeJob>();
        const ASSETS::LIGHTING::LightingBakeHashSnapshot hashes =
            ASSETS::LIGHTING::BuildLightingBakeHashSnapshot(
                scene.state_->identity.document,
                scene.state_->assets.Database());
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = scene.state_->identity.currentSceneAssetGuid.value;
        job->request.sceneName = scene.GetCurrentSceneDisplayName();
        job->request.probeId = probeId;
        job->request.probeName = probe->name;
        job->request.position = probe->settings.position;
        job->request.radius = probe->settings.radius;
        job->request.intensity = probe->settings.intensity;
        job->request.influenceShape = ReflectionProbeInfluenceShapeName(probe->settings.influenceShape);
        job->request.influenceBoxCenter = probe->settings.influenceBoxCenter;
        job->request.influenceBoxSize = probe->settings.influenceBoxSize;
        job->request.projectionShape = ReflectionProbeProjectionShapeName(probe->settings.projectionShape);
        job->request.projectionBoxCenter = probe->settings.projectionBoxCenter;
        job->request.projectionBoxSize = probe->settings.projectionBoxSize;
        job->request.blendDistance = probe->settings.blendDistance;
        job->request.priority = probe->settings.priority;
        job->request.resolution = probe->captureResolution;
        job->request.prefilteredMipCount = 7;
        job->request.prefilteredSampleCount = 128;
        job->request.brdfLutSize = 256;
        job->request.brdfSampleCount = 256;
        job->request.forceRebake = batchForce_;
        const auto objectHash =
            hashes.recordAuthoringHashes.find(probeId);
        job->request.authoringHash =
            objectHash == hashes.recordAuthoringHashes.end()
                ? std::string{}
                : objectHash->second;
        job->request.sceneHash = hashes.sceneHash;
        job->request.dependencyHash = hashes.dependencyHash;
        job->request.bakeSet = probe->bakeSet;
        job->request.scenario = probe->scenario;
        job->report = report;
        if (batchItemCount_ == 0u) {
            batchItemCount_ = 1u;
            batchCompletedItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
            job->report.dirtyReasons.clear();
        } else {
            job->report.dirtyReasons = lastReport_.dirtyReasons;
        }
        job->report.bakeItemCount = batchItemCount_;
        job->report.dirtyItemCount =
            static_cast<uint32_t>(queuedBakeItems_.size()) + 1u;
        job->report.completedItemCount = batchCompletedItemCount_;
        job->report.reusedItemCount = batchReusedItemCount_;
        job->report.currentItemName = probe->name;
        job->report.messages.push_back("Reflection probe scene capture requested.");

        reflectionProbeJob_ = std::move(job);
        lastReport_ = reflectionProbeJob_->report;
        hasLastReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] reflection probe scene capture requested scene=" +
            scene.state_->identity.currentSceneAssetGuid.value);
        return true;
    }

    bool DocumentSceneBakeCoordinator::RequestLightProbeBake(
        DocumentSceneBase& scene,
        SceneObjectId targetObjectId) {
        if (lightProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                lightProbeJob_->state)) {
            lastReport_ = lightProbeJob_->report;
            lastReport_.warnings.push_back("Light probe volume bake is already running.");
            hasLastReport_ = true;
            return false;
        }
        if (reflectionProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                reflectionProbeJob_->state)) {
            lastReport_ = reflectionProbeJob_->report;
            lastReport_.warnings.push_back("Reflection probe bake is already running.");
            hasLastReport_ = true;
            return false;
        }

        TOOLS::BAKING::LightingBakeReport report{};
        report.action = TOOLS::BAKING::LightingBakeAction::BakeLightProbes;
        report.target = TOOLS::BAKING::LightingBakeTarget::LightProbesOnly;
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Requested);

        if (scene.state_->assets.Database().GetProjectRoot().empty()) {
            scene.state_->assets.Database().Initialize({});
        }

        const std::filesystem::path projectRoot = scene.state_->assets.Database().GetProjectRoot();
        LIGHTING_AUTHORING::SynchronizeLegacyLightingViews(
            scene.state_->identity.document);
        const std::vector<LIGHTING_AUTHORING::IrradianceVolumeAuthoring>
            volumes = LIGHTING_AUTHORING::CollectIrradianceVolumes(
                scene.state_->identity.document);
        const auto volumeIt = std::find_if(
            volumes.begin(),
            volumes.end(),
            [targetObjectId](
                const LIGHTING_AUTHORING::IrradianceVolumeAuthoring&
                    volume) {
                return targetObjectId.value != 0u
                    ? volume.objectId == targetObjectId
                    : volume.settings.enabled;
            });
        const LIGHTING_AUTHORING::IrradianceVolumeAuthoring* volume =
            volumeIt == volumes.end() ? nullptr : &*volumeIt;
        LightProbeVolumeSettings settings = volume != nullptr
            ? volume->settings
            : LightProbeVolumeSettings{};
        if (volume == nullptr) {
            settings.enabled = false;
        }
        ClampLightProbeVolumeSettings(settings);

        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!scene.state_->identity.currentSceneAssetGuid.IsValid()) {
            AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (!settings.enabled) {
            AddBakeError(report, "Light probe volume is disabled in the current scene.");
        }

        const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        report.lightProbeVolumePath = ASSETS::LIGHTING::BuildLightProbeVolumeOutputPath(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        report.lightProbeProbeCount = probeCount;
        report.lightProbeCaptureResolution = settings.captureResolution;
        report.lightProbeMessages.push_back(
            "Light probe volume requested: probes=" + std::to_string(probeCount) +
            " faces=" + std::to_string(GetLightProbeCaptureFaceCount(settings)));

        if (!report.errors.empty()) {
            SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
            lastReport_ = report;
            hasLastReport_ = true;
            HIKARI_LOG_WARN("[LightingBake] light probe bake request failed.");
            return false;
        }

        auto job = std::make_unique<LightProbeJob>();
        const ASSETS::LIGHTING::LightingBakeHashSnapshot hashes =
            ASSETS::LIGHTING::BuildLightingBakeHashSnapshot(
                scene.state_->identity.document,
                scene.state_->assets.Database());
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = scene.state_->identity.currentSceneAssetGuid.value;
        job->request.sceneName = scene.GetCurrentSceneDisplayName();
        job->request.volumeId = "irradiance_volume_" +
            std::to_string(volume->objectId.value);
        job->request.volumeName = volume->name;
        job->request.settings = settings;
        job->request.priority = volume->priority;
        job->request.forceRebake = batchForce_;
        const auto objectHash =
            hashes.recordAuthoringHashes.find(
                job->request.volumeId);
        job->request.authoringHash =
            objectHash == hashes.recordAuthoringHashes.end()
                ? std::string{}
                : objectHash->second;
        job->request.sceneHash = hashes.sceneHash;
        job->request.dependencyHash = hashes.dependencyHash;
        job->request.bakeSet = volume->bakeSet;
        job->request.scenario = volume->scenario;
        job->probeCapturePaths.reserve(probeCount);
        job->report = report;
        if (batchItemCount_ == 0u) {
            batchItemCount_ = 1u;
            batchCompletedItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
            job->report.dirtyReasons.clear();
        } else {
            job->report.dirtyReasons = lastReport_.dirtyReasons;
        }
        job->report.bakeItemCount = batchItemCount_;
        job->report.dirtyItemCount =
            static_cast<uint32_t>(queuedBakeItems_.size()) + 1u;
        job->report.completedItemCount = batchCompletedItemCount_;
        job->report.reusedItemCount = batchReusedItemCount_;
        job->report.currentItemName = volume->name;
        job->report.messages.push_back("Light probe volume scene capture requested.");

        lightProbeJob_ = std::move(job);
        lastReport_ = lightProbeJob_->report;
        hasLastReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] light probe volume scene capture requested scene=" +
            scene.state_->identity.currentSceneAssetGuid.value +
            " probes=" + std::to_string(probeCount));
        return true;
    }

    bool DocumentSceneBakeCoordinator::RequestLightingBake(
        DocumentSceneBase& scene,
        bool force,
        SceneObjectId selectedObjectId) {

        if (TOOLS::BAKING::IsLightingBakeJobRunning(GetJobState())) {
            lastReport_.warnings.push_back(
                "A lighting bake batch is already running.");
            hasLastReport_ = true;
            return false;
        }

        if (scene.state_->assets.Database().GetProjectRoot().empty()) {
            scene.state_->assets.Database().Initialize({});
        }

        TOOLS::BAKING::LightingBakeRequest request{};
        request.projectRoot =
            scene.state_->assets.Database().GetProjectRoot();
        request.sceneGuid =
            scene.state_->identity.currentSceneAssetGuid.value;
        request.sceneName = scene.GetCurrentSceneDisplayName();
        request.scenePath = scene.GetScenePath();
        request.sceneDocument = &scene.state_->identity.document;
        request.assetDatabase = &scene.state_->assets.Database();
        request.target = TOOLS::BAKING::LightingBakeTarget::All;
        request.force = force;

        TOOLS::BAKING::LightingBakeService service{};
        const TOOLS::BAKING::LightingBakeStatus status =
            service.AnalyzeLightingBakeStatus(request);
        TOOLS::BAKING::LightingBakeReport report{};
        report.action = TOOLS::BAKING::LightingBakeAction::BakeLighting;
        report.target = TOOLS::BAKING::LightingBakeTarget::All;
        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            request.projectRoot,
            request.sceneGuid);
        report.manifestPath =
            ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                request.projectRoot,
                request.sceneGuid);
        if (!status.valid) {
            report.success = false;
            report.errors = status.warnings;
            report.jobState =
                TOOLS::BAKING::LightingBakeJobState::Failed;
            lastReport_ = std::move(report);
            hasLastReport_ = true;
            return false;
        }

        queuedBakeItems_.clear();
        batchItemCount_ = 0u;
        batchCompletedItemCount_ = 0u;
        batchReusedItemCount_ = 0u;
        batchForce_ = force;
        for (const TOOLS::BAKING::LightingBakeItemStatus& item :
            status.items) {
            if (!item.enabled ||
                (selectedObjectId.value != 0u &&
                    item.objectId != selectedObjectId.value)) {
                continue;
            }
            ++batchItemCount_;
            if (!force && !item.dirty) {
                ++batchReusedItemCount_;
                continue;
            }
            queuedBakeItems_.push_back(QueuedBakeItem{
                item.kind ==
                    TOOLS::BAKING::LightingBakeItemKind::
                        ReflectionProbe,
                SceneObjectId{ item.objectId }
            });
            if (item.dirty) {
                report.dirtyReasons.push_back(
                    item.name + ": " + item.dirtyReason);
            }
        }

        report.bakeItemCount = batchItemCount_;
        report.dirtyItemCount = static_cast<uint32_t>(
            queuedBakeItems_.size());
        report.reusedItemCount = batchReusedItemCount_;
        if (batchItemCount_ == 0u) {
            report.success = false;
            report.jobState =
                TOOLS::BAKING::LightingBakeJobState::Failed;
            report.errors.push_back(
                selectedObjectId.value != 0u
                    ? "The selected object is not an enabled lighting bake object."
                    : "The scene contains no enabled lighting bake objects.");
            lastReport_ = std::move(report);
            hasLastReport_ = true;
            return false;
        }
        if (queuedBakeItems_.empty()) {
            report.jobState =
                TOOLS::BAKING::LightingBakeJobState::Completed;
            report.completedItemCount = batchReusedItemCount_;
            report.messages.push_back(
                "All lighting bake outputs are already up to date.");
            lastReport_ = std::move(report);
            hasLastReport_ = true;
            batchItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
            batchForce_ = false;
            return true;
        }

        lastReport_ = report;
        hasLastReport_ = true;
        return StartNextQueuedBake(scene);
    }

    bool DocumentSceneBakeCoordinator::StartNextQueuedBake(
        DocumentSceneBase& scene) {

        if (queuedBakeItems_.empty()) {
            lastReport_.jobState =
                TOOLS::BAKING::LightingBakeJobState::Completed;
            lastReport_.completedItemCount =
                batchCompletedItemCount_ + batchReusedItemCount_;
            lastReport_.messages.push_back(
                "Lighting bake batch completed.");
            hasLastReport_ = true;
            batchItemCount_ = 0u;
            batchCompletedItemCount_ = 0u;
            batchReusedItemCount_ = 0u;
            batchForce_ = false;
            return true;
        }

        const QueuedBakeItem next = queuedBakeItems_.front();
        queuedBakeItems_.erase(queuedBakeItems_.begin());
        const bool started = next.reflectionProbe
            ? RequestReflectionProbeBake(scene, next.objectId)
            : RequestLightProbeBake(scene, next.objectId);
        if (!started) {
            queuedBakeItems_.clear();
        }
        return started;
    }

    TOOLS::BAKING::LightingBakeJobState DocumentSceneBakeCoordinator::GetJobState() const {
        if (lightProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                lightProbeJob_->state)) {
            return lightProbeJob_->state;
        }
        if (reflectionProbeJob_ &&
            TOOLS::BAKING::IsLightingBakeJobRunning(
                reflectionProbeJob_->state)) {
            return reflectionProbeJob_->state;
        }
        return hasLastReport_
            ? lastReport_.jobState
            : TOOLS::BAKING::LightingBakeJobState::Idle;
    }

    bool DocumentSceneBakeCoordinator::HasLastReport() const {
        return hasLastReport_;
    }

    const TOOLS::BAKING::LightingBakeReport& DocumentSceneBakeCoordinator::GetLastReport() const {
        return lastReport_;
    }

} // namespace HIKARI
