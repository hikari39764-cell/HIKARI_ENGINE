#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Editor/Documents/HIKARI_EnvironmentEditorDocuments.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 環境プロファイル（.henvironment）と天気（.hweather）の資産を編集する。エンジン内蔵の資産は読み取り専用で、
// 編集する時はプロジェクトへ複製する。未保存の下書きは環境資産の store へ渡し、保存前からシーンへ映す。
// 画面と AI 操作は同じ文書を使う。
class EnvironmentAssetEditor {
public:
    // 資産の内容を描く。複製を作った時は、シーンの参照を切り替えるためにその ID を返す。
    std::string DrawProfile(const std::string& id, DocumentSceneBase& scene);
    std::string DrawWeather(const std::string& id, DocumentSceneBase& scene);
    // 下書きを store へ渡し、変わっていればシーンの環境を引き直す。毎フレームと AI の編集後に呼ぶ。
    void PublishDrafts(DocumentSceneBase& scene);

    bool CreateProfileCopy(ENVIRONMENT::EnvironmentProfileAsset source, AssetDatabase& database, std::string& message);
    bool CreateWeatherCopy(ENVIRONMENT::WeatherPresetAsset source, AssetDatabase& database, std::string& message);

    EnvironmentProfileEditorDocument& Profile() { return profile_; }
    WeatherPresetEditorDocument& Weather() { return weather_; }

private:
    EnvironmentProfileEditorDocument profile_;
    WeatherPresetEditorDocument weather_;
    uint64_t publishedProfileRevision_ = 0;
    uint64_t publishedWeatherRevision_ = 0;
    std::string status_;
};

// 内蔵とプロジェクトの環境資産（AssetType::Environment か Weather）から一つを選ぶ。emptyLabelKey があれば空を選べる。
bool DrawEnvironmentAssetPicker(AssetType type, const AssetDatabase& database, std::string& id,
                                const char* emptyLabelKey = nullptr);
std::string ResolveEnvironmentAssetLabel(AssetType type, std::string_view id, const AssetDatabase& database);

} // namespace HIKARI::EDITOR
