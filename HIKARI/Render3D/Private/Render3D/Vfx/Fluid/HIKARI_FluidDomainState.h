#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

// 流体域 1 つの CPU 側の状態（HIKARI_FluidVolumePass.cpp が段取りし、HIKARI_FluidBudget.cpp が予算を割り振る）：
// 資源、固定子步の時計、預熱、源の動きの追跡、持ち越す押すもの、回読、統計。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

// 受け取ったフレームの後の子步へ渡し続ける一度きりの事件：冲量（向きのあるものは最初の子步だけ、向きの無いもの（湧き出し）
// は残りの時間がある間）と注入（提交したフレームの時間ぶんを、子步ごとに最大 1 子步ずつ入れる）。子步の無いフレームの分は
// 次のフレームの子步へ持ち越すので、注入はフレームと子步の刻みがずれても落ちず、重ならない。
struct CarriedInteraction {
    FluidInteraction interaction{};
    double remainingSeconds = 0.0;
    // 注入：提交したフレームの時間（動いた線分の長さを決める）。
    float frameSeconds = 0.0f;
};

// 源ごと（FluidSourceSubmission::id）の、まだ子步に渡していない動きの始点（最後に走った子步の終わり。域の局所の位置と
// 向き、世界の拡縮）と、子步の無かったフレームから溜めた burst。次に走る子步たちが始点から今の位置までを等分して掃く。
struct SourceTrack {
    uint64_t id = 0u;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    float elapsedSeconds = 0.0f;
    float pendingBurst = 0.0f;
    bool seen = false;
};

// このフレームの源 1 つの今の位置（域の局所）と、追っている始点の番号（追わない源は -1）。
struct SourceMotion {
    int track = -1;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
};

struct FluidDomainState {
    PARTICLES::ParticleEmitterInstanceKey key{};
    FluidDomainResources resources{};
    MATH::Vec3 layoutSize{};
    uint32_t layoutResolution = 0u;
    FluidStepClock clock{};
    // 残りの預熱の子步（秒から子步の長さを引いていくと丸めの残りが 0 にならず、預熱が終わらない組み合わせがあった）。
    uint64_t prewarmSteps = 0u;
    // 預熱を終える前に作り直された回数（描いた時に 0 へ戻す）。
    uint32_t unfinishedRestarts = 0u;
    // 上限で解かれなかったことを日誌へ書いたか（域ごとに一度だけ）。
    bool limitWarned = false;
    // 最後に解く域に選ばれた描画入力の帧号（前のフレームに選ばれた域は選び直しで少し有利にする）。
    uint64_t admittedFrame = UINT64_MAX;
    uint32_t velocityIndex = 0u;
    uint32_t densityIndex = 0u;
    // 密度が変わるたびに増やす（子步を進めた・作り直した）。描く側は同じ版なら光照体積を書き直さない。
    uint64_t densityVersion = 0u;
    bool needsClear = true;
    // 作り直した回数（描画の履歴が古い烟を混ぜないよう、描く側へ渡す）。
    uint32_t generation = 0u;
    // 最後に時間を進めた描画入力の帧号。同じ入力を別の視図や交付待ちで再び受けても二度は進めない。
    uint64_t lastSimulatedFrame = UINT64_MAX;
    // 解算の LOD（ResolveFluidSimulationLod）と、それで決めた子步の長さ [s]（描く側の外推が読む）。
    uint32_t lod = 0u;
    double stepSeconds = 0.0;
    // 解算の予算（AssignFluidBudget）：視図の LOD に足す段（0〜2）と、予算を超えて解かずに止めた（前の烟を描くだけ）か。
    uint32_t budgetLevel = 0u;
    bool frozen = false;
    // 砖块を烟があるとみなす最大密度（INTERNAL::FluidOccupiedDensity。占有の回読が数える）。
    float occupiedDensity = 0.0f;
    // 回読の砖块ごとの活動（1：烟がある、または最後の子步で動いた。烟の 26 近傍、源・注入の届く砖块）。疎な域の住まいが
    // 読む。作り直し・再起動で全部 0。
    std::vector<uint8_t> activity{};
    // 解く空間（世界空間の域は作り直した時の変換に固定、局所空間の域は毎フレームの変換）。
    MATH::Vec3 framePosition{};
    MATH::Quat frameRotation = MATH::Quat::Identity();
    MATH::Vec3 previousPosition{};
    // 均した挂点の速度（世界 [m/s]、作り直した時は静止。動いている物体に付けた烟は最初に後ろへ流れる）。
    MATH::Vec3 smoothedVelocity{};
    bool motionInitialized = false;
    // まだ子步に渡していない挂点の速度の変化（世界 [m/s]）。次に走る子步が一度だけ逆向きに入れるので、子步の数が
    // フレームごとに変わっても烟が受ける速さの合計は挂点の速度の変化と等しい。
    MATH::Vec3 pendingInertia{};
    // 解いたゲーム時間（細部のノイズを流れに沿って運ぶ時計。float の精度のため周期で折る）。
    float simulationSeconds = 0.0f;
    std::vector<SourceTrack> tracks{};
    // 子步へ渡す冲量と注入（子步の無いフレームの分は次へ持ち越す）と、最後に押すものを受け取ったフレーム。
    std::vector<CarriedInteraction> carried{};
    uint64_t lastInteractionFrame = UINT64_MAX;
    std::array<uint64_t, GFX::kFrameResourceCount> readbackFence{};
    std::array<uint64_t, GFX::kFrameResourceCount> readbackSteps{};
    std::array<bool, GFX::kFrameResourceCount> readbackDiagnostics{};
    std::array<bool, GFX::kFrameResourceCount> readbackPending{};
    // 回読に汇の吸い込み量が入っているか（そのフレームに数えた子步があったか）と、押すものの表の行ごとの reportKey。
    std::array<bool, GFX::kFrameResourceCount> readbackSinks{};
    std::array<std::array<uint64_t, kMaximumFluidInteractionsPerDomain>, GFX::kFrameResourceCount> readbackSinkKeys{};
    // 回読に発光が入っているか（reportGlow の域）と、その時の発光（起亮の色と熱い端の色 × emissionIntensity）、局所空間か。
    struct GlowReadback {
        bool requested = false;
        MATH::Vec3 emission{};
        MATH::Vec3 hotEmission{};
        bool localSpace = false;
    };
    // 最後に送った発光が 0 より大きいか（解かれなくなった時に一度だけ 0 を送る）。
    bool glowLit = false;
    std::array<GlowReadback, GFX::kFrameResourceCount> readbackGlow{};
    FluidDomainStats stats{};
};

// 域の 1 フレームの解算の見積もり（単位は百万格・子步）：子步の数（フレームの時間 / 子步、maxSubsteps まで）×（活動する
// 砖块の割合 × 2（平流・力・注入）+ V 循環 × 0.5（圧力は域全体を解く））× 格の数。
double EstimateFluidSolveCost(const FluidGridLayout& layout, float activeFraction, double frameSeconds,
                              double stepSeconds, uint32_t maxSubsteps, uint32_t vCycles) noexcept;

// 予算の段 0〜2 ごとの見積もりと、割り振った段・止めたか（前のフレームの値から始める）。
struct FluidBudgetEntry {
    double cost[3]{};
    uint32_t level = 0u;
    bool frozen = false;
};

// 予算の割り振り（Niagara の重要度による間引きと同じ考え方）。entries は重要な順（作者の優先度、相機に近い順）。見積もりの
// 合計が budget を超える間は最も重要でない域から段を上げ（子步を長く、V 循環を 1 つ減らす）、段が尽きたら重要でない域から
// 止める（最も重要な域は止めない）。予算の 7 割を下回ったら重要な域から止めたのを戻し、段を下げる（戻した後も 8.5 割以内の
// 間だけ。境目で毎フレーム行き来しない）。budget が 0 なら予算なし。
void AssignFluidBudget(std::span<FluidBudgetEntry> entries, double budget) noexcept;

// 見積もりから GPU の時間 [ms] への換算（画質の solveBudgetMs を見積もりの単位にする）。装置と場面で変わるので、流体解算
// の GPU 計時（数フレーム遅れて読み戻る）を同じフレームの見積もりと組にし、kFluidCalibrationFrames 組ごとに比べて半分ずつ
// 寄せる（1 回に 2 倍まで）。預熱（見積もりの外の子步）のフレームと、見積もりがごく小さいフレームは組にしない。GPU 計時が
// 切ってある間は 1 のまま。
inline constexpr uint32_t kFluidCalibrationFrames = 32u;
class FluidBudgetCalibration final {
public:
    // このフレーム（帧号）に解いた域の見積もりの合計と、見積もりの外の仕事（預熱）があったか。
    void Record(uint64_t frameIndex, double units, bool unmodeled) noexcept;
    void Update();
    double BudgetUnits(double budgetMs) const noexcept { return budgetMs / msPerUnit_; }

private:
    struct Frame {
        uint64_t index = UINT64_MAX;
        double units = 0.0;
        bool unmodeled = false;
    };
    std::array<Frame, 16> frames_{};
    uint64_t measuredFrame_ = 0u;
    double msPerUnit_ = 1.0;
    double ms_ = 0.0;
    double units_ = 0.0;
    uint32_t pairs_ = 0u;
};

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
