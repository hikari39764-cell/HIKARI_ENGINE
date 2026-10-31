#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI::TRACING {

// 性能捕獲の採取区間だけ、CPU 区間と GPU pass の「一回ごとの開始と終わり」を溜め、chrome://tracing で開ける JSON に書く。
// 記録していない間は IsRecording() が偽を返すだけで、ほかの費用はない。主スレッドだけが呼ぶ（ロックなし）。
namespace detail {
inline bool gRecording = false;
}

inline bool IsRecording() noexcept { return detail::gRecording; }

// 記録を始める。イベントの領域は最初に 1 回だけ確保し、溢れた分は捨てて数える（記録中に確保しない）。
void Start(size_t eventCapacity);
void Stop() noexcept;
// 溜めたイベントを捨てる。
void Clear() noexcept;

// name と module は記録が終わるまで有効な文字列を渡す（リテラルか、消えない区間名）。
void RecordCpu(const char* name, const char* module, std::chrono::steady_clock::time_point begin,
               std::chrono::steady_clock::time_point end, uint64_t frame);

// GPU のタイムスタンプを CPU の時計へ写すための値。calibrated は ID3D12CommandQueue::GetClockCalibration の
// 結果（gpuTicks と cpuNanoseconds は同じ瞬間）が取れた時だけ真。取れない時は周波数だけを渡す。
struct GpuClock {
    uint64_t ticksPerSecond = 0;
    uint64_t gpuTicks = 0;
    int64_t cpuNanoseconds = 0;
    bool calibrated = false;
};
void SetGpuClock(const GpuClock& clock);
// SetGpuClock を呼んだか（取れなかった場合も真。毎フレーム試し直さないための印）。
bool HasGpuClock() noexcept;
// parent は入れ子の外側の pass の名前。無ければ nullptr。
void RecordGpu(const char* name, const char* module, const char* parent, uint64_t beginTicks, uint64_t endTicks,
               uint64_t frame);

struct TraceStats {
    uint64_t events = 0;
    uint64_t dropped = 0;
    // GPU の時間線が CPU の時計に合わせてあるか。偽なら GPU 側は先頭を CPU 側の先頭に揃えただけ。
    bool gpuClockAligned = false;
};
// frame が [firstFrame, lastFrame] のイベントを、CPU と GPU を別の tid にして書く。
bool Write(const std::filesystem::path& path, uint64_t firstFrame, uint64_t lastFrame, TraceStats& stats,
           std::string& error);

} // namespace HIKARI::TRACING
