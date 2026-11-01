#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <json.hpp>

namespace HIKARI {
class IScene;
}

namespace HIKARI::RUNTIME::PERFORMANCE_CAPTURE {

// 性能捕獲：熱身 warmupFrames 枚のあと sampleFrames 枚を登録表から取り、キー順の決まった JSON 摘要に書く。
// コマンドライン（--benchmark-output）とエディタの操作（perf.capture）は、どちらもここを呼ぶ。
// ホストのフレームループが毎フレーム OnPresentedFrame を渡す。進程に同時に 1 つだけ。主スレッドだけが呼ぶ。
// 性能の分解（HIKARI_PerformanceBreakdown.h）も同じ OnPresentedFrame と終了の処理で進める。
struct Request {
    // 摘要 JSON の保存先。
    std::filesystem::path output;
    // 持っていれば内部の捕獲：摘要を書かず（output は要らない）、終わった時にここへ渡すだけで、最後の捕獲の記録
    // （GetStatus）もホスト終了の要求も変えない。性能の分解が区間ごとの標本をこれで採る。
    std::function<void(const nlohmann::json& report, const std::string& message)> onFinished;
    // 空でなければ、採取区間の CPU と GPU の時間線（chrome://tracing 形式）もここへ書く。
    std::filesystem::path tracePath;
    // 枚数の既定値は、コマンドラインもエディタの操作もここを使う。
    uint32_t warmupFrames = 180;
    uint32_t sampleFrames = 600;
    // コマンドラインから始めた捕獲は、摘要の origin が commandLine になり、プロセスの終了コードに結果が出る。
    bool commandLine = false;
    // 終わったらホストを終了する（--benchmark-exit）。
    bool exitWhenFinished = false;
    // 既定値以外なら、起動から最初の表示までの時間（startup）も書く。
    std::chrono::steady_clock::time_point startupStarted{};
};

enum class CaptureState : uint8_t { Idle, Running, Finished, Failed };

struct Status {
    CaptureState state = CaptureState::Idle;
    uint32_t warmupFrames = 0;
    uint32_t warmedFrames = 0;
    uint32_t sampleFrames = 0;
    uint32_t sampledFrames = 0;
    std::filesystem::path output;
    std::filesystem::path tracePath;
    // Failed の理由。Finished でも時間線が書けなかった時は、その説明が入る。
    std::string message;
    // Finished の時だけ：保存した摘要と同じ内容。
    nlohmann::json report = nlohmann::json::object();
};

// 現在のローカル時刻。摘要の capturedAt（2026-11-01T05:47:00）と、保存ファイル名（20261101-054700）が使う。
std::string LocalTimestamp(bool forFileName);
// 進行中の捕獲が無ければ開始する。
bool Start(Request request, std::string& error);
// --benchmark-output（--benchmark-warmup-frames、--benchmark-sample-frames、--benchmark-trace、--benchmark-exit）
// があれば開始する。無ければ何もしない。
void StartFromCommandLine(std::chrono::steady_clock::time_point startupStarted);
void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene);
// 進行中ならその進み具合、無ければ最後に終わった捕獲（無ければ Idle）。内部の捕獲は数えない。
Status GetStatus();
// 進行中の捕獲を打ち切り、そこまでを書き出す（無ければ何もしない）。ホストの終了時は Shutdown が呼ぶ。
void Cancel();
// コマンドラインで始めた捕獲が終わり、ホストを終了すべきか。
bool ShouldExitHost() noexcept;
// コマンドラインで始めた捕獲の終了コード（0 が正常、2 が不完全）。それ以外は 0。
int ExitCode() noexcept;
// ホストの終了時：進行中の捕獲を打ち切り、そこまでを書き出す。
void Shutdown();

} // namespace HIKARI::RUNTIME::PERFORMANCE_CAPTURE
