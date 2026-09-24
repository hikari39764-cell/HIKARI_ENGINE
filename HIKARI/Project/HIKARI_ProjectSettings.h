#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

struct ProjectSettings {
    uint32_t version = 5;
    AssetGuid startupSceneGuid{};
    std::vector<std::string> enabledRuntimeFeatures{};
};

class ProjectSettingsService {
public:
    bool Load(const std::filesystem::path& projectRoot);
    bool Save() const;

    const ProjectSettings& GetSettings() const;
    ProjectSettings& GetSettings();

    bool SetStartupSceneGuid(const AssetGuid& guid);
    void SetEnabledRuntimeFeatures(std::vector<std::string> featureIds);

private:
    std::filesystem::path projectRoot_{};
    std::filesystem::path settingsPath_{};
    ProjectSettings settings_{};
};

} // namespace HIKARI
