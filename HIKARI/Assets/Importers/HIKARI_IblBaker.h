#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI {

    struct IblBakeSettings {
        uint32_t irradianceSize = 64;
        uint32_t prefilteredSize = 256;
        uint32_t prefilteredMipCount = 7;

        uint32_t irradianceSampleCount = 256;
        uint32_t prefilteredSampleCount = 1024;
        uint32_t brdfLutSize = 256;
        uint32_t brdfSampleCount = 1024;

        bool forceRebake = false;
    };

    struct IblBakeResult {
        bool success = false;

        std::filesystem::path irradiancePath{};
        std::filesystem::path prefilteredPath{};
        std::filesystem::path brdfLutPath{};

        uint32_t irradianceSize = 0;
        uint32_t prefilteredSize = 0;
        uint32_t prefilteredMipCount = 0;
        uint32_t brdfLutSize = 0;

        std::string message{};
    };

    class IblBaker {
    public:
        static IblBakeResult BakeSkyCubemapToIbl(
            const std::filesystem::path& sourceSkyDds,
            const std::filesystem::path& outputDirectory,
            const std::filesystem::path& sharedGeneratedDirectory,
            const IblBakeSettings& settings);

        static bool BakePrefilteredCubemapOnly(
            const std::filesystem::path& sourceCubemapDds,
            const std::filesystem::path& outputPrefilteredDds,
            uint32_t prefilteredSize,
            uint32_t prefilteredMipCount,
            uint32_t sampleCount,
            std::string* outMessage = nullptr);

        static bool EnsureSharedBrdfLut(
            const std::filesystem::path& outputBrdfLutDds,
            uint32_t brdfLutSize,
            uint32_t sampleCount,
            bool forceRebake,
            std::string* outMessage = nullptr);
    };

} // namespace HIKARI
