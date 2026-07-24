#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::ASSETS::LIGHTING {

    constexpr uint32_t kLightProbeVolumeVersion = 1;
    constexpr uint32_t kLightProbeShOrder = 3;
    constexpr uint32_t kLightProbeShCoeffCount = 9;

    struct LightProbeSh9 {
        MATH::Vec3 coeffs[kLightProbeShCoeffCount]{};
    };

    struct LightProbeVolumeFileData {
        MATH::Vec3 origin{};
        MATH::Vec3 size{};
        MATH::Vec3 spacing{};
        uint32_t countX = 0;
        uint32_t countY = 0;
        uint32_t countZ = 0;
        uint32_t shOrder = kLightProbeShOrder;
        uint32_t coeffCount = kLightProbeShCoeffCount;
        std::vector<LightProbeSh9> probes{};
    };

    bool LoadLightProbeVolumeFile(
        const std::filesystem::path& path,
        LightProbeVolumeFileData& outData,
        std::string* outMessage = nullptr);

    bool SaveLightProbeVolumeFile(
        const std::filesystem::path& path,
        const LightProbeVolumeFileData& data,
        std::string* outMessage = nullptr);

    std::filesystem::path BuildLightProbeVolumeOutputPath(
        const std::filesystem::path& projectRoot,
        const std::string& sceneGuid);

} // namespace HIKARI::ASSETS::LIGHTING
