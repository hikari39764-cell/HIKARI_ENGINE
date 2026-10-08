#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Assets/Color/HIKARI_ColorLutAsset.h"
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/HIKARI_AssetFileCache.h"

namespace HIKARI::ENVIRONMENT {

// エンジン内蔵（engine.environment.* / engine.weather.*）とプロジェクト資産（GUID）の環境プロファイルと天気を同じ ID で引く。
// プロファイルが参照する色調整の LUT（.cube の GUID）もここで読む。
class EnvironmentAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const EnvironmentProfileAsset> LoadProfile(std::string_view id, std::string& error) const;
    std::shared_ptr<const WeatherPresetAsset> LoadWeather(std::string_view id, std::string& error) const;
    std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> LoadColorLut(std::string_view id, std::string& error) const;
    // エディターで編集中の未保存の資産。保存か破棄まで、この内容で描画する。
    void SetProfileDraft(std::string_view id, std::shared_ptr<const EnvironmentProfileAsset> draft);
    void SetWeatherDraft(std::string_view id, std::shared_ptr<const WeatherPresetAsset> draft);
    void ClearDrafts() noexcept;

private:
    const AssetDatabase* database_ = nullptr;
    bool configured_ = false;
    using ProfileCache = AssetFileCache<EnvironmentProfileAsset, AssetType::Environment, &LoadEnvironmentProfileAsset>;
    using WeatherCache = AssetFileCache<WeatherPresetAsset, AssetType::Weather, &LoadWeatherPresetAsset>;
    using ColorLutCache =
        AssetFileCache<ASSETS::COLOR::ColorLutAsset, AssetType::ColorLut, &ASSETS::COLOR::LoadColorLutAsset>;
    ProfileCache profiles_;
    WeatherCache weathers_;
    ColorLutCache colorLuts_;
    BuiltinAssetCache<EnvironmentProfileAsset> builtinProfiles_;
    BuiltinAssetCache<WeatherPresetAsset> builtinWeathers_;
    std::string profileDraftId_;
    std::shared_ptr<const EnvironmentProfileAsset> profileDraft_;
    std::string weatherDraftId_;
    std::shared_ptr<const WeatherPresetAsset> weatherDraft_;
};

} // namespace HIKARI::ENVIRONMENT
