#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "RenderContracts/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"

namespace HIKARI::TOOLS::BAKING {

struct LightProbeBakeRequest {
    std::filesystem::path projectRoot{};
    std::string sceneGuid{};
    std::string sceneName{};
    std::string volumeId{"light_probe_volume_000"};
    std::string volumeName{"LightProbeVolume_000"};
    LightProbeVolumeSettings settings{};
    // 光照贴图と同じ DXR の経路の設定（方向ごとの標本数と反弹の数）。
    uint32_t sampleCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultSampleCount;
    uint32_t bounceCount = ASSETS::LIGHTING::kSurfaceLightmapDefaultBounceCount;
    int priority = 0;
    bool forceRebake = false;
    std::string authoringHash{};
    std::string sceneHash{};
    std::string dependencyHash{};
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

struct LightProbeBakeResult {
    bool success = false;
    bool volumeWritten = false;
    bool debugJsonWritten = false;
    ASSETS::LIGHTING::LightProbeBakeRecord record{};
    std::filesystem::path volumePath{};
    std::filesystem::path debugJsonPath{};
    uint32_t probeCount = 0;
    uint32_t buriedProbeCount = 0; // 形状に埋まって無効にした探針
    std::vector<std::string> messages{};
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
};

class LightProbeBaker {
public:
    // DXR で積んだ探針の方向ごとの放射輝度と距離（探針ごとに kDxrProbeDirectionCount 個）から体積を作って保存する：
    // SH9 への投影、可視性の距離の moment（visibilityDistanceScale で正規化）、形状に埋まった探針の無効化。
    LightProbeBakeResult BuildVolume(const LightProbeBakeRequest& request,
                                     std::span<const DxrTransportAccumulation> cells,
                                     float visibilityDistanceScale) const;
};

} // namespace HIKARI::TOOLS::BAKING
