#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"

#include <utility>

namespace HIKARI::ENVIRONMENT {

void EnvironmentAssetStore::Configure(const AssetDatabase* database) {
    // 同じデータベースなら読み込み済みの内容と編集中の下書きを保つ。
    if (configured_ && database_ == database) {
        return;
    }
    configured_ = true;
    database_ = database;
    profiles_.Configure(database);
    weathers_.Configure(database);
    colorLuts_.Configure(database);
    ClearDrafts();
}

std::shared_ptr<const EnvironmentProfileAsset> EnvironmentAssetStore::LoadProfile(std::string_view id,
                                                                                  std::string& error) const {
    if (profileDraft_ && id == profileDraftId_) {
        return profileDraft_;
    }
    if (id.starts_with(kBuiltinEnvironmentPrefix)) {
        return builtinProfiles_.Load(id, ResolveBuiltinEnvironmentProfilePath(id), &LoadEnvironmentProfileAsset, error);
    }
    return profiles_.Load(AssetGuid{std::string(id)}, error);
}

std::shared_ptr<const WeatherPresetAsset> EnvironmentAssetStore::LoadWeather(std::string_view id,
                                                                             std::string& error) const {
    if (weatherDraft_ && id == weatherDraftId_) {
        return weatherDraft_;
    }
    if (id.starts_with(kBuiltinWeatherPrefix)) {
        return builtinWeathers_.Load(id, ResolveBuiltinWeatherPresetPath(id), &LoadWeatherPresetAsset, error);
    }
    return weathers_.Load(AssetGuid{std::string(id)}, error);
}

std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> EnvironmentAssetStore::LoadColorLut(std::string_view id,
                                                                                        std::string& error) const {
    return colorLuts_.Load(AssetGuid{std::string(id)}, error);
}

void EnvironmentAssetStore::SetProfileDraft(std::string_view id, std::shared_ptr<const EnvironmentProfileAsset> draft) {
    profileDraftId_ = id;
    profileDraft_ = std::move(draft);
}

void EnvironmentAssetStore::SetWeatherDraft(std::string_view id, std::shared_ptr<const WeatherPresetAsset> draft) {
    weatherDraftId_ = id;
    weatherDraft_ = std::move(draft);
}

void EnvironmentAssetStore::ClearDrafts() noexcept {
    profileDraftId_.clear();
    profileDraft_.reset();
    weatherDraftId_.clear();
    weatherDraft_.reset();
}

} // namespace HIKARI::ENVIRONMENT
