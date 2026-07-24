#include "HIKARI_ProjectSettings.h"

#include <filesystem>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {

    bool ProjectSettingsService::Load(const std::filesystem::path& projectRoot) {
        std::error_code ec{};
        projectRoot_ = projectRoot.empty()
            ? std::filesystem::current_path(ec)
            : projectRoot;
        projectRoot_ = std::filesystem::absolute(projectRoot_, ec).lexically_normal();
        if (ec) {
            projectRoot_ = projectRoot.lexically_normal();
        }

        settingsPath_ = projectRoot_ / "ProjectSettings" / "project_settings.json";
        if (!std::filesystem::exists(settingsPath_, ec)) {
            settings_ = ProjectSettings{};
            return Save();
        }

        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                settingsPath_,
                root) ||
            !root.is_object()) {
            HIKARI_LOG_WARN("[ProjectSettings] project_settings.json parse failed; defaults are used.");
            settings_ = ProjectSettings{};
            return false;
        }

        settings_ = ProjectSettings{};
        const uint32_t sourceVersion = root.value("version", 1u);
        settings_.version = (std::max)(sourceVersion, 3u);
        settings_.startupSceneGuid.value = root.value("startupSceneGuid", std::string{});
        if (root.contains("enabledRuntimeFeatures") &&
            root["enabledRuntimeFeatures"].is_array()) {
            settings_.enabledRuntimeFeatures.clear();
            std::unordered_set<std::string> seen;
            for (const nlohmann::json& node :
                    root["enabledRuntimeFeatures"]) {
                if (!node.is_string()) {
                    continue;
                }
                std::string featureId = node.get<std::string>();
                if (!featureId.empty() && seen.insert(featureId).second) {
                    settings_.enabledRuntimeFeatures.push_back(
                        std::move(featureId));
                }
            }
        }
        if (sourceVersion < 3u) {
            const auto rendering = std::find(
                settings_.enabledRuntimeFeatures.begin(),
                settings_.enabledRuntimeFeatures.end(),
                RuntimeFeatureIds::Rendering);
            const bool hasAnimation = std::find(
                settings_.enabledRuntimeFeatures.begin(),
                settings_.enabledRuntimeFeatures.end(),
                RuntimeFeatureIds::Animation) !=
                settings_.enabledRuntimeFeatures.end();
            if (rendering != settings_.enabledRuntimeFeatures.end() &&
                !hasAnimation) {
                settings_.enabledRuntimeFeatures.insert(
                    std::next(rendering),
                    std::string(RuntimeFeatureIds::Animation));
            }
        }
        return true;
    }

    bool ProjectSettingsService::Save() const {
        if (settingsPath_.empty()) {
            return false;
        }

        // ProjectSettings は参照 GUID だけを保持し、Scene 本体は Asset 側に置く。
        const nlohmann::json root{
            { "version", settings_.version },
            { "startupSceneGuid", settings_.startupSceneGuid.value },
            { "enabledRuntimeFeatures", settings_.enabledRuntimeFeatures },
        };

        std::string writeMessage{};
        if (!SERIALIZATION::JSON::WriteJsonFile(
                settingsPath_,
                root,
                &writeMessage)) {
            HIKARI_LOG_WARN(
                "[ProjectSettings] failed to write: " +
                writeMessage);
            return false;
        }

        return true;
    }

    const ProjectSettings& ProjectSettingsService::GetSettings() const {
        return settings_;
    }

    ProjectSettings& ProjectSettingsService::GetSettings() {
        return settings_;
    }

    bool ProjectSettingsService::SetStartupSceneGuid(const AssetGuid& guid) {
        if (guid.IsValid() && !IsValidAssetGuid(guid.value)) {
            return false;
        }

        settings_.startupSceneGuid = guid;
        return true;
    }

    void ProjectSettingsService::SetEnabledRuntimeFeatures(
        std::vector<std::string> featureIds) {

        settings_.enabledRuntimeFeatures.clear();
        std::unordered_set<std::string> seen;
        settings_.enabledRuntimeFeatures.reserve(featureIds.size());
        for (std::string& featureId : featureIds) {
            if (!featureId.empty() && seen.insert(featureId).second) {
                settings_.enabledRuntimeFeatures.push_back(
                    std::move(featureId));
            }
        }
        settings_.version = (std::max)(settings_.version, 3u);
    }

} // namespace HIKARI
