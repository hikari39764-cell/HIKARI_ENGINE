#include "Assets/Environment/HIKARI_EnvironmentAsset.h"

#include <utility>

#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Assets/Environment/HIKARI_EnvironmentFields.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"

namespace HIKARI::ENVIRONMENT {
namespace {

using FIELDS::VisitProfileSections;

constexpr BuiltinEnvironmentContent kBuiltinProfiles[]{
    {"engine.environment.default", "Default", "Default"},
    {"engine.environment.studio", "Studio", "Studio"},
};

constexpr BuiltinEnvironmentContent kBuiltinWeathers[]{
    {"engine.weather.clear", "Clear", "Clear"},
    {"engine.weather.cloudy", "Cloudy", "Cloudy"},
    {"engine.weather.overcast", "Overcast", "Overcast"},
    {"engine.weather.cloudsea", "Cloud Sea", "CloudSea"},
    {"engine.weather.rain", "Rain", "Rain"},
    {"engine.weather.storm", "Storm", "Storm"},
    {"engine.weather.snow", "Snow", "Snow"},
    {"engine.weather.fog", "Fog", "Fog"},
};

bool IsProjectAssetReference(const std::string& id, std::string_view builtinPrefix) {
    return !id.empty() && (builtinPrefix.empty() || !id.starts_with(builtinPrefix)) && IsValidAssetGuid(id);
}

void AddDependency(std::vector<AssetDependencyDesc>& dependencies, const std::string& id, const char* role) {
    for (const AssetDependencyDesc& dependency : dependencies) {
        if (dependency.guid.value == id) {
            return;
        }
    }
    dependencies.push_back({AssetGuid{id}, {}, role});
}

std::filesystem::path ResolveBuiltinPath(std::span<const BuiltinEnvironmentContent> content, std::string_view id,
                                         const char* directory, const char* extension) {
    for (const BuiltinEnvironmentContent& entry : content) {
        if (entry.id == id) {
            return CONTENT::GetEngineContentContext().Resolve(std::filesystem::path("Environment") / directory /
                                                              (std::string(entry.fileStem) + extension));
        }
    }
    return {};
}

} // namespace

EnvironmentProfile NormalizeEnvironmentProfile(EnvironmentProfile profile) {
    const EnvironmentProfile defaults{};
    VisitProfileSections([&](const char*, auto member, const auto& fields) {
        profile.*member = JSONFIELDS::Sanitize(profile.*member, fields, defaults.*member);
    });
    if (!TEXT::IsNamedEnum(kSkySourceNames, profile.sky.source)) {
        profile.sky.source = defaults.sky.source;
    }
    if (!TEXT::IsNamedEnum(kBelowHorizonModeNames, profile.atmosphere.belowHorizon)) {
        profile.atmosphere.belowHorizon = defaults.atmosphere.belowHorizon;
    }
    if (!TEXT::IsNamedEnum(kSunModeNames, profile.sun.mode)) {
        profile.sun.mode = defaults.sun.mode;
    }
    if (!TEXT::IsNamedEnum(RENDER3D::COLOR::kExposureModeNames, profile.exposure.mode)) {
        profile.exposure.mode = defaults.exposure.mode;
    }
    if (profile.exposure.maxEv100 < profile.exposure.minEv100) {
        profile.exposure.maxEv100 = profile.exposure.minEv100;
    }
    if (!TEXT::IsNamedEnum(kSsaoModeNames, profile.ambientOcclusion.mode)) {
        profile.ambientOcclusion.mode = defaults.ambientOcclusion.mode;
    }
    if (!TEXT::IsNamedEnum(kScreenSpaceReflectionQualityNames, profile.screenSpaceReflections.quality)) {
        profile.screenSpaceReflections.quality = defaults.screenSpaceReflections.quality;
    }
    if (profile.defaultWeather.empty()) {
        profile.defaultWeather = std::string(kDefaultWeatherId);
    }
    return profile;
}

bool ReadEnvironmentProfileAsset(const nlohmann::json& root, EnvironmentProfileAsset& out, std::string& error) {
    if (!JSONSTRICT::RequireFormatVersion(root, kEnvironmentProfileFormatVersion, error)) {
        return false;
    }
    EnvironmentProfileAsset asset;
    if (!JSONSTRICT::ReadString(root, "name", asset.name, asset.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "defaultWeather", asset.profile.defaultWeather, asset.profile.defaultWeather,
                                error, "root")) {
        return false;
    }
    bool valid = true;
    VisitProfileSections([&](const char* key, auto member, const auto& fields) {
        const auto section = root.find(key);
        if (section == root.end()) {
            return;
        }
        if (!section->is_object()) {
            valid = valid && JSONSTRICT::Fail(error, std::string("root.") + key + " must be an object");
            return;
        }
        JSONFIELDS::ReadLenient(*section, asset.profile.*member, fields);
    });
    if (!valid) {
        return false;
    }
    asset.profile = NormalizeEnvironmentProfile(std::move(asset.profile));
    out = std::move(asset);
    return true;
}

nlohmann::json WriteEnvironmentProfileSections(const EnvironmentProfile& source) {
    const EnvironmentProfile profile = NormalizeEnvironmentProfile(source);
    nlohmann::json root = nlohmann::json::object();
    VisitProfileSections([&](const char* key, auto member, const auto& fields) {
        nlohmann::json section = nlohmann::json::object();
        JSONFIELDS::Write(section, profile.*member, fields);
        root[key] = std::move(section);
    });
    root["defaultWeather"] = profile.defaultWeather;
    return root;
}

nlohmann::json WriteEnvironmentProfileAsset(const EnvironmentProfileAsset& asset) {
    nlohmann::json root = WriteEnvironmentProfileSections(asset.profile);
    root["formatVersion"] = kEnvironmentProfileFormatVersion;
    root["name"] = asset.name;
    return root;
}

bool LoadEnvironmentProfileAsset(const std::filesystem::path& path, EnvironmentProfileAsset& out, std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) && ReadEnvironmentProfileAsset(root, out, error);
}

bool SaveEnvironmentProfileAsset(const std::filesystem::path& path, const EnvironmentProfileAsset& asset,
                                 std::string& error) {
    return SERIALIZATION::JSON::WriteJsonFile(path, WriteEnvironmentProfileAsset(asset), &error);
}

bool ReadWeatherPresetAsset(const nlohmann::json& root, WeatherPresetAsset& out, std::string& error) {
    if (!JSONSTRICT::RequireFormatVersion(root, kWeatherPresetFormatVersion, error)) {
        return false;
    }
    WeatherPresetAsset asset;
    if (!JSONSTRICT::ReadString(root, "name", asset.name, asset.name, error, "root")) {
        return false;
    }
    const auto weather = root.find("weather");
    if (weather != root.end() && !weather->is_object()) {
        return JSONSTRICT::Fail(error, "root.weather must be an object");
    }
    ReadWeatherParameters(weather != root.end() ? *weather : nlohmann::json::object(), asset.weather);
    out = std::move(asset);
    return true;
}

nlohmann::json WriteWeatherPresetAsset(const WeatherPresetAsset& asset) {
    return {
        {"formatVersion", kWeatherPresetFormatVersion},
        {"name", asset.name},
        {"weather", WriteWeatherParameters(asset.weather)},
    };
}

bool LoadWeatherPresetAsset(const std::filesystem::path& path, WeatherPresetAsset& out, std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) && ReadWeatherPresetAsset(root, out, error);
}

bool SaveWeatherPresetAsset(const std::filesystem::path& path, const WeatherPresetAsset& asset, std::string& error) {
    return SERIALIZATION::JSON::WriteJsonFile(path, WriteWeatherPresetAsset(asset), &error);
}

std::vector<AssetDependencyDesc> CollectEnvironmentProfileDependencies(const EnvironmentProfileAsset& asset) {
    std::vector<AssetDependencyDesc> dependencies;
    const EnvironmentProfile& profile = asset.profile;
    if (profile.sky.source != SkySource::Atmosphere && IsProjectAssetReference(profile.sky.asset, {})) {
        AddDependency(dependencies, profile.sky.asset, "Environment.Sky");
    }
    if (profile.sky.source == SkySource::Panorama && IsProjectAssetReference(profile.sky.panoramaCloudAsset, {})) {
        AddDependency(dependencies, profile.sky.panoramaCloudAsset, "Environment.PanoramaCloud");
    }
    if (IsProjectAssetReference(profile.colorGrading.lut, {})) {
        AddDependency(dependencies, profile.colorGrading.lut, "Environment.ColorLut");
    }
    if (IsProjectAssetReference(profile.defaultWeather, kBuiltinWeatherPrefix)) {
        AddDependency(dependencies, profile.defaultWeather, "Environment.Weather");
    }
    return dependencies;
}

std::span<const BuiltinEnvironmentContent> GetBuiltinEnvironmentProfiles() noexcept { return kBuiltinProfiles; }
std::span<const BuiltinEnvironmentContent> GetBuiltinWeatherPresets() noexcept { return kBuiltinWeathers; }

std::filesystem::path ResolveBuiltinEnvironmentProfilePath(std::string_view id) {
    return ResolveBuiltinPath(kBuiltinProfiles, id, "Profiles", ".henvironment");
}

std::filesystem::path ResolveBuiltinWeatherPresetPath(std::string_view id) {
    return ResolveBuiltinPath(kBuiltinWeathers, id, "Weathers", ".hweather");
}

} // namespace HIKARI::ENVIRONMENT
