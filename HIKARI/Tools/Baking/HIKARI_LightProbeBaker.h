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
        LightProbeVolumeSettings settings{};
        bool forceRebake = false;
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
