#include "HIKARI_LightProbeBaker.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <utility>

#include <DirectXPackedVector.h>
#include <DirectXTex.h>
#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::TOOLS::BAKING {

    namespace {

        constexpr const char* kVolumeProbeId = "light_probe_volume_000";
        constexpr const char* kVolumeProbeName = "LightProbeVolume_000";

        void AddError(LightProbeBakeResult& result, std::string message) {
            result.errors.push_back(std::move(message));
            result.success = false;
        }

        MATH::Vec3 Add(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        MATH::Vec3 Mul(const MATH::Vec3& value, float scale) {
            return { value.x * scale, value.y * scale, value.z * scale };
        }

        std::filesystem::path LightProbeOutputDirectory(
            const std::filesystem::path& projectRoot,
            const std::string& sceneGuid) {

            return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) /
                "light_probes").lexically_normal();
        }

        float SolidAngleWeight(float u, float v, uint32_t resolution) {
            const float denom = 1.0f + u * u + v * v;
            return 4.0f /
                (static_cast<float>(resolution) * static_cast<float>(resolution) *
                    denom * std::sqrt(denom));
        }

        MATH::Vec3 CubemapDirection(uint32_t face, float u, float v) {
            switch (face) {
            case 0: return MATH::Normalize({ 1.0f, -v, -u });
            case 1: return MATH::Normalize({ -1.0f, -v, u });
            case 2: return MATH::Normalize({ u, 1.0f, v });
            case 3: return MATH::Normalize({ u, -1.0f, -v });
            case 4: return MATH::Normalize({ u, -v, 1.0f });
            case 5: return MATH::Normalize({ -u, -v, -1.0f });
            default: return { 0.0f, 1.0f, 0.0f };
            }
        }

        void EvaluateSh9Basis(const MATH::Vec3& dir, float outBasis[9]) {
            const float x = dir.x;
            const float y = dir.y;
            const float z = dir.z;
            outBasis[0] = 0.282095f;
            outBasis[1] = 0.488603f * y;
            outBasis[2] = 0.488603f * z;
            outBasis[3] = 0.488603f * x;
            outBasis[4] = 1.092548f * x * y;
            outBasis[5] = 1.092548f * y * z;
            outBasis[6] = 0.315392f * (3.0f * z * z - 1.0f);
            outBasis[7] = 1.092548f * x * z;
            outBasis[8] = 0.546274f * (x * x - y * y);
        }

        MATH::Vec3 ReadPixelRgb(const DirectX::Image& image, uint32_t x, uint32_t y) {
            const uint8_t* pixel =
                image.pixels + static_cast<size_t>(image.rowPitch) * y + x * DirectX::BitsPerPixel(image.format) / 8u;

            if (image.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const uint16_t* h = reinterpret_cast<const uint16_t*>(pixel);
                return {
                    DirectX::PackedVector::XMConvertHalfToFloat(h[0]),
                    DirectX::PackedVector::XMConvertHalfToFloat(h[1]),
                    DirectX::PackedVector::XMConvertHalfToFloat(h[2])
                };
            }
            if (image.format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                const float* f = reinterpret_cast<const float*>(pixel);
                return { f[0], f[1], f[2] };
            }
            if (image.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                image.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
                return {
                    static_cast<float>(pixel[0]) / 255.0f,
                    static_cast<float>(pixel[1]) / 255.0f,
                    static_cast<float>(pixel[2]) / 255.0f
                };
            }

            return {};
        }

        bool ProjectCubemapToSh9(
            const std::filesystem::path& capturePath,
            ASSETS::LIGHTING::LightProbeSh9& outSh,
            std::string* outMessage) {

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage image{};
            const HRESULT hr = DirectX::LoadFromDDSFile(
                capturePath.wstring().c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                image);
            if (FAILED(hr)) {
                if (outMessage) {
                    *outMessage = "failed to load light probe capture DDS: " + capturePath.generic_string();
                }
                return false;
            }

            if (!metadata.IsCubemap() ||
                metadata.arraySize < 6 ||
                metadata.width == 0 ||
                metadata.width != metadata.height) {
                if (outMessage) {
                    *outMessage = "light probe capture DDS is not a valid cubemap: " + capturePath.generic_string();
                }
                return false;
            }

            const uint32_t resolution = static_cast<uint32_t>(metadata.width);
            float totalWeight = 0.0f;
            for (uint32_t face = 0; face < 6u; ++face) {
                const DirectX::Image* faceImage = image.GetImage(0, face, 0);
                if (faceImage == nullptr || faceImage->pixels == nullptr) {
                    if (outMessage) {
                        *outMessage = "light probe cubemap face is missing: " + capturePath.generic_string();
                    }
                    return false;
                }

                for (uint32_t y = 0; y < resolution; ++y) {
                    for (uint32_t x = 0; x < resolution; ++x) {
                        const float u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(resolution)) * 2.0f - 1.0f;
                        const float v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(resolution)) * 2.0f - 1.0f;
                        const float weight = SolidAngleWeight(u, v, resolution);
                        const MATH::Vec3 dir = CubemapDirection(face, u, v);
                        const MATH::Vec3 radiance = ReadPixelRgb(*faceImage, x, y);

                        float basis[9]{};
                        EvaluateSh9Basis(dir, basis);
                        for (uint32_t coeff = 0; coeff < ASSETS::LIGHTING::kLightProbeShCoeffCount; ++coeff) {
                            outSh.coeffs[coeff] = Add(
                                outSh.coeffs[coeff],
                                Mul(radiance, basis[coeff] * weight));
                        }
                        totalWeight += weight;
                    }
                }
            }

            if (totalWeight <= 0.0f) {
                if (outMessage) {
                    *outMessage = "light probe capture solid angle is invalid.";
                }
                return false;
            }

            if (outMessage) {
                *outMessage = "Projected cubemap to SH9: " + capturePath.generic_string();
            }
            return true;
        }

        bool SaveDebugJson(
            const std::filesystem::path& path,
            const LightProbeBakeRequest& request,
            const std::vector<std::filesystem::path>& capturePaths,
            std::string* outMessage) {

            LightProbeVolumeSettings settings = request.settings;
            ClampLightProbeVolumeSettings(settings);
            nlohmann::json serializedCapturePaths =
                nlohmann::json::array();
            for (const std::filesystem::path& capturePath :
                    capturePaths) {
                serializedCapturePaths.push_back(
                    PROJECT_PATHS::MakeProjectRelativeString(
                        request.projectRoot,
                        capturePath));
            }

            const nlohmann::json root{
                { "type", "VolumeGrid" },
                { "probeCount",
                    GetLightProbeVolumeProbeCount(settings) },
                { "captureResolution", settings.captureResolution },
                { "shOrder", settings.shOrder },
                { "capturePaths",
                    std::move(serializedCapturePaths) },
            };
            if (!SERIALIZATION::JSON::WriteJsonFile(
                path,
                root,
                outMessage)) {
                return false;
            }
            if (outMessage != nullptr) {
                *outMessage = "light probe debug json saved: " + path.generic_string();
            }
            return true;
        }

    } // namespace

    LightProbeBakeResult LightProbeBaker::FinalizeCapturedVolume(
        const LightProbeBakeRequest& request,
        const std::vector<std::filesystem::path>& probeCapturePaths) const {

        LightProbeBakeResult result{};
        result.success = false;

        if (request.projectRoot.empty()) {
            AddError(result, "Project root is empty.");
            return result;
        }
        if (request.sceneGuid.empty()) {
            AddError(result, "Scene GUID is empty.");
            return result;
        }

        LightProbeVolumeSettings settings = request.settings;
        ClampLightProbeVolumeSettings(settings);
        const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
        if (!settings.enabled) {
            AddError(result, "Light probe volume is disabled.");
            return result;
        }
        if (probeCapturePaths.size() != probeCount) {
            AddError(result, "Light probe capture count does not match volume grid.");
            return result;
        }

        ASSETS::LIGHTING::LightProbeVolumeFileData volume{};
        volume.origin = settings.origin;
        volume.size = settings.size;
        volume.spacing = GetLightProbeVolumeSpacing(settings);
        volume.countX = settings.countX;
        volume.countY = settings.countY;
        volume.countZ = settings.countZ;
        volume.shOrder = ASSETS::LIGHTING::kLightProbeShOrder;
        volume.coeffCount = ASSETS::LIGHTING::kLightProbeShCoeffCount;
        volume.probes.resize(probeCount);

        // Cubemap radiance を SH9 に投影し、diffuse indirect 用に保存する。
        for (uint32_t probe = 0; probe < probeCount; ++probe) {
            std::string message{};
            if (!ProjectCubemapToSh9(probeCapturePaths[probe], volume.probes[probe], &message)) {
                AddError(result, message);
                return result;
            }
            result.messages.push_back(message);
        }

        result.volumePath = ASSETS::LIGHTING::BuildLightProbeVolumeOutputPath(
            request.projectRoot,
            request.sceneGuid);
        std::string saveMessage{};
        if (!ASSETS::LIGHTING::SaveLightProbeVolumeFile(result.volumePath, volume, &saveMessage)) {
            AddError(result, saveMessage);
            return result;
        }
        result.volumeWritten = true;
        result.messages.push_back(saveMessage);

        result.debugJsonPath = LightProbeOutputDirectory(request.projectRoot, request.sceneGuid) /
            "light_probe_volume_debug.json";
        if (SaveDebugJson(result.debugJsonPath, request, probeCapturePaths, &saveMessage)) {
            result.debugJsonWritten = true;
            result.messages.push_back(saveMessage);
        } else if (!saveMessage.empty()) {
            result.warnings.push_back(saveMessage);
        }

        result.record.id = kVolumeProbeId;
        result.record.name = kVolumeProbeName;
        result.record.type = "VolumeGrid";
        result.record.position = settings.origin + settings.size * 0.5f;
        result.record.origin = settings.origin;
        result.record.size = settings.size;
        result.record.countX = settings.countX;
        result.record.countY = settings.countY;
        result.record.countZ = settings.countZ;
        result.record.shOrder = settings.shOrder;
        result.record.probeCount = probeCount;
        result.record.shDataPath = PROJECT_PATHS::MakeProjectRelativeString(request.projectRoot, result.volumePath);

        result.probeCount = probeCount;
        result.captureResolution = settings.captureResolution;
        result.success = true;
        HIKARI_LOG_INFO("[LightProbeBaker] baked volume scene=" +
            request.sceneGuid +
            " probes=" + std::to_string(probeCount) +
            " path=" + result.volumePath.generic_string());
        return result;
    }

} // namespace HIKARI::TOOLS::BAKING
