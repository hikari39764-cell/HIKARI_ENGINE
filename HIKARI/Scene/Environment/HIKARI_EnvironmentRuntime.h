#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Scene/Environment/HIKARI_EnvironmentSimulation.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"

// シーンの環境の実行時の所有者（World サービス）。環境プロファイルと天気資産、プロファイルが参照する色調整の
// LUT を解決し、時刻・天気のシミュレーションを進め、毎フレームの描画入力 SceneEnvironment を作る。
// 編集中のプレビューと Play で同じものを使い、実行時の変更はシーン文書へ書き戻さない。
namespace HIKARI::ENVIRONMENT {

// 天気の霧と霞をプロファイルへ重ねる。雲と風は BuildLightingFrame が lighting frame の clouds へ渡す。
void ApplyWeather(EnvironmentProfile& profile, const WeatherParameters& weather);
// 天体の状態から太陽と月の方向光を求める。太陽が十分に沈むと月が影の主光源になる。
EnvironmentLightingFrame BuildLightingFrame(const EnvironmentProfile& profile, const EnvironmentFrameState& state,
                                            const GeoLocation& location);

class EnvironmentRuntime {
public:
    void Configure(const EnvironmentAssetStore* store) noexcept { store_ = store; }
    // シーンの参照で初期化する（シーンの読み込み、Play 開始、参照の編集）。
    void Reset(const SceneEnvironmentBinding& binding);
    // 資産の再読み込みや下書きの変更を拾う。時刻と天気の状態は保つ。
    void RefreshProfile();
    void Advance(float deltaSeconds);
    SceneEnvironment ResolveFrame() const;

    const SceneEnvironmentBinding& GetBinding() const noexcept { return binding_; }
    const EnvironmentProfile& GetProfile() const noexcept { return profile_; }
    const std::string& GetError() const noexcept { return error_; }
    // プロファイルの色調整が参照する LUT。参照が無いか読めなければ nullptr（読めない理由は GetError）。
    const std::shared_ptr<const ASSETS::COLOR::ColorLutAsset>& GetColorLut() const noexcept { return colorLut_; }
    EnvironmentSimulation& Simulation() noexcept { return simulation_; }
    const EnvironmentSimulation& Simulation() const noexcept { return simulation_; }
    const std::string& GetWeatherId() const noexcept { return weatherId_; }
    // 今の描画の様式（シーンの参照で初期化し、ゲームが実行中に切り替えられる）。
    const std::string& GetRenderStyleId() const noexcept { return renderStyleId_; }
    void SetRenderStyle(std::string_view renderStyleId) { renderStyleId_ = renderStyleId; }

    // 天気を weatherId へ seconds 秒で切り替える。seconds が負なら天気資産の既定の遷移時間。
    bool RequestWeather(std::string_view weatherId, float seconds, std::string& error);
    // 存档の復元。weatherId は遷移先の天気、renderStyleId は描画の様式。
    bool Restore(const EnvironmentSnapshot& snapshot, std::string_view weatherId, std::string_view renderStyleId);

private:
    const EnvironmentAssetStore* store_ = nullptr;
    SceneEnvironmentBinding binding_{};
    EnvironmentProfile profile_{};
    std::shared_ptr<const ASSETS::COLOR::ColorLutAsset> colorLut_{};
    std::string error_{};
    std::string weatherId_{};
    std::string renderStyleId_{};
    EnvironmentSimulation simulation_{};
};

} // namespace HIKARI::ENVIRONMENT
