#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

namespace HIKARI::RENDER3D {

    std::filesystem::path RenderQualityProfilePath(
        const std::filesystem::path& projectRoot) {
        return projectRoot / "ProjectSettings" / "render_quality.json";
    }

    bool LoadRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        RenderQualitySettings& outSettings,
        std::string* errorMessage) {
        const std::filesystem::path path = RenderQualityProfilePath(projectRoot);
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root)) {
            if (errorMessage != nullptr) {
                errorMessage->clear();
            }
            return false;
        }

        if (!root.is_object() ||
            !root.contains("renderQuality")) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not parse render quality profile: " + path.string();
            }
            return false;
        }

        std::string decodeError{};
        if (!DeserializeRenderQualitySettings(
                root["renderQuality"],
                outSettings,
                &decodeError)) {
            if (errorMessage != nullptr) {
                *errorMessage = decodeError + " Path: " + path.string();
            }
            return false;
        }
        return true;
    }

    bool SaveRenderQualityProfile(
        const std::filesystem::path& projectRoot,
        const RenderQualitySettings& settings,
        std::string* errorMessage) {
        const std::filesystem::path path = RenderQualityProfilePath(projectRoot);
        const nlohmann::json root{
            { "version", 1u },
            { "renderQuality", SerializeRenderQualitySettings(settings) },
        };
        return SERIALIZATION::JSON::WriteJsonFile(
            path,
            root,
            errorMessage);
    }

} // namespace HIKARI::RENDER3D
