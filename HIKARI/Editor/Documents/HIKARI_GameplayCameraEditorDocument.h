#pragma once
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"

namespace HIKARI::EDITOR {
struct GameplayCameraDocumentTraits {
    using Asset = CAMERA::GameplayCameraProfile;
    static constexpr AssetType type = AssetType::GameplayCamera;
    static constexpr const char* directory = "Cameras";
    static constexpr const char* stem = "GameplayCamera";
    static constexpr const char* title = "Gameplay Camera";
    static constexpr const char* extension = ".hcamera";
    static auto Write(const Asset& asset) { return CAMERA::WriteGameplayCameraProfile(asset); }
    static bool Normalize(Asset& asset, std::string& error) {
        return CAMERA::ValidateGameplayCameraProfile(asset, error);
    }
    static bool Load(const std::filesystem::path& path, Asset& asset, std::string& error) {
        return LoadGameplayCameraAsset(path, asset, error);
    }
    static bool Save(const std::filesystem::path& path, const Asset& asset, std::string& error) {
        return SaveGameplayCameraAsset(path, asset, error);
    }
};

class GameplayCameraEditorDocument : public ValidatedAssetDocument<GameplayCameraDocumentTraits> {
public:
    bool New(std::string_view preset, std::string& message, bool discard = false) {
        return ValidatedAssetDocument::New(CAMERA::MakeGameplayCameraPreset(preset), message, discard);
    }
};
} // namespace HIKARI::EDITOR
