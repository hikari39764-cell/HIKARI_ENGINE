#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI {

    class AssetRegistry;
    class SkyManager;

    namespace RENDER3D::LIGHTING {

        struct LightingRuntimeLoadRequest {
            std::string sceneGuid{};
            std::filesystem::path projectRoot{};
            std::unordered_set<std::string> skyAssetIds{};
            std::unordered_set<std::string> reflectionProbeCubemapAssetIds{};
            bool reflectionProbeEnabled = false;
            MATH::Vec3 reflectionProbePosition{ 0.0f, 2.0f, 0.0f };
            float reflectionProbeRadius = 8.0f;
            float reflectionProbeIntensity = 1.0f;
            REFLECTION::RuntimeReflectionProbeInfluenceShape reflectionProbeInfluenceShape =
                REFLECTION::RuntimeReflectionProbeInfluenceShape::Sphere;
            REFLECTION::RuntimeReflectionProbeProjectionShape reflectionProbeProjectionShape =
                REFLECTION::RuntimeReflectionProbeProjectionShape::Infinite;
            MATH::Vec3 reflectionProbeInfluenceBoxCenter{ 0.0f, 2.0f, 0.0f };
            MATH::Vec3 reflectionProbeInfluenceBoxSize{ 8.0f, 4.0f, 8.0f };
            MATH::Vec3 reflectionProbeProjectionBoxCenter{ 0.0f, 2.0f, 0.0f };
            MATH::Vec3 reflectionProbeProjectionBoxSize{ 8.0f, 4.0f, 8.0f };
            float reflectionProbeBlendDistance = 1.0f;
            int reflectionProbePriority = 0;
            bool lightProbeVolumeEnabled = false;
            float lightProbeVolumeIntensity = 1.0f;
            bool allowAuthoringReflectionProbeFallback = true;
            bool preferBakeManifest = true;
        };

        struct LightingRuntimeLoadResult {
            bool success = true;
            SceneLightingRuntimeData runtimeData{};
            std::vector<std::string> messages{};
        };

        class LightingRuntimeLoader {
        public:
            LightingRuntimeLoadResult Load(
                const LightingRuntimeLoadRequest& request,
                const AssetRegistry& assetRegistry,
                SkyManager& skyManager) const;

        private:
            bool TryLoadBakeManifest(
                const LightingRuntimeLoadRequest& request,
                SceneLightingRuntimeData& runtimeData,
                std::vector<std::string>& messages) const;

            void LoadSkyLightingFromAssets(
                const LightingRuntimeLoadRequest& request,
                const AssetRegistry& assetRegistry,
                SkyManager& skyManager,
                SceneLightingRuntimeData& runtimeData,
                std::vector<std::string>& messages) const;

            void LoadReflectionProbeFromAuthoringSource(
                const LightingRuntimeLoadRequest& request,
                const AssetRegistry& assetRegistry,
                SceneLightingRuntimeData& runtimeData,
                std::vector<std::string>& messages) const;
        };

    } // namespace RENDER3D::LIGHTING

} // namespace HIKARI
