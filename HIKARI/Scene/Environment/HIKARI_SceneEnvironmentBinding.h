#pragma once

#include <string>

#include <json.hpp>

#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Environment/HIKARI_CelestialModel.h"

// シーンが保存する環境の参照と初期状態。空・大気・霧などの設定は環境プロファイル資産、画面全体の描き方
// （後処理の重ね方など）は描画の様式の資産が持つ。
namespace HIKARI::ENVIRONMENT {

struct SceneEnvironmentBinding {
    // 内蔵 ID（engine.environment.*）か .henvironment の GUID。
    std::string profileId = "engine.environment.default";
    GeoLocation location{};
    CalendarTime time{};
    // 1 で実時間、60 で実時間 1 分が 1 時間。0 で時刻を止める。
    double timeScale = 0.0;
    // 内蔵 ID（engine.weather.*）か .hweather の GUID。空ならプロファイルの既定の天気。
    std::string weatherId{};
    // 内蔵 ID（engine.style.*）か .hrenderstyle の GUID。
    std::string renderStyleId{RENDERSTYLE::kDefaultRenderStyleId};

    bool operator==(const SceneEnvironmentBinding&) const = default;
};

// 地点・時刻・参照の項目表。シーン文書の読み書き・正規化・エディターの項目表示が共有する。
inline constexpr auto kLocationFields = [](auto&& field) {
    using JSONFIELDS::Range;
    field("latitudeDegrees", &GeoLocation::latitudeDegrees, Range{-90.0, 90.0});
    field("longitudeDegrees", &GeoLocation::longitudeDegrees, Range{-180.0, 180.0});
    field("utcOffsetHours", &GeoLocation::utcOffsetHours, Range{-14.0, 14.0});
    field("northYawDegrees", &GeoLocation::northYawDegrees, Range{-360.0, 360.0});
};

inline constexpr auto kTimeFields = [](auto&& field) {
    using JSONFIELDS::Range;
    field("year", &CalendarTime::year, Range{1.0, 9999.0});
    field("month", &CalendarTime::month, Range{1.0, 12.0});
    field("day", &CalendarTime::day, Range{1.0, 31.0});
    field("localHours", &CalendarTime::localHours, Range{0.0, 23.999999});
};

inline constexpr auto kBindingFields = [](auto&& field) {
    field("profile", &SceneEnvironmentBinding::profileId);
    field("timeScale", &SceneEnvironmentBinding::timeScale, JSONFIELDS::Range{0.0, 100000.0});
    field("weather", &SceneEnvironmentBinding::weatherId);
    field("renderStyle", &SceneEnvironmentBinding::renderStyleId);
};

void ReadSceneEnvironmentBinding(const nlohmann::json& node, SceneEnvironmentBinding& binding);
nlohmann::json WriteSceneEnvironmentBinding(const SceneEnvironmentBinding& binding);

} // namespace HIKARI::ENVIRONMENT
