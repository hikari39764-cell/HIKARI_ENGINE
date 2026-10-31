#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::METRIC {

// 性能指標の登録表。値の所有者（プロファイラなど）が Publish し、パネル・審計・基準・コンソールは Snapshot だけを読む。
// 進程に 1 つ。主スレッドだけが読み書きする前提で、ロックは持たない。
//
// 指標 id は点区切り（区間名・pass 名は表示名そのまま。区間名は '.' を含んでよい）:
//   frame.fps / frame.cpuMs / frame.gpuMs
//   cpu.<区間名>.ms|calls
//   gpu.<pass 名>.ms|calls|selfMs, gpu.<pass 名>.pipeline.<項>
//   render.<計数名>, memory.vram.usage|budget
// 按 pass の指標は、その pass が初めて計測された時に登録する。

enum class MetricCategory : uint8_t { Work, Wait, Count, Memory };
enum class MetricStatus : uint8_t { NoData, Unsupported, Stale, Fresh };

struct MetricDescriptor {
    std::string id;
    std::string owner;
    std::string unit;
    MetricCategory category = MetricCategory::Count;
    std::string help;
};

using MetricHandle = uint32_t;
inline constexpr MetricHandle kInvalidMetric = UINT32_MAX;

// 来源フレームがこれより古い値は Stale。GPU 計時は読み戻しで数フレーム遅れて届く。
inline constexpr uint64_t kStaleAfterFrames = 30u;

// 読み手が複数いる指標の id。作る側の Register と読む側の検索で、同じ綴りを使う。
inline constexpr const char* kFrameFps = "frame.fps";
inline constexpr const char* kFrameCpuMs = "frame.cpuMs";
inline constexpr const char* kFrameGpuMs = "frame.gpuMs";
inline constexpr const char* kGpuPipelineExecutions = "gpu.pipeline.executions";
inline constexpr const char* kVramUsage = "memory.vram.usage";
inline constexpr const char* kVramBudget = "memory.vram.budget";

// 直近の標本窓（Work / Wait のみ。標本が無ければ全部 0）。
struct MetricWindow {
    double average = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
    uint32_t sampleCount = 0u;
};

struct MetricReading {
    const MetricDescriptor* descriptor = nullptr;
    // NoData と Unsupported では 0 であって計測値ではない。必ず status を見ること。
    double value = 0.0;
    uint64_t sourceFrame = 0;
    MetricStatus status = MetricStatus::NoData;
    // Unsupported の理由。快照は跨フレームで持たれることがある（審計）ので、登録表の文字列を指さず写しを持つ。
    std::string note;
    MetricWindow window;
};

// 同じ id の再登録は同じハンドルを返す。
MetricHandle Register(MetricDescriptor descriptor);
// 値を出す。Work / Wait は標本窓にも入る。Unsupported だった指標も Fresh に戻る。非有限値は捨てる。
void Publish(MetricHandle handle, double value, uint64_t frameIndex);
// この構成では値が出ないことを示す。理由は画面に出る。
void MarkUnsupported(MetricHandle handle, std::string_view reason);
// 所有者の全指標を「データなし」へ戻し、標本窓を空にする（統計の再開始、終了処理）。
void ClearOwner(std::string_view owner);

// 全指標の現在値。来源フレームが currentFrame より kStaleAfterFrames を超えて古い値は Stale になる。
std::vector<MetricReading> Snapshot(uint64_t currentFrame);
const MetricReading* FindReading(const std::vector<MetricReading>& readings, std::string_view id);

// "<domain>.<名前>.ms" の指標ごとに、同じ <名前> の ".calls" と ".selfMs" を引き当てた行。
// ポインタは readings の要素を指すので、readings より長く持たないこと。
struct TimingRow {
    std::string_view name;
    const MetricReading* ms = nullptr;
    const MetricReading* calls = nullptr;
    const MetricReading* selfMs = nullptr;
};
std::vector<TimingRow> TimingRows(const std::vector<MetricReading>& readings, std::string_view domain);

const char* ToString(MetricStatus status);
const char* ToString(MetricCategory category);

} // namespace HIKARI::METRIC
