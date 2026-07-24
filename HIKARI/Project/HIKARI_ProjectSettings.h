#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"

namespace HIKARI {

    struct ProjectSettings {
        uint32_t version = 3;
        AssetGuid startupSceneGuid{};
        std::vector<std::string> enabledRuntimeFeatures{
            std::string(RuntimeFeatureIds::Rendering),
            std::string(RuntimeFeatureIds::Animation),
            std::string(RuntimeFeatureIds::Camera),
            std::string(RuntimeFeatureIds::Physics),
            std::string(RuntimeFeatureIds::GameplayBasic),
            std::string(RuntimeFeatureIds::Cinematics)
        };
    };

    class ProjectSettingsService {
    public:
        bool Load(const std::filesystem::path& projectRoot);
        bool Save() const;

        const ProjectSettings& GetSettings() const;
        ProjectSettings& GetSettings();

        bool SetStartupSceneGuid(const AssetGuid& guid);
        void SetEnabledRuntimeFeatures(
            std::vector<std::string> featureIds);

    private:
        std::filesystem::path projectRoot_{};
        std::filesystem::path settingsPath_{};
        ProjectSettings settings_{};
    };

} // namespace HIKARI
