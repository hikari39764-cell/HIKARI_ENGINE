#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace TOOLS::BAKING {

        struct ReflectionProbeBakeRequest {
            std::filesystem::path projectRoot{};
            std::string sceneGuid{};
            std::string sceneName{};
            MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
            float radius = 8.0f;
            float intensity = 1.0f;
            std::string influenceShape{ "Sphere" };
            MATH::Vec3 influenceBoxCenter{ 0.0f, 2.0f, 0.0f };
            MATH::Vec3 influenceBoxSize{ 8.0f, 4.0f, 8.0f };
            std::string projectionShape{ "Infinite" };
            MATH::Vec3 projectionBoxCenter{ 0.0f, 2.0f, 0.0f };
            MATH::Vec3 projectionBoxSize{ 8.0f, 4.0f, 8.0f };
            float blendDistance = 1.0f;
            int priority = 0;
            uint32_t resolution = 128;
            uint32_t prefilteredMipCount = 7;
            uint32_t prefilteredSampleCount = 128;
            uint32_t brdfLutSize = 256;
            uint32_t brdfSampleCount = 256;
            bool forceRebake = false;
            std::filesystem::path sourceCubemapPath{};
        };

        struct ReflectionProbeBakeResult {
            bool success = false;
            bool captured = false;
            bool prefiltered = false;
            bool brdfReady = false;
            bool captureValidated = false;
            bool prefilterValidated = false;
            uint32_t capturedFaceCount = 0;
            uint32_t captureResolution = 0;
            uint32_t captureMipCount = 0;
            std::string captureFormat{};
            uint32_t prefilteredMipCount = 0;
            std::string prefilteredFormat{};
            ASSETS::LIGHTING::ReflectionProbeBakeRecord record{};
            std::filesystem::path capturePath{};
            std::filesystem::path prefilteredPath{};
            std::filesystem::path brdfLutPath{};
            std::vector<std::string> faceSummaries{};
            std::vector<std::string> messages{};
            std::vector<std::string> warnings{};
            std::vector<std::string> errors{};
        };

        class ReflectionProbeBaker {
        public:
            ReflectionProbeBakeResult BakeSingleProbe(
                DocumentSceneBase& scene,
                const ReflectionProbeBakeRequest& request) const;

            ReflectionProbeBakeResult FinalizeCapturedProbe(
                const ReflectionProbeBakeRequest& request,
                const std::filesystem::path& capturePath) const;
        };

    } // namespace TOOLS::BAKING

} // namespace HIKARI
