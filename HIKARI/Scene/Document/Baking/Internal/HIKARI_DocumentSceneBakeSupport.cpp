#include "Scene/Document/Baking/Internal/HIKARI_DocumentSceneBakeSupport.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <utility>

#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Core/HIKARI_Logger.h"
#include "Tools/Baking/HIKARI_ProbeCubemapCaptureTarget.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {
const char* ReflectionProbeInfluenceShapeName(ReflectionProbeInfluenceShape shape) {
    return shape == ReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
}

const char* ReflectionProbeProjectionShapeName(ReflectionProbeProjectionShape shape) {
    return shape == ReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
}

const char* ProbeFaceName(uint32_t faceIndex) {
    static constexpr const char* kFaceNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    return faceIndex < 6u ? kFaceNames[faceIndex] : "?";
}

void AddBakeError(TOOLS::BAKING::LightingBakeReport& report, std::string message) {
    HIKARI_LOG_ERROR("[LightingBake] " + message);
    report.success = false;
    report.errors.push_back(std::move(message));
}

std::filesystem::path ReflectionProbeOutputDirectory(const std::filesystem::path& projectRoot,
                                                     const std::string& sceneGuid) {
    return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) / "reflection_probes").lexically_normal();
}

std::filesystem::path LightProbeOutputDirectory(const std::filesystem::path& projectRoot,
                                                const std::string& sceneGuid) {
    return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) / "light_probes").lexically_normal();
}

std::filesystem::path LightProbeCaptureDirectory(const std::filesystem::path& projectRoot,
                                                 const std::string& sceneGuid) {
    return (LightProbeOutputDirectory(projectRoot, sceneGuid) / "captures").lexically_normal();
}

std::filesystem::path LightProbeCapturePath(const std::filesystem::path& projectRoot, const std::string& sceneGuid,
                                            uint32_t probeIndex) {
    std::ostringstream filename{};
    filename << "probe_" << std::setfill('0') << std::setw(3) << probeIndex << "_capture.dds";
    return (LightProbeCaptureDirectory(projectRoot, sceneGuid) / filename.str()).lexically_normal();
}

std::filesystem::path LightmapOutputDirectory(const std::filesystem::path& projectRoot, const std::string& sceneGuid) {
    return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) / "lightmaps").lexically_normal();
}

static std::string MakeBakeGuid() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&local, &time);
#endif

    std::ostringstream oss{};
    oss << "bake_" << std::put_time(&local, "%Y%m%d_%H%M%S");
    return oss.str();
}

ASSETS::LIGHTING::LightingBakeManifest LoadOrCreateLightingBakeManifest(const std::filesystem::path& manifestPath,
                                                                        const std::filesystem::path& projectRoot,
                                                                        const std::string& sceneGuid,
                                                                        std::vector<std::string>& warnings) {
    ASSETS::LIGHTING::LightingBakeManifest manifest{};
    std::string loadMessage{};
    if (!manifestPath.empty() && std::filesystem::exists(manifestPath) &&
        !ASSETS::LIGHTING::LoadLightingBakeManifest(manifestPath, manifest, &loadMessage)) {
        warnings.push_back(loadMessage);
        manifest = {};
    }

    manifest.version = ASSETS::LIGHTING::kLightingBakeManifestVersion;
    manifest.sceneGuid = sceneGuid;
    if (manifest.bakeGuid.empty()) {
        manifest.bakeGuid = MakeBakeGuid();
    }
    manifest.bakeVersion = 1;
    manifest.generatedRoot = PROJECT_PATHS::MakeProjectRelativeString(
        projectRoot, ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid));
    return manifest;
}

void ReplaceReflectionProbeRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                                  const ASSETS::LIGHTING::ReflectionProbeBakeRecord& record) {
    manifest.reflectionProbes.erase(
        std::remove_if(manifest.reflectionProbes.begin(), manifest.reflectionProbes.end(),
                       [&record](const ASSETS::LIGHTING::ReflectionProbeBakeRecord& existing) {
                           return existing.id == record.id;
                       }),
        manifest.reflectionProbes.end());
    manifest.reflectionProbes.push_back(record);
    std::stable_sort(
        manifest.reflectionProbes.begin(), manifest.reflectionProbes.end(),
        [](const ASSETS::LIGHTING::ReflectionProbeBakeRecord& lhs,
           const ASSETS::LIGHTING::ReflectionProbeBakeRecord& rhs) { return lhs.priority > rhs.priority; });
}

void ReplaceLightProbeVolumeRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                                   const ASSETS::LIGHTING::LightProbeBakeRecord& record) {
    manifest.lightProbes.erase(std::remove_if(manifest.lightProbes.begin(), manifest.lightProbes.end(),
                                              [&record](const ASSETS::LIGHTING::LightProbeBakeRecord& existing) {
                                                  return existing.id == record.id;
                                              }),
                               manifest.lightProbes.end());
    manifest.lightProbes.push_back(record);
    std::stable_sort(manifest.lightProbes.begin(), manifest.lightProbes.end(),
                     [](const ASSETS::LIGHTING::LightProbeBakeRecord& lhs,
                        const ASSETS::LIGHTING::LightProbeBakeRecord& rhs) { return lhs.priority > rhs.priority; });
}

void ReplaceLightmapRecord(ASSETS::LIGHTING::LightingBakeManifest& manifest,
                           const ASSETS::LIGHTING::LightmapBakeRecord& record) {
    manifest.lightmaps.erase(std::remove_if(manifest.lightmaps.begin(), manifest.lightmaps.end(),
                                            [&record](const ASSETS::LIGHTING::LightmapBakeRecord& existing) {
                                                return existing.id == record.id || existing.objectId == record.objectId;
                                            }),
                             manifest.lightmaps.end());
    manifest.lightmaps.push_back(record);
    std::stable_sort(manifest.lightmaps.begin(), manifest.lightmaps.end(),
                     [](const ASSETS::LIGHTING::LightmapBakeRecord& lhs,
                        const ASSETS::LIGHTING::LightmapBakeRecord& rhs) { return lhs.objectId < rhs.objectId; });
}

uint32_t GetLightProbeCaptureFaceCount(const LightProbeVolumeSettings& settings) {
    return GetLightProbeVolumeProbeCount(settings) * 6u;
}

MATH::Vec3 GetLightProbePositionByIndex(const LightProbeVolumeSettings& settings, uint32_t probeIndex) {
    const uint32_t xyCount = settings.countX * settings.countY;
    const uint32_t z = xyCount > 0u ? probeIndex / xyCount : 0u;
    const uint32_t xy = xyCount > 0u ? probeIndex % xyCount : 0u;
    const uint32_t y = settings.countX > 0u ? xy / settings.countX : 0u;
    const uint32_t x = settings.countX > 0u ? xy % settings.countX : 0u;
    return GetLightProbeVolumeProbePosition(settings, x, y, z);
}

float GetLightProbeCaptureFarPlane(const LightProbeVolumeSettings& settings) {
    const float xyMax = (std::max)(settings.size.x * 1.5f, settings.size.y * 1.5f);
    return (std::max)((std::max)(4.0f, xyMax), settings.size.z * 1.5f);
}

void SetBakeJobState(TOOLS::BAKING::LightingBakeReport& report, TOOLS::BAKING::LightingBakeJobState state) {
    report.jobState = state;
}

Camera3D MakeProbeFaceCamera(const MATH::Vec3& position, uint32_t faceIndex, float farPlane) {
    static constexpr MATH::Vec3 kDirections[6] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
    };
    static constexpr MATH::Vec3 kUps[6] = {
        {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };

    const uint32_t face = (std::min)(faceIndex, 5u);
    Camera3D camera{};
    camera.SetPerspective(std::numbers::pi_v<float> * 0.5f, 1.0f, TOOLS::BAKING::kProbeCaptureNearPlane,
                          (std::max)(1.0f, farPlane));
    camera.SetLookAt(position, position + kDirections[face], kUps[face]);
    return camera;
}

SceneEnvironment MakeProbeCaptureEnvironment(const SceneEnvironment& source) {
    // 探針の捕獲は絶対量の放射輝度だけを記録する。後処理と画面空間の効果は含めない。
    SceneEnvironment captureEnvironment = source;
    captureEnvironment.ambientOcclusion.enabled = false;
    captureEnvironment.screenSpaceReflections.enabled = false;
    captureEnvironment.bloom.enabled = false;
    captureEnvironment.toneMapping.curve = ToneCurve::None;
    // 描画の様式（Cel の受け方など）は画面の表現なので、焼き込む光には使わない。
    captureEnvironment.styleShading = {};
    return captureEnvironment;
}

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
