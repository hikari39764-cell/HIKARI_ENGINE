#include "HIKARI_LightingBakeService.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Lighting/HIKARI_LightingBakeDependencyHash.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>
#include <system_error>
#include <utility>

namespace HIKARI::TOOLS::BAKING {

namespace {

void AddError(LightingBakeReport& report, std::string message) {
    report.success = false;
    report.errors.push_back(std::move(message));
}

std::string ToLowerGenericPath(std::filesystem::path path) {
    return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
}

bool HasPathPrefix(const std::filesystem::path& path, const std::filesystem::path& prefix) {
    const std::string lhs = ToLowerGenericPath(path);
    const std::string rhs = ToLowerGenericPath(prefix);
    if (lhs == rhs) {
        return true;
    }
    if (lhs.size() <= rhs.size()) {
        return false;
    }
    return lhs.compare(0, rhs.size(), rhs) == 0 && lhs[rhs.size()] == '/';
}

void FillBakePaths(const LightingBakeRequest& request, LightingBakeReport& report) {
    if (!request.projectRoot.empty() && !request.sceneGuid.empty()) {
        report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid);
        report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(request.projectRoot, request.sceneGuid);
    }
}

// 光照贴图の無い面の範囲を丸ごと覆う焼き込み探針の体積。全面を覆う箱（格子の外へ半間隔）から、探針の重みが九割以上
// 残る所まで（HikariEvaluateBakedVolumeInfluence の smoothstep が 0.1 になる blendDistance の 0.19 倍）を覆うと見なす。
const LIGHTING_AUTHORING::IrradianceVolumeAuthoring* FindCoveringVolume(
    const std::vector<LIGHTING_AUTHORING::IrradianceVolumeAuthoring>& volumes,
    const ASSETS::LIGHTING::LightmapBakeRecord& record) {
    for (const LIGHTING_AUTHORING::IrradianceVolumeAuthoring& volume : volumes) {
        if (!volume.settings.enabled)
            continue;
        const MATH::Vec3 center = volume.settings.origin + volume.settings.size * 0.5f;
        const float fade = 0.19f * volume.settings.blendDistance;
        const MATH::Vec3 half =
            GetBakedLightProbeVolumeCoveredSize(volume.settings) * 0.5f + MATH::Vec3{fade, fade, fade};
        const MATH::Vec3 boxMin = center - half;
        const MATH::Vec3 boxMax = center + half;
        const MATH::Vec3& lo = record.noLightmapBoundsMin;
        const MATH::Vec3& hi = record.noLightmapBoundsMax;
        if (lo.x >= boxMin.x && lo.y >= boxMin.y && lo.z >= boxMin.z && hi.x <= boxMax.x && hi.y <= boxMax.y &&
            hi.z <= boxMax.z)
            return &volume;
    }
    return nullptr;
}

bool ProjectRelativeOutputExists(const std::filesystem::path& projectRoot, const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec{};
    return std::filesystem::exists(PROJECT_PATHS::ResolveProjectPath(projectRoot, path), ec) && !ec;
}

} // namespace

LightingBakeRequest LightingBakeService::BuildRequest(DocumentSceneBase& scene) {
    LightingBakeRequest request{};
    request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
    request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
    request.sceneName = scene.GetCurrentSceneDisplayName();
    request.scenePath = scene.GetScenePath();
    request.sceneDocument = &scene.GetSceneDocument();
    request.assetDatabase = &scene.GetAssetDatabase();
    request.target = LightingBakeTarget::All;
    return request;
}

std::string LightingBakeService::GetBlockReason(const DocumentSceneBase& scene) const {
    if (scene.IsRuntimePlayActive())
        return "Stop Play before changing lighting bake outputs.";
    if (IsLightingBakeJobRunning(scene.GetLightingBakeJobState()))
        return "A lighting bake is running. Wait for completion before starting or clearing outputs.";
    if (scene.GetAssetDatabase().GetAssetTaskService().HasActiveTasks())
        return "Wait for asset import to finish before baking or clearing lighting.";
    if (!scene.GetCurrentSceneAssetGuid().IsValid())
        return "Save the scene before baking lighting.";
    return {};
}

LightingBakeStatus LightingBakeService::AnalyzeLightingBakeStatus(const LightingBakeRequest& request) const {
    LightingBakeStatus status{};
    if (request.sceneDocument == nullptr || request.assetDatabase == nullptr || request.projectRoot.empty() ||
        request.sceneGuid.empty()) {
        status.warnings.push_back("Save the scene before analyzing lighting bake state.");
        return status;
    }

    const ASSETS::LIGHTING::LightingBakeHashSnapshot hashes =
        ASSETS::LIGHTING::BuildLightingBakeHashSnapshot(*request.sceneDocument, *request.assetDatabase);
    status.sceneHash = hashes.sceneHash;
    status.dependencyHash = hashes.dependencyHash;
    for (const std::string& missing : hashes.missingAssetIds) {
        status.warnings.push_back("Missing lighting dependency: " + missing);
    }
    for (const std::string& unreadable : hashes.unreadableAssets) {
        status.warnings.push_back("Unreadable lighting dependency: " + unreadable);
    }

    ASSETS::LIGHTING::LightingBakeManifest manifest{};
    const std::filesystem::path manifestPath =
        ASSETS::LIGHTING::BuildLightingBakeManifestPath(request.projectRoot, request.sceneGuid);
    std::string manifestMessage{};
    const bool hasManifest = std::filesystem::exists(manifestPath) &&
                             ASSETS::LIGHTING::LoadLightingBakeManifest(manifestPath, manifest, &manifestMessage);

    const auto classify = [&](LightingBakeItemStatus& item, const std::string& recordAuthoringHash,
                              const std::string& recordCaptureSceneHash, const std::string& recordDependencyHash,
                              bool outputExists) {
        using enum LightingBakeItemState;
        if (!item.enabled) {
            item.state = Disabled;
        } else if (!hasManifest) {
            item.state = NoManifest;
        } else if (recordAuthoringHash.empty()) {
            item.state = NoRecord;
        } else if (recordAuthoringHash != item.authoringHash) {
            item.state = SettingsChanged;
        } else if (recordCaptureSceneHash != hashes.sceneHash) {
            item.state = SceneChanged;
        } else if (!hashes.unreadableAssets.empty()) {
            // 中身を読めない資産の先の依存は確かめられないので、焼き込み済みの結果を最新とは見なさない。
            item.state = DependencyUnreadable;
        } else if (recordDependencyHash != hashes.dependencyHash) {
            item.state = DependencyChanged;
        } else if (!outputExists) {
            item.state = OutputMissing;
        } else {
            item.state = Ready;
        }
        item.dirty = item.state != Ready && item.state != Disabled;
        ++status.totalCount;
        if (item.dirty) {
            ++status.dirtyCount;
        } else if (item.enabled) {
            ++status.readyCount;
        }
    };

    for (const LIGHTING_AUTHORING::ReflectionProbeAuthoring& probe :
         LIGHTING_AUTHORING::CollectReflectionProbes(*request.sceneDocument)) {
        LightingBakeItemStatus item{};
        item.kind = LightingBakeItemKind::ReflectionProbe;
        item.objectId = probe.objectId.value;
        item.name = probe.name;
        item.recordId = "reflection_probe_" + std::to_string(probe.objectId.value);
        item.enabled = probe.settings.enabled;
        const auto hash = hashes.recordAuthoringHashes.find(item.recordId);
        if (hash != hashes.recordAuthoringHashes.end()) {
            item.authoringHash = hash->second;
        }
        const auto record = std::find_if(manifest.reflectionProbes.begin(), manifest.reflectionProbes.end(),
                                         [&item](const ASSETS::LIGHTING::ReflectionProbeBakeRecord& candidate) {
                                             return candidate.id == item.recordId;
                                         });
        classify(item, record == manifest.reflectionProbes.end() ? std::string{} : record->authoringHash,
                 record == manifest.reflectionProbes.end() ? std::string{} : record->captureSceneHash,
                 record == manifest.reflectionProbes.end() ? std::string{} : record->dependencyHash,
                 record != manifest.reflectionProbes.end() &&
                     ProjectRelativeOutputExists(request.projectRoot, record->prefilteredCubemapPath));
        status.items.push_back(std::move(item));
    }

    const std::vector<LIGHTING_AUTHORING::IrradianceVolumeAuthoring> bakedVolumes =
        LIGHTING_AUTHORING::CollectBakedIrradianceVolumes(*request.sceneDocument);
    for (const LIGHTING_AUTHORING::IrradianceVolumeAuthoring& volume : bakedVolumes) {
        LightingBakeItemStatus item{};
        item.kind = LightingBakeItemKind::IrradianceVolume;
        item.objectId = volume.objectId.value;
        item.name = volume.name;
        item.recordId = "irradiance_volume_" + std::to_string(volume.objectId.value);
        item.enabled = volume.settings.enabled;
        const auto hash = hashes.recordAuthoringHashes.find(item.recordId);
        if (hash != hashes.recordAuthoringHashes.end()) {
            item.authoringHash = hash->second;
        }
        const auto record = std::find_if(
            manifest.lightProbes.begin(), manifest.lightProbes.end(),
            [&item](const ASSETS::LIGHTING::LightProbeBakeRecord& candidate) { return candidate.id == item.recordId; });
        classify(item, record == manifest.lightProbes.end() ? std::string{} : record->authoringHash,
                 record == manifest.lightProbes.end() ? std::string{} : record->captureSceneHash,
                 record == manifest.lightProbes.end() ? std::string{} : record->dependencyHash,
                 record != manifest.lightProbes.end() &&
                     ProjectRelativeOutputExists(request.projectRoot, record->shDataPath));
        status.items.push_back(std::move(item));
    }

    for (const SceneObjectData& object : request.sceneDocument->objects) {
        const auto component =
            std::find_if(object.components.begin(), object.components.end(),
                         [](const SceneComponentData& candidate) { return candidate.type == "ModelComponent"; });
        if (component == object.components.end()) {
            continue;
        }

        LightingBakeItemStatus item{};
        item.kind = LightingBakeItemKind::LightmapObject;
        item.objectId = object.id.value;
        item.name = object.name;
        item.recordId = "lightmap_object_" + std::to_string(object.id.value);
        const bool authoredLightmapTarget = component->properties.value("renderStatic", false) &&
                                            component->properties.value("includeInLightingBake", true);
        item.enabled = object.enabled && component->properties.value("visible", true) && authoredLightmapTarget;
        const auto hash = hashes.recordAuthoringHashes.find(item.recordId);
        if (hash != hashes.recordAuthoringHashes.end()) {
            item.authoringHash = hash->second;
        }
        const auto record =
            std::find_if(manifest.lightmaps.begin(), manifest.lightmaps.end(),
                         [&item](const ASSETS::LIGHTING::LightmapBakeRecord& candidate) {
                             return candidate.id == item.recordId || candidate.objectId == item.objectId;
                         });
        if (!authoredLightmapTarget && record == manifest.lightmaps.end()) {
            continue;
        }
        const bool hasRecord = record != manifest.lightmaps.end();
        // 物体全体に光照贴图が無い記録は出力が無くて正しい。
        const bool wholeObjectFailed = hasRecord && record->pages.empty() && !record->noLightmapReason.empty();
        classify(item, hasRecord ? record->authoringHash : std::string{},
                 hasRecord ? record->captureSceneHash : std::string{},
                 hasRecord ? record->dependencyHash : std::string{},
                 wholeObjectFailed || (hasRecord && !record->pages.empty() &&
                                       std::all_of(record->pages.begin(), record->pages.end(), [&](const auto& page) {
                                           return !page.surfaceBindings.empty() &&
                                                  ProjectRelativeOutputExists(request.projectRoot, page.texturePath);
                                       })));
        // 光照贴图の無い面は今の入力では焼き直しても同じなので、待ち（dirty）にも完了にも数えず、別に知らせる。
        if (item.state == LightingBakeItemState::Ready && !record->noLightmapReason.empty()) {
            item.state = wholeObjectFailed ? LightingBakeItemState::NoLightmap : LightingBakeItemState::PartialLightmap;
            item.noLightmapReason = record->noLightmapReason;
            const auto* covering = FindCoveringVolume(bakedVolumes, *record);
            item.coveringVolume = covering != nullptr ? covering->name : std::string{};
            --status.readyCount;
            ++status.noLightmapCount;
            status.uncoveredCount += covering == nullptr ? 1u : 0u;
        }
        status.items.push_back(std::move(item));
    }

    status.valid = true;
    return status;
}

LightingBakeReport LightingBakeService::ClearLightingBake(const LightingBakeRequest& request) const {
    LightingBakeReport report{};
    report.action = LightingBakeAction::ClearBake;
    report.target = request.target;
    FillBakePaths(request, report);

    if (request.projectRoot.empty()) {
        AddError(report, "Project root is empty.");
        return report;
    }
    if (request.sceneGuid.empty()) {
        AddError(report, "Current scene has no stable asset GUID.");
        return report;
    }

    const std::filesystem::path projectRoot = PATHS::MakeAbsoluteNormalized(request.projectRoot);
    const std::filesystem::path bakeRoot = PATHS::MakeAbsoluteNormalized(report.bakeRoot);
    const std::filesystem::path lightingRoot =
        PATHS::MakeAbsoluteNormalized(request.projectRoot / "Library" / "Generated" / "Lighting");

    if (!HasPathPrefix(bakeRoot, projectRoot) || !HasPathPrefix(bakeRoot, lightingRoot)) {
        AddError(report, "Refused to clear unsafe bake folder: " + bakeRoot.generic_string());
        return report;
    }

    std::error_code ec{};
    if (!std::filesystem::exists(bakeRoot, ec)) {
        report.messages.push_back("Bake folder is already empty: " + bakeRoot.generic_string());
        HIKARI_LOG_INFO("[LightingBake] clear skipped, folder missing: " + bakeRoot.generic_string());
        return report;
    }

    const uintmax_t removed = std::filesystem::remove_all(bakeRoot, ec);
    if (ec) {
        AddError(report, "Failed to clear bake folder: " + ec.message());
        return report;
    }

    report.bakeFolderCleared = true;
    report.messages.push_back("Bake folder cleared: " + bakeRoot.generic_string());
    report.messages.push_back("Removed entries: " + std::to_string(removed));
    HIKARI_LOG_INFO("[LightingBake] clear bake folder scene=" + request.sceneGuid +
                    " path=" + bakeRoot.generic_string());
    return report;
}

} // namespace HIKARI::TOOLS::BAKING
