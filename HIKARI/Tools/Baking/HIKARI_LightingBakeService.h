#pragma once

#include <filesystem>
#include <string>

#include "Assets/Lighting/HIKARI_LightingBakeManifest.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct SceneDocument;

    namespace TOOLS::BAKING {

        struct LightingBakeRequest {
            std::filesystem::path projectRoot{};
            std::string sceneGuid{};
            std::string sceneName{};
            std::string scenePath{};
            const SceneDocument* sceneDocument = nullptr;
            LightingBakeTarget target = LightingBakeTarget::All;
            bool force = false;
        };

        class LightingBakeService {
        public:
            LightingBakeReport ValidateLightingBakeSetup(const LightingBakeRequest& request) const;
            LightingBakeReport PrepareLightingBakeManifest(const LightingBakeRequest& request) const;
            LightingBakeReport BakeReflectionProbesOnly(DocumentSceneBase& scene) const;
            LightingBakeReport BakeLightProbesOnly(DocumentSceneBase& scene) const;
            LightingBakeReport ClearLightingBake(const LightingBakeRequest& request) const;

        private:
            ASSETS::LIGHTING::LightingBakeManifest BuildEmptyManifest(const LightingBakeRequest& request) const;
            void AppendSceneAuthoringSummary(const LightingBakeRequest& request, LightingBakeReport& report) const;
        };

    } // namespace TOOLS::BAKING

} // namespace HIKARI
