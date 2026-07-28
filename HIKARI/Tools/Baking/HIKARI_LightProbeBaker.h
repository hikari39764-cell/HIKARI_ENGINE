#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Assets/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::TOOLS::BAKING {

    struct LightProbeBakeRequest {
        std::filesystem::path projectRoot{};
        std::string sceneGuid{};
        std::string sceneName{};
        std::string volumeId{ "light_probe_volume_000" };
        std::string volumeName{ "LightProbeVolume_000" };
        LightProbeVolumeSettings settings{};
        int priority = 0;
        bool forceRebake = false;
        std::string authoringHash{};
        std::string sceneHash{};
        std::string dependencyHash{};
        std::string bakeSet{ "Default" };
        std::string scenario{ "Default" };
    };

    struct LightProbeBakeResult {
        bool success = false;
        bool volumeWritten = false;
        bool debugJsonWritten = false;
        ASSETS::LIGHTING::LightProbeBakeRecord record{};
        std::filesystem::path volumePath{};
        std::filesystem::path debugJsonPath{};
        uint32_t probeCount = 0;
        uint32_t captureResolution = 0;
        std::vector<std::string> messages{};
        std::vector<std::string> warnings{};
        std::vector<std::string> errors{};
    };

    class LightProbeBaker {
    public:
        LightProbeBakeResult FinalizeCapturedVolume(
            const LightProbeBakeRequest& request,
            const std::vector<std::filesystem::path>& probeCapturePaths) const;
    };

} // namespace HIKARI::TOOLS::BAKING
