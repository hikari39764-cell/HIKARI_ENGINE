#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <filesystem>
#include <memory>
#include <utility>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_Logger.h"

namespace HIKARI {
    using namespace DOCUMENT_SCENE_BAKING;

    bool DocumentSceneBakeCoordinator::RequestReflectionProbeBake(DocumentSceneBase& scene) {
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
            scene.state_->assets.Database().Initialize(std::filesystem::current_path());
        }

        const std::filesystem::path projectRoot = scene.state_->assets.Database().GetProjectRoot();
        if (projectRoot.empty()) {
            AddBakeError(report, "Project root is empty.");
        }
        if (!scene.state_->identity.currentSceneAssetGuid.IsValid()) {
            AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
        }
        if (!scene.state_->lighting.environment.reflectionProbe.enabled) {
            AddBakeError(report, "Reflection probe is disabled in the current scene.");
        }
        if (scene.state_->lighting.environment.reflectionProbe.radius <= 0.0f) {
            AddBakeError(report, "Reflection probe radius must be greater than zero.");
        }

        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
            projectRoot,
            scene.state_->identity.currentSceneAssetGuid.value);
            report.reflectionProbeCapturePath =
                ReflectionProbeOutputDirectory(projectRoot, scene.state_->identity.currentSceneAssetGuid.value) /
                "probe_000_capture.dds";
        report.reflectionProbePrefilteredPath =
            ReflectionProbeOutputDirectory(projectRoot, scene.state_->identity.currentSceneAssetGuid.value) /
            "probe_000_prefiltered.dds";
            report.reflectionProbeBrdfLutPath =
                (projectRoot / "Library" / "Generated" / "IBL" / "brdf_lut.dds").lexically_normal();
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
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = scene.state_->identity.currentSceneAssetGuid.value;
        job->request.sceneName = scene.GetCurrentSceneDisplayName();
        job->request.position = scene.state_->lighting.environment.reflectionProbe.position;
        job->request.radius = scene.state_->lighting.environment.reflectionProbe.radius;
        job->request.intensity = scene.state_->lighting.environment.reflectionProbe.intensity;
        job->request.influenceShape = ReflectionProbeInfluenceShapeName(scene.state_->lighting.environment.reflectionProbe.influenceShape);
        job->request.influenceBoxCenter = scene.state_->lighting.environment.reflectionProbe.influenceBoxCenter;
        job->request.influenceBoxSize = scene.state_->lighting.environment.reflectionProbe.influenceBoxSize;
        job->request.projectionShape = ReflectionProbeProjectionShapeName(scene.state_->lighting.environment.reflectionProbe.projectionShape);
        job->request.projectionBoxCenter = scene.state_->lighting.environment.reflectionProbe.projectionBoxCenter;
        job->request.projectionBoxSize = scene.state_->lighting.environment.reflectionProbe.projectionBoxSize;
        job->request.blendDistance = scene.state_->lighting.environment.reflectionProbe.blendDistance;
        job->request.priority = scene.state_->lighting.environment.reflectionProbe.priority;
        job->request.resolution = 128;
        job->request.prefilteredMipCount = 7;
        job->request.prefilteredSampleCount = 128;
        job->request.brdfLutSize = 256;
        job->request.brdfSampleCount = 256;
        job->request.forceRebake = true;
        job->report = report;
        job->report.messages.push_back("Reflection probe scene capture requested.");

        reflectionProbeJob_ = std::move(job);
        lastReport_ = reflectionProbeJob_->report;
        hasLastReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] reflection probe scene capture requested scene=" +
            scene.state_->identity.currentSceneAssetGuid.value);
        return true;
    }

    bool DocumentSceneBakeCoordinator::RequestLightProbeBake(DocumentSceneBase& scene) {
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
            scene.state_->assets.Database().Initialize(std::filesystem::current_path());
        }

        const std::filesystem::path projectRoot = scene.state_->assets.Database().GetProjectRoot();
        LightProbeVolumeSettings settings = scene.state_->identity.document.lightingBake.lightProbeVolume;
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
        job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
        job->request.projectRoot = projectRoot;
        job->request.sceneGuid = scene.state_->identity.currentSceneAssetGuid.value;
        job->request.sceneName = scene.GetCurrentSceneDisplayName();
        job->request.settings = settings;
        job->request.forceRebake = true;
        job->probeCapturePaths.reserve(probeCount);
        job->report = report;
        job->report.messages.push_back("Light probe volume scene capture requested.");

        lightProbeJob_ = std::move(job);
        lastReport_ = lightProbeJob_->report;
        hasLastReport_ = true;
        HIKARI_LOG_INFO("[LightingBake] light probe volume scene capture requested scene=" +
            scene.state_->identity.currentSceneAssetGuid.value +
            " probes=" + std::to_string(probeCount));
        return true;
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

    bool DocumentSceneBase::RequestReflectionProbeBake() {
        return state_->baking.RequestReflectionProbeBake(*this);
    }

    bool DocumentSceneBase::RequestLightProbeBake() {
        return state_->baking.RequestLightProbeBake(*this);
    }

    TOOLS::BAKING::LightingBakeJobState
        DocumentSceneBase::GetLightingBakeJobState() const {
        return state_->baking.GetJobState();
    }

    bool DocumentSceneBase::HasLastLightingBakeReport() const {
        return state_->baking.HasLastReport();
    }

    const TOOLS::BAKING::LightingBakeReport&
        DocumentSceneBase::GetLastLightingBakeReport() const {
        return state_->baking.GetLastReport();
    }



} // namespace HIKARI
