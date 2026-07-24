#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <dxgiformat.h>

namespace HIKARI::TOOLS::BAKING {

    struct ReflectionProbeCaptureValidationResult {
        bool success = false;

        bool isCubemap = false;
        bool hasSixFaces = false;
        bool formatSupported = false;
        bool sizeValid = false;
        bool hasNonBlackContent = false;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t arraySize = 0;
        uint32_t mipCount = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

        std::vector<std::string> faceSummaries{};
        std::vector<std::string> messages{};
        std::vector<std::string> warnings{};
        std::vector<std::string> errors{};
    };

    ReflectionProbeCaptureValidationResult ValidateReflectionProbeCaptureDds(
        const std::filesystem::path& captureDds);

    ReflectionProbeCaptureValidationResult ValidateReflectionProbePrefilteredDds(
        const std::filesystem::path& prefilteredDds);

} // namespace HIKARI::TOOLS::BAKING
