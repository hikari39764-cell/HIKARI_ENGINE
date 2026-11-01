#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Runtime/Diagnostics/HIKARI_PerformanceCapture.h"

namespace HIKARI {
class IScene;
}

namespace HIKARI::RUNTIME::PERFORMANCE_BREAKDOWN {

// 全体を繰り返す回数の範囲。基準どうしの差（雑音）を測るので 2 回は要る。
inline constexpr uint32_t kMinimumRounds = 2;
inline constexpr uint32_t kMaximumRounds = 8;

// 性能の分解：画質の項目を 1 つずつ切り替え、その項目が対象の GPU pass で何 ms かかっているかを測る。
// 最初の基準で対象 pass が落ち着くのを待ってから、基準と変種を交互に並べた区間（設定を切り替える→効いたのを
// 確かめる→熱身→標本）を順に測る。標本は PERFORMANCE_CAPTURE を使う。基準と変種の組は表（preset）が持つ。
// 終わると元の画質に戻す。
// コマンドライン（--benchmark-breakdown）とエディタの操作（perf.breakdown）は、どちらもここを呼ぶ。
// ホストのフレームループが PERFORMANCE_CAPTURE::OnPresentedFrame 経由で毎フレーム OnPresentedFrame を渡す。進程に
// 同時に 1 つだけ。主スレッドだけが呼ぶ。
struct Request {
    // 表の名前。
    std::string preset = "forward";
    // 結果の JSON の保存先。
    std::filesystem::path output;
    // 区間ごとの熱身と標本の枚数、全体を繰り返す回数（交互に逆順で回す）。
    uint32_t warmupFrames = 20;
    uint32_t sampleFrames = 60;
    uint32_t rounds = 2;
    bool commandLine = false;
    // 終わったらホストを終了する（--benchmark-exit）。
    bool exitWhenFinished = false;
};

struct Status {
    PERFORMANCE_CAPTURE::CaptureState state = PERFORMANCE_CAPTURE::CaptureState::Idle;
    std::string preset;
    uint32_t segmentsDone = 0;
    uint32_t segmentsTotal = 0;
    // 今測っている区間の説明。
    std::string current;
    std::filesystem::path output;
    // Failed の理由、または Finished でも途中で打ち切った時の説明。
    std::string message;
    // Finished の時だけ：保存した結果と同じ内容。
    nlohmann::json report = nlohmann::json::object();
};

// 表の名前（先頭が既定）。
std::vector<std::string_view> PresetNames();
// 進行中の分解や捕獲が無く、場面がある時に開始する。今の画質を覚え、終わると戻す。
bool Start(Request request, std::string& error);
// --benchmark-breakdown <出力.json>（--benchmark-breakdown-preset、--benchmark-breakdown-rounds、
// --benchmark-warmup-frames、--benchmark-sample-frames、--benchmark-exit）があれば開始する。無ければ何もしない。
void StartFromCommandLine();
void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene);
bool IsRunning() noexcept;
// 進行中ならその進み具合、無ければ最後に終わった分解（無ければ Idle）。
Status GetStatus();
// 進行中の分解を打ち切り、画質を戻し、そこまでを書き出す。進行中でなければ false。
bool Cancel();
// コマンドラインで始めた分解が終わり、ホストを終了すべきか。
bool ShouldExitHost() noexcept;
// コマンドラインで始めた分解の終了コード（0 が正常、2 が不完全）。それ以外は 0。
int ExitCode() noexcept;
// ホストの終了時：進行中なら打ち切って書き出す。
void Shutdown();

} // namespace HIKARI::RUNTIME::PERFORMANCE_BREAKDOWN
