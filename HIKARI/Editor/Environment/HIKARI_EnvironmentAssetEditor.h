#pragma once

#include <cstdint>
#include <functional>
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
    // 布景でよく触る項目（太陽の向きと強さ、空の光、影、霧、露出、雲）だけを描く。内蔵プロファイルは最初の編集で
    // プロジェクトへ複製し、その ID を返す。
    std::string DrawLightingControls(const std::string& id, DocumentSceneBase& scene);
    // 光照の型を切り替える（美術布景 = 太陽の向きを固定、昼夜 = 日付と地点）。固定へ移る時は今の太陽の位置から始める。
    // 内蔵プロファイルは複製してその ID を返す。
    std::string SetSunMode(const std::string& id, SunMode mode, DocumentSceneBase& scene);
    // プロファイルを edit で書き換える（edit が false なら何もしない）。continuing の間（ドラッグ中）の編集は一つの
    // 取り消しにまとめ、false の呼び出しで区切る。内蔵プロファイルは複製してその ID を返す。
    std::string EditProfile(const std::string& id, DocumentSceneBase& scene,
                            const std::function<bool(ENVIRONMENT::EnvironmentProfileAsset&)>& edit, bool continuing);
    // 下書きを store へ渡し、変わっていればシーンの環境を引き直す。毎フレームと AI の編集後に呼ぶ。
    void PublishDrafts(DocumentSceneBase& scene);

    EnvironmentProfileEditorDocument& Profile() { return profile_; }
    const EnvironmentProfileEditorDocument& Profile() const { return profile_; }
    WeatherPresetEditorDocument& Weather() { return weather_; }
    const WeatherPresetEditorDocument& Weather() const { return weather_; }

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
// 画面预设：内蔵とプロジェクトの環境プロファイルを押して選べる札として並べる。選び直したら true。
bool DrawEnvironmentProfilePresets(const AssetDatabase& database, std::string& id);

} // namespace HIKARI::EDITOR
