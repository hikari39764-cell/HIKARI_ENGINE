#include "HIKARI_ReflectionProbeCaptureValidator.h"

#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_DxgiFormatName.h"

namespace HIKARI::TOOLS::BAKING {

    namespace {

        constexpr const char* kFaceNames[6] = {
            "+X", "-X", "+Y", "-Y", "+Z", "-Z"
        };

        bool IsSupportedReflectionProbeFormat(DXGI_FORMAT format) {
            switch (format) {
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
                return true;
            default:
                return false;
            }
        }

        void AddError(ReflectionProbeCaptureValidationResult& result, std::string message) {
            result.errors.push_back(std::move(message));
            result.success = false;
        }

        void AddWarning(ReflectionProbeCaptureValidationResult& result, std::string message) {
            result.warnings.push_back(std::move(message));
        }

        void AddMessage(ReflectionProbeCaptureValidationResult& result, std::string message) {
            result.messages.push_back(std::move(message));
        }

        std::string FormatFaceSummary(
            const char* faceName,
            double minLum,
            double maxLum,
            double avgLum,
            uint64_t nanInfCount) {

            std::ostringstream oss{};
            oss << "Face " << faceName
                << " avgLum=" << std::fixed << std::setprecision(4) << avgLum
                << " minLum=" << std::fixed << std::setprecision(4) << minLum
                << " maxLum=" << std::fixed << std::setprecision(4) << maxLum
                << " nanInf=" << nanInfCount;
            return oss.str();
        }

        bool AnalyzeFacesAsFloat4(
            const DirectX::ScratchImage& image,
            uint32_t faceCount,
            ReflectionProbeCaptureValidationResult& result) {

            bool anyNonBlack = false;
            for (uint32_t face = 0; face < faceCount; ++face) {
                const DirectX::Image* faceImage = image.GetImage(0, face, 0);
                if (faceImage == nullptr || faceImage->pixels == nullptr) {
                    AddError(result, "Capture face image is missing: " + std::to_string(face));
                    continue;
                }

                double minLum = (std::numeric_limits<double>::max)();
                double maxLum = 0.0;
                double sumLum = 0.0;
                uint64_t sampleCount = 0;
                uint64_t nanInfCount = 0;

                for (size_t y = 0; y < faceImage->height; ++y) {
                    const uint8_t* row = faceImage->pixels + y * faceImage->rowPitch;
                    const float* values = reinterpret_cast<const float*>(row);
                    for (size_t x = 0; x < faceImage->width; ++x) {
                        const float r = values[x * 4 + 0];
                        const float g = values[x * 4 + 1];
                        const float b = values[x * 4 + 2];
                        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
                            ++nanInfCount;
                            continue;
                        }

                        const double lum =
                            static_cast<double>(r) * 0.2126 +
                            static_cast<double>(g) * 0.7152 +
                            static_cast<double>(b) * 0.0722;
                        minLum = (std::min)(minLum, lum);
                        maxLum = (std::max)(maxLum, lum);
                        sumLum += lum;
                        ++sampleCount;
                    }
                }

                if (sampleCount == 0) {
                    minLum = 0.0;
                }

                const double avgLum = sampleCount > 0
                    ? sumLum / static_cast<double>(sampleCount)
                    : 0.0;
                if (maxLum > 0.0001) {
                    anyNonBlack = true;
                }
                if (nanInfCount > 0) {
                    AddError(result, "NaN or Inf was found in capture face " + std::to_string(face) + ".");
                }
                if (maxLum <= 0.0001) {
                    AddWarning(result, "Capture face appears black: " + std::to_string(face));
                }

                const char* faceName = face < 6u ? kFaceNames[face] : "?";
                result.faceSummaries.push_back(
                    FormatFaceSummary(faceName, minLum, maxLum, avgLum, nanInfCount));
            }

            return anyNonBlack;
        }

        ReflectionProbeCaptureValidationResult ValidateReflectionProbeDds(
            const std::filesystem::path& ddsPath,
            bool requirePrefilterMips) {

            ReflectionProbeCaptureValidationResult result{};
            if (ddsPath.empty()) {
                AddError(result, "Reflection probe DDS path is empty.");
                return result;
            }

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage source{};
            HRESULT hr = DirectX::LoadFromDDSFile(
                ddsPath.wstring().c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                source);
            if (FAILED(hr)) {
                AddError(result, "Failed to load reflection probe DDS: " + ddsPath.generic_string());
                return result;
            }

            result.width = static_cast<uint32_t>(metadata.width);
            result.height = static_cast<uint32_t>(metadata.height);
            result.arraySize = static_cast<uint32_t>(metadata.arraySize);
            result.mipCount = static_cast<uint32_t>(metadata.mipLevels);
            result.format = metadata.format;
            result.isCubemap = metadata.IsCubemap();
            result.hasSixFaces = metadata.arraySize >= 6;
            result.formatSupported = IsSupportedReflectionProbeFormat(metadata.format);
            result.sizeValid = metadata.width == metadata.height && metadata.width >= 32;

            if (!result.isCubemap) {
                AddError(result, "Reflection probe DDS is not a cubemap.");
            }
            if (!result.hasSixFaces) {
                AddError(result, "Reflection probe DDS does not contain six faces.");
            }
            if (!result.sizeValid) {
                AddError(result, "Reflection probe DDS size is invalid.");
            }
            if (!result.formatSupported) {
                AddError(result, "Reflection probe DDS format is not supported: " +
                    std::string(GFX::DxgiFormatName(metadata.format)));
            }
            if (requirePrefilterMips && metadata.mipLevels < 2) {
                AddError(result, "Prefiltered reflection probe DDS has too few mip levels.");
            }

            if (!result.isCubemap || !result.hasSixFaces) {
                return result;
            }

            DirectX::ScratchImage floatImage{};
            hr = DirectX::Convert(
                source.GetImages(),
                source.GetImageCount(),
                metadata,
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                DirectX::TEX_FILTER_DEFAULT,
                0.0f,
                floatImage);
            if (FAILED(hr)) {
                AddError(result, "Failed to convert reflection probe DDS for validation.");
                return result;
            }

            // mip0 の各 face だけを破損検出用に確認する。
            result.hasNonBlackContent = AnalyzeFacesAsFloat4(
                floatImage,
                std::min<uint32_t>(result.arraySize, 6u),
                result);
            if (!result.hasNonBlackContent) {
                AddError(result, "Reflection probe DDS appears fully black.");
            }

            result.success = result.errors.empty();
            AddMessage(result, "Reflection probe DDS validated: " + ddsPath.generic_string());
            HIKARI_LOG_INFO("[ReflectionProbeCaptureValidator] path=" +
                ddsPath.generic_string() +
                " cubemap=" + std::string(result.isCubemap ? "true" : "false") +
                " faces=" + std::to_string(result.arraySize) +
                " mips=" + std::to_string(result.mipCount) +
                " format=" + GFX::DxgiFormatName(result.format) +
                " valid=" + std::string(result.success ? "true" : "false"));
            return result;
        }

    } // namespace

    ReflectionProbeCaptureValidationResult ValidateReflectionProbeCaptureDds(
        const std::filesystem::path& captureDds) {

        return ValidateReflectionProbeDds(captureDds, false);
    }

    ReflectionProbeCaptureValidationResult ValidateReflectionProbePrefilteredDds(
        const std::filesystem::path& prefilteredDds) {

        return ValidateReflectionProbeDds(prefilteredDds, true);
    }

} // namespace HIKARI::TOOLS::BAKING
