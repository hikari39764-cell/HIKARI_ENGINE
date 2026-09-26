#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {
bool LoadGameplayCameraAsset(const std::filesystem::path& path, CAMERA::GameplayCameraProfile& profile,
                             std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) &&
           CAMERA::ReadGameplayCameraProfile(root, profile, error);
}
bool SaveGameplayCameraAsset(const std::filesystem::path& path, const CAMERA::GameplayCameraProfile& profile,
                             std::string& error) {
    return CAMERA::ValidateGameplayCameraProfile(profile, error) &&
           SERIALIZATION::JSON::WriteJsonFile(path, CAMERA::WriteGameplayCameraProfile(profile), &error);
}
} // namespace HIKARI
