#include "HIKARI_LightingBakeService.h"

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_ReflectionProbeBaker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace HIKARI::TOOLS::BAKING {

    namespace {

        void AddError(LightingBakeReport& report, std::string message) {
            report.success = false;
            report.errors.push_back(std::move(message));
        }

        std::string ToLowerGenericPath(std::filesystem::path path) {
            return TEXT::ToLowerAsciiCopy(
                path.lexically_normal().generic_string());
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

        std::string MakeBakeGuid() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &time);
#else
            localtime_r(&time, &local);
#endif

            std::ostringstream oss{};
            oss << "bake_" << std::put_time(&local, "%Y%m%d_%H%M%S");
            return oss.str();
        }

        void FillBakePaths(const LightingBakeRequest& request, LightingBakeReport& report) {
            if (!request.projectRoot.empty() && !request.sceneGuid.empty()) {
                report.bakeRoot = ASSETS::LIGHTING::BuildLightingBakeRoot(
                    request.projectRoot,
                    request.sceneGuid);
                report.manifestPath = ASSETS::LIGHTING::BuildLightingBakeManifestPath(
                    request.projectRoot,
                    request.sceneGuid);
            }
        }

        bool ValidateStableSceneForPrepare(const LightingBakeRequest& request, LightingBakeReport& report) {
            if (request.projectRoot.empty()) {
                AddError(report, "Project root is empty.");
            }
            if (request.sceneGuid.empty()) {
                AddError(report, "Current scene has no stable asset GUID. Save scene before preparing lighting bake.");
            }
            if (!request.sceneDocument) {
                AddError(report, "SceneDocument is missing.");
            }
            return report.errors.empty();
        }

        LightingBakeRequest BuildRequestFromScene(DocumentSceneBase& scene, LightingBakeTarget target) {
            LightingBakeRequest request{};
            request.projectRoot = scene.GetAssetDatabase().GetProjectRoot();
            request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
            request.sceneName = scene.GetCurrentSceneDisplayName();
            request.scenePath = scene.GetScenePath();
            request.sceneDocument = &scene.GetSceneDocument();
            request.target = target;
            return request;
        }

    } // namespace

    LightingBakeReport LightingBakeService::ValidateLightingBakeSetup(const LightingBakeRequest& request) const {
        LightingBakeReport report{};
        report.action = LightingBakeAction::ValidateOnly;
        report.target = request.target;
        FillBakePaths(request, report);

        if (request.projectRoot.empty()) {
            AddError(report, "Project root is empty.");
        }
        if (request.sceneGuid.empty()) {
            AddError(report, "Current scene has no stable asset GUID. Save scene before preparing lighting bake.");
        }
        if (!request.sceneDocument) {
            AddError(report, "SceneDocument is missing.");
        }

        AppendSceneAuthoringSummary(request, report);
        report.messages.push_back("Lighting bake manifest can be prepared independently from GPU bake jobs.");

        HIKARI_LOG_INFO("[LightingBake] validate scene=" + request.sceneGuid +
            " success=" + std::string(report.success ? "true" : "false"));
        return report;
    }

    LightingBakeReport LightingBakeService::PrepareLightingBakeManifest(const LightingBakeRequest& request) const {
        LightingBakeReport report{};
        report.action = LightingBakeAction::PrepareManifest;
        report.target = request.target;
        FillBakePaths(request, report);

        // Scene capture bake は GPU job として Scene 側に委譲する。
        if (!ValidateStableSceneForPrepare(request, report)) {
            AppendSceneAuthoringSummary(request, report);
            return report;
        }

        std::error_code ec{};
        std::filesystem::create_directories(report.bakeRoot, ec);
        if (ec) {
            AddError(report, "Failed to create bake folder: " + ec.message());
            return report;
        }
        report.bakeFolderCreated = true;

        const ASSETS::LIGHTING::LightingBakeManifest manifest = BuildEmptyManifest(request);
        std::string saveMessage{};
        // 空の manifest を保存して bake 出力先を確定する。
        if (!ASSETS::LIGHTING::SaveLightingBakeManifest(report.manifestPath, manifest, &saveMessage)) {
            AddError(report, saveMessage);
            return report;
        }

        report.manifestWritten = true;
        report.messages.push_back(saveMessage);
        report.messages.push_back("Lighting bake manifest prepared: " + report.manifestPath.generic_string());
        AppendSceneAuthoringSummary(request, report);

        HIKARI_LOG_INFO("[LightingBake] manifest prepared scene=" + request.sceneGuid +
            " path=" + report.manifestPath.generic_string());
        return report;
    }

    LightingBakeReport LightingBakeService::BakeReflectionProbesOnly(DocumentSceneBase& scene) const {
        const LightingBakeRequest request =
            BuildRequestFromScene(scene, LightingBakeTarget::ReflectionProbesOnly);

        LightingBakeReport report{};
        report.action = LightingBakeAction::BakeReflectionProbes;
        report.target = request.target;
        FillBakePaths(request, report);

        // Scene capture bake は GPU job として Scene 側に委譲する。
        if (!scene.RequestReflectionProbeBake()) {
            if (scene.HasLastLightingBakeReport()) {
                return scene.GetLastLightingBakeReport();
            }
            AddError(report, "Failed to request reflection probe scene capture.");
            return report;
        }
        if (scene.HasLastLightingBakeReport()) {
            return scene.GetLastLightingBakeReport();
        }
        return report;
    }

    LightingBakeReport LightingBakeService::BakeLightProbesOnly(DocumentSceneBase& scene) const {
        const LightingBakeRequest request =
            BuildRequestFromScene(scene, LightingBakeTarget::LightProbesOnly);

        LightingBakeReport report{};
        report.action = LightingBakeAction::BakeLightProbes;
        report.target = request.target;
        FillBakePaths(request, report);

        // Scene capture bake は GPU job として Scene 側に委譲する。
        if (!scene.RequestLightProbeBake()) {
            if (scene.HasLastLightingBakeReport()) {
                return scene.GetLastLightingBakeReport();
            }
            AddError(report, "Failed to request light probe volume scene capture.");
            return report;
        }
        if (scene.HasLastLightingBakeReport()) {
            return scene.GetLastLightingBakeReport();
        }
        return report;
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
        const std::filesystem::path lightingRoot = PATHS::MakeAbsoluteNormalized(
            request.projectRoot / "Library" / "Generated" / "Lighting");

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

    ASSETS::LIGHTING::LightingBakeManifest LightingBakeService::BuildEmptyManifest(
        const LightingBakeRequest& request) const {

        ASSETS::LIGHTING::LightingBakeManifest manifest{};
        manifest.version = ASSETS::LIGHTING::kLightingBakeManifestVersion;
        manifest.sceneGuid = request.sceneGuid;
        manifest.bakeGuid = MakeBakeGuid();
        manifest.bakeVersion = 1;
        manifest.generatedRoot = PROJECT_PATHS::MakeProjectRelativeString(
            request.projectRoot,
            ASSETS::LIGHTING::BuildLightingBakeRoot(request.projectRoot, request.sceneGuid));
        return manifest;
    }

    void LightingBakeService::AppendSceneAuthoringSummary(
        const LightingBakeRequest& request,
        LightingBakeReport& report) const {

        if (!request.sceneDocument) {
            return;
        }

        const SceneEnvironment& environment = request.sceneDocument->environment;
        report.messages.push_back("Scene objects: " + std::to_string(request.sceneDocument->objects.size()));
        report.messages.push_back("Scene systems: " + std::to_string(request.sceneDocument->systems.size()));

        if (environment.sky.enabled && environment.sky.skyAsset.empty()) {
            report.warnings.push_back("Sky is enabled but sky asset is empty.");
        }
        if (environment.reflectionProbe.enabled && environment.reflectionProbe.sourceCubemapAsset.empty()) {
            report.messages.push_back("Reflection probe source cubemap override is empty; scene capture will be used.");
        }
        if (environment.reflectionProbe.enabled && environment.reflectionProbe.radius <= 0.0f) {
            report.warnings.push_back("Reflection probe radius should be greater than zero.");
        }

        LightProbeVolumeSettings lightProbeVolume = request.sceneDocument->lightingBake.lightProbeVolume;
        ClampLightProbeVolumeSettings(lightProbeVolume);
        if (lightProbeVolume.enabled) {
            report.messages.push_back("Light probe volume grid: " +
                std::to_string(lightProbeVolume.countX) + "x" +
                std::to_string(lightProbeVolume.countY) + "x" +
                std::to_string(lightProbeVolume.countZ) +
                " probes=" + std::to_string(GetLightProbeVolumeProbeCount(lightProbeVolume)));
            report.messages.push_back("Light probe capture resolution: " +
                std::to_string(lightProbeVolume.captureResolution));
        } else {
            report.warnings.push_back("Light probe volume is disabled.");
        }

        if (environment.ambientOcclusion.enabled) {
            if (environment.ambientOcclusion.radius <= 0.0f) {
                report.warnings.push_back("Ambient occlusion radius should be greater than zero.");
            }
            if (environment.ambientOcclusion.sampleCount < 8u) {
                report.warnings.push_back("Ambient occlusion sample count is very low.");
            }
        }
    }

} // namespace HIKARI::TOOLS::BAKING
