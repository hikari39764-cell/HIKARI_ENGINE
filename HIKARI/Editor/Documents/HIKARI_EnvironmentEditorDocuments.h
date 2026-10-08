#pragma once
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"
namespace HIKARI::EDITOR {
struct EnvironmentProfileDocumentTraits {
    using Asset = ENVIRONMENT::EnvironmentProfileAsset;
    static constexpr AssetType type = AssetType::Environment;
    static constexpr const char* title = "Environment";
    static constexpr const char* directory = "Environment";
    static constexpr const char* stem = "Environment";
    static constexpr const char* extension = ".henvironment";
    static auto Write(const Asset& a) { return ENVIRONMENT::WriteEnvironmentProfileAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return ENVIRONMENT::ReadEnvironmentProfileAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) {
        return ENVIRONMENT::LoadEnvironmentProfileAsset(p, a, e);
    }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return ENVIRONMENT::SaveEnvironmentProfileAsset(p, a, e);
    }
};
using EnvironmentProfileEditorDocument = ValidatedAssetDocument<EnvironmentProfileDocumentTraits>;

struct WeatherPresetDocumentTraits {
    using Asset = ENVIRONMENT::WeatherPresetAsset;
    static constexpr AssetType type = AssetType::Weather;
    static constexpr const char* title = "Weather";
    static constexpr const char* directory = "Environment";
    static constexpr const char* stem = "Weather";
    static constexpr const char* extension = ".hweather";
    static auto Write(const Asset& a) { return ENVIRONMENT::WriteWeatherPresetAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return ENVIRONMENT::ReadWeatherPresetAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) {
        return ENVIRONMENT::LoadWeatherPresetAsset(p, a, e);
    }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return ENVIRONMENT::SaveWeatherPresetAsset(p, a, e);
    }
};
using WeatherPresetEditorDocument = ValidatedAssetDocument<WeatherPresetDocumentTraits>;
} // namespace HIKARI::EDITOR
