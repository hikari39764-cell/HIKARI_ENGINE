#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/Environment/HIKARI_WeatherModel.h"

// 環境の資産。環境プロファイル（.henvironment）は空・大気・天体・霧・露出などの設定、
// 天気（.hweather）は雲量や降水などの天気の値を持つ。シーンはプロファイルを参照し、
// 初期の時刻・地点・天気だけを持つ。
namespace HIKARI::ENVIRONMENT {

inline constexpr uint32_t kEnvironmentProfileFormatVersion = 1u;
inline constexpr uint32_t kWeatherPresetFormatVersion = 1u;

struct EnvironmentProfileAsset {
    std::string name = "Environment";
    EnvironmentProfile profile{};
};

struct WeatherPresetAsset {
    std::string name = "Weather";
    WeatherParameters weather{};
};

// 範囲外や非有限の値を既定の範囲へ戻す。読み込みと保存の両方で同じ規則を使う。
EnvironmentProfile NormalizeEnvironmentProfile(EnvironmentProfile profile);

// プロファイルの各節（sky, atmosphere, sun ...）の JSON。資産の書き出しと診断が同じ形を使う。
nlohmann::json WriteEnvironmentProfileSections(const EnvironmentProfile& profile);

bool ReadEnvironmentProfileAsset(const nlohmann::json& root, EnvironmentProfileAsset& out, std::string& error);
nlohmann::json WriteEnvironmentProfileAsset(const EnvironmentProfileAsset& asset);
bool LoadEnvironmentProfileAsset(const std::filesystem::path& path, EnvironmentProfileAsset& out, std::string& error);
bool SaveEnvironmentProfileAsset(const std::filesystem::path& path, const EnvironmentProfileAsset& asset,
                                 std::string& error);

bool ReadWeatherPresetAsset(const nlohmann::json& root, WeatherPresetAsset& out, std::string& error);
nlohmann::json WriteWeatherPresetAsset(const WeatherPresetAsset& asset);
bool LoadWeatherPresetAsset(const std::filesystem::path& path, WeatherPresetAsset& out, std::string& error);
bool SaveWeatherPresetAsset(const std::filesystem::path& path, const WeatherPresetAsset& asset, std::string& error);

// プロファイルが参照するプロジェクト資産（空のテクスチャ、後処理プロファイル、既定の天気）。内蔵 ID は含めない。
std::vector<AssetDependencyDesc> CollectEnvironmentProfileDependencies(const EnvironmentProfileAsset& asset);

// エンジンが初期内容として持つプロファイルと天気。ID は "engine.environment.*" / "engine.weather.*"。
struct BuiltinEnvironmentContent {
    std::string_view id;
    std::string_view displayName;
    std::string_view fileStem;
};
inline constexpr std::string_view kBuiltinEnvironmentPrefix = "engine.environment.";
inline constexpr std::string_view kBuiltinWeatherPrefix = "engine.weather.";
inline constexpr std::string_view kDefaultEnvironmentProfileId = "engine.environment.default";
inline constexpr std::string_view kDefaultWeatherId = "engine.weather.clear";
std::span<const BuiltinEnvironmentContent> GetBuiltinEnvironmentProfiles() noexcept;
std::span<const BuiltinEnvironmentContent> GetBuiltinWeatherPresets() noexcept;
std::filesystem::path ResolveBuiltinEnvironmentProfilePath(std::string_view id);
std::filesystem::path ResolveBuiltinWeatherPresetPath(std::string_view id);

} // namespace HIKARI::ENVIRONMENT
