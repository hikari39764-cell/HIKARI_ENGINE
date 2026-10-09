#pragma once

#include <string_view>

#include "HIKARI/Project/EnvironmentTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

class WorldView;

// シーンの環境（時刻・天気・描画の様式）の実行時の操作。保持できる非所有のビューで、所有スレッドからだけ呼ぶ。
// 変更は実行中の World だけに効き、シーン文書には保存しない（シーンの読み込みと Play の開始で
// シーンの初期時刻・天気・様式へ戻る）。空・大気・霧などの見た目は環境プロファイル資産、画面全体の描き方
// （標準の材質の受光、後処理の層）は描画の様式の資産が持つ。
class EnvironmentView {
public:
    EnvironmentView() = default;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult GetState(GameProjectEnvironmentState& outState) const noexcept;
    // 日付と時刻を置き換える（日付の妥当性を確かめる）。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetTime(const GameProjectEnvironmentTime& time) const noexcept;
    // [0, 100000]
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetTimeScale(double timeScale) const noexcept;
    // weatherId は内蔵 ID（engine.weather.*）か天気資産の GUID。transitionSeconds が負なら天気資産の既定の遷移時間、
    // 0 なら即座に切り替える。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult RequestWeather(std::string_view weatherId,
                                                                      float transitionSeconds) const noexcept;
    // 遷移先の天気の ID。次の RequestWeather / RestoreSnapshot / シーンの再構築まで有効な借用。
    HIKARI_GAME_PROJECT_SDK_API std::string_view GetWeatherId() const noexcept;
    // 描画の様式を次のフレームから切り替える。styleId は内蔵 ID（engine.style.*）か描画の様式の資産（.hrenderstyle）
    // の GUID。読めない様式は InvalidRequest。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetRenderStyle(std::string_view styleId) const noexcept;
    // 今の描画の様式の ID。次の SetRenderStyle / RestoreSnapshot / シーンの再構築まで有効な借用。
    HIKARI_GAME_PROJECT_SDK_API std::string_view GetRenderStyleId() const noexcept;
    // 今効いている色調整（置き換え中ならその値）。outGrading の size/version を初期化してから呼ぶ。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    GetColorGrading(GameProjectColorGrading& outGrading) const noexcept;
    // 環境プロファイルの色調整を置き換える（範囲へ丸める）。LUT が読めなければ InvalidRequest で変えない。
    // シーンの読み込みと Play の開始で解除される。存档には含めない。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetColorGradingOverride(const GameProjectColorGrading& grading) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ClearColorGradingOverride() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    CaptureSnapshot(GameProjectEnvironmentSnapshot& outSnapshot) const noexcept;
    // 別の版のエンジンで保存した状態や壊れた状態は InvalidRequest で拒否する。
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    RestoreSnapshot(const GameProjectEnvironmentSnapshot& snapshot) const noexcept;

private:
    friend class WorldView;
    explicit EnvironmentView(GameProjectWorldHandle world) noexcept : world_(world) {}
    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
