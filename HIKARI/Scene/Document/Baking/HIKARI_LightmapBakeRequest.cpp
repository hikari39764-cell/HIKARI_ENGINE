#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/HIKARI_AssetGuid.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Scene/Document/Baking/HIKARI_LightingTransportSceneBuilder.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Lighting/HIKARI_SceneLightingSet.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {
using namespace DOCUMENT_SCENE_BAKING;

namespace {

// 光照贴图の無い面（atlas に割り当てられなかった三角形）の理由と世界空間の範囲を記録に書く。物体全体が作れなかった
// 時も、遮蔽物として三角形は場面に入っている。
void DescribeSurfacesWithoutLightmap(const RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene& scene,
                                     const RENDER3D::LIGHTING::TRANSPORT::LightingTransportSurfaceTarget& target,
                                     ASSETS::LIGHTING::LightmapBakeRecord& record) {
    bool found = false;
    for (const auto& triangle : scene.triangles) {
        if (triangle.objectId != target.objectId ||
            (target.eligible && triangle.lightmapAllocationIndex != ASSETS::LIGHTING::kInvalidLightmapSurfaceIndex))
            continue;
        for (const MATH::Vec3& p : triangle.position) {
            MATH::Vec3& lo = record.noLightmapBoundsMin;
            MATH::Vec3& hi = record.noLightmapBoundsMax;
            lo = found ? MATH::Vec3{(std::min)(lo.x, p.x), (std::min)(lo.y, p.y), (std::min)(lo.z, p.z)} : p;
            hi = found ? MATH::Vec3{(std::max)(hi.x, p.x), (std::max)(hi.y, p.y), (std::max)(hi.z, p.z)} : p;
            found = true;
        }
    }
    if (!target.eligible) {
        record.noLightmapReason = target.eligibilityMessage;
        return;
    }
    if (!found)
        return;
    const std::pair<uint32_t, const char*> reasons[] = {
        {target.invalidUvSurfaceCount, "invalid or missing UV1"},
        {target.alphaMaskedSurfaceCount, "alpha-masked (occluder only)"},
        {target.blendedSurfaceCount, "blended"},
        {target.unsupportedSurfaceCount, "unsupported vertex layout"},
    };
    record.noLightmapReason = "Surfaces without lightmap:";
    for (const auto& [count, reason] : reasons)
        if (count > 0u)
            record.noLightmapReason += " " + std::string(reason) + " " + std::to_string(count) + ",";
    record.noLightmapReason.back() = '.';
}

} // namespace

RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene DocumentSceneBakeCoordinator::BuildTransportScene(
    DocumentSceneBase& scene, const std::filesystem::path& projectRoot,
    const RENDER3D::LIGHTING::SceneLightingSet& lighting) {
    RenderSubmissionSystem::SetRenderViewPurpose(RENDER3D::RenderViewPurpose::Game);
    RenderSubmissionSystem::SetAssetContext(&scene.state_->assets.Registry(), projectRoot,
                                            &scene.state_->assets.Models());
    scene.state_->runtime.systemScheduler.PreRender(scene.state_->runtime.world, HIKARI::TIME::GetFrameContext());
    return BuildLightingTransportScene(scene.state_->identity.document, scene.state_->lighting.environment, lighting,
                                       RenderSubmissionSystem::GetSceneRenderCache(), &scene.GetSkyManager());
}

bool DocumentSceneBakeCoordinator::RequestLightmapBake(DocumentSceneBase& scene, SceneObjectId targetObjectId) {
    if (TOOLS::BAKING::IsLightingBakeJobRunning(GetJobState())) {
        lastReport_.warnings.push_back("A lighting bake job is already running.");
        hasLastReport_ = true;
        return false;
    }

    TOOLS::BAKING::LightingBakeReport report{};
    report.action = TOOLS::BAKING::LightingBakeAction::BakeLightmaps;
    report.target = TOOLS::BAKING::LightingBakeTarget::LightmapsOnly;
    report.lightingTransportBackend = "DxrPathTracing";
    SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Requested);

    if (scene.state_->assets.Database().GetProjectRoot().empty()) {
        scene.state_->assets.Database().Initialize({});
    }
    const std::filesystem::path projectRoot = scene.state_->assets.Database().GetProjectRoot();
    if (projectRoot.empty()) {
        AddBakeError(report, "Project root is empty.");
    }
    if (!scene.state_->identity.currentSceneAssetGuid.IsValid()) {
        AddBakeError(report, "Current scene has no stable asset GUID. Save scene before baking.");
    }
    if (targetObjectId.value == 0u) {
        AddBakeError(report, "Surface lightmap bake requires a stable target object.");
    }

    const RENDER3D::LIGHTING::SceneLightingSet lighting =
        LIGHTING_AUTHORING::BuildRuntimeSceneLightingSet(scene.state_->identity.document, scene.state_->runtime.world);
    RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene transportScene =
        BuildTransportScene(scene, projectRoot, lighting);
    const auto target =
        std::find_if(transportScene.targets.begin(), transportScene.targets.end(),
                     [targetObjectId](const auto& candidate) { return candidate.objectId == targetObjectId.value; });
    if (target == transportScene.targets.end()) {
        AddBakeError(report, "Target is not a visible Static model included in lighting bake.");
    }

    report.dxrBackendAvailable = TOOLS::BAKING::DxrProgressiveLightmapBaker::IsAvailable(GFX::GetCurrentContext());
    if (!report.dxrBackendAvailable) {
        AddBakeError(
            report,
            "DXR 1.1 and Shader Model 6.5 are required for progressive lightmap baking. Runtime Probe/IBL fallback remains active.");
    }
    if (!report.errors.empty()) {
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
        lastReport_ = std::move(report);
        hasLastReport_ = true;
        return false;
    }

    const ASSETS::LIGHTING::LightingBakeHashSnapshot hashes = ASSETS::LIGHTING::BuildLightingBakeHashSnapshot(
        scene.state_->identity.document, scene.state_->assets.Database());
    const std::string recordId = "lightmap_object_" + std::to_string(targetObjectId.value);
    const auto authoringHash = hashes.recordAuthoringHashes.find(recordId);
    ASSETS::LIGHTING::LightmapBakeRecord record{};
    record.id = recordId;
    record.name = target->name;
    record.objectId = targetObjectId.value;
    record.authoringHash = authoringHash != hashes.recordAuthoringHashes.end() ? authoringHash->second : std::string{};
    record.captureSceneHash = hashes.sceneHash;
    record.dependencyHash = hashes.dependencyHash;
    record.scenario = lighting.activeScenario.empty() ? "Default" : lighting.activeScenario;
    DescribeSurfacesWithoutLightmap(transportScene, *target, record);

    report.dirtyReasons = lastReport_.dirtyReasons;
    report.bakeItemCount = batchItemCount_;
    report.dirtyItemCount = static_cast<uint32_t>(queuedBakeItems_.size()) + 1u;
    report.completedItemCount = batchCompletedItemCount_;
    report.reusedItemCount = batchReusedItemCount_;
    report.currentItemName = record.name;

    // 光照贴图を作れない物体（UV 生成の失敗など）は失敗を記録して次へ進む。入力が変わるまで同じ失敗を焼き直さず、
    // 一つの失敗で一括の焼き込み全体を止めない。この物体は焼き込み探針・動的 GI・空の光の順で照らされる。
    if (!target->eligible) {
        std::string message{};
        if (!CommitLightmapRecord(scene, projectRoot, scene.state_->identity.currentSceneAssetGuid.value, record,
                                  report, message)) {
            AddBakeError(report, message);
            SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Failed);
            lastReport_ = std::move(report);
            hasLastReport_ = true;
            return false;
        }
        batchWarnings_.push_back(record.name + ": no lightmap, " + record.noLightmapReason);
        report.completedItemCount = ++batchCompletedItemCount_ + batchReusedItemCount_;
        SetBakeJobState(report, TOOLS::BAKING::LightingBakeJobState::Completed);
        lastReport_ = std::move(report);
        hasLastReport_ = true;
        return StartNextQueuedBake(scene);
    }

    auto job = std::make_unique<LightmapJob>();
    job->state = TOOLS::BAKING::LightingBakeJobState::Requested;
    job->report = report;
    job->targetIndex = static_cast<size_t>(std::distance(transportScene.targets.begin(), target));
    job->transportScene = std::move(transportScene);
    job->objectId = targetObjectId;
    job->projectRoot = projectRoot;
    job->sceneGuid = scene.state_->identity.currentSceneAssetGuid.value;
    const auto& resolvedTarget = job->transportScene.targets[job->targetIndex];
    job->settings.sampleCount = scene.state_->identity.document.lightingBake.sampleCount;
    job->settings.bounceCount = scene.state_->identity.document.lightingBake.bounceCount;
    job->settings.outputResolution = resolvedTarget.pages.front().outputResolution;
    job->outputDirectory =
        LightmapOutputDirectory(projectRoot, job->sceneGuid) / (record.id + "_" + GenerateAssetGuid().value);
    job->outputPath = job->outputDirectory / "page_0.dds";
    job->record = std::move(record);

    job->report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, job->sceneGuid);
    job->report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(projectRoot, job->sceneGuid);
    job->report.lightmapPath = job->outputPath;
    job->report.lightmapResolution = job->settings.outputResolution;
    job->report.transportRequestedSamples =
        job->settings.sampleCount * static_cast<uint32_t>(resolvedTarget.pages.size());
    job->report.messages.push_back(
        "Surface lightmap uses UV1, static opaque geometry, RGB16F diffuse irradiance, and per-record runtime fallback.");
    job->report.messages.push_back(resolvedTarget.eligibilityMessage);

    lightmapJob_ = std::move(job);
    lastReport_ = lightmapJob_->report;
    hasLastReport_ = true;
    return true;
}

} // namespace HIKARI
