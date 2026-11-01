#include "Diagnostics/HIKARI_PerformanceAnalysis.h"
#include "Diagnostics/Internal/HIKARI_AnalysisJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace HIKARI::DIAGNOSTICS {
namespace {
using Json = nlohmann::json;
using namespace ANALYSIS;

constexpr double kMiB = 1024.0 * 1024.0;

// ---- 判定の閾値（結果の thresholds にも出す） ----
// 耗時のような量は、差の絶対値と比率の両方が閾値を超えた時だけ変化とみなす。
struct Threshold {
    double absolute;
    double percent;
};
// 同じ構成の 2 回で亜ミリ秒の区間が 0.19 ms（26%）揺れた。このノート PC は会話をまたぐと ±40% 揺れるので、比べるのは
// 同じ会話で交互に撮ったものに限る。
constexpr Threshold kMsThreshold{0.25, 10.0};
constexpr Threshold kBytesThreshold{32.0 * kMiB, 5.0};
constexpr Threshold kFpsThreshold{1.0, 3.0};
constexpr Threshold kCountThreshold{1.0, 10.0};

// 区間の呼び出しが 1 フレームにこの回数以上なら「多い」と書く。
constexpr double kManyCallsPerFrame = 32.0;
// 枠（フレーム間隔）のこの割合以上を埋めていれば、その側が足りないとみなす。
constexpr double kBoundShare = 0.9;
// CPU が GPU のフェンスを待つ時間が間隔のこの割合以上なら、GPU を待っている。
constexpr double kFenceWaitShare = 0.25;
// VRAM の使用がこの割合以上で警告、さらに高ければ重大。
constexpr double kVramWarnRatio = 0.85;
constexpr double kVramCriticalRatio = 0.95;

Threshold ThresholdFor(std::string_view unit) {
    if (unit == "bytes")
        return kBytesThreshold;
    if (unit == "fps")
        return kFpsThreshold;
    if (unit == "count")
        return kCountThreshold;
    return kMsThreshold;
}

// ---- 区間（pass）の行 ----
struct PassRow {
    std::string name;
    std::string module;
    std::string category;
    double meanMs = 0.0;
    double selfMeanMs = 0.0;
    double callsMean = 0.0;
    double callsMax = 0.0;
};

// self 時間を持つ区間（フレーム全体の行は持たない）。workOnly なら、待ち時間の区間を除く。
std::vector<PassRow> PassRows(const Json& passes, bool workOnly) {
    std::vector<PassRow> rows;
    if (!passes.is_object())
        return rows;
    for (auto it = passes.begin(); it != passes.end(); ++it) {
        const Json& entry = it.value();
        if (!HasNumber(entry, "selfMeanMs"))
            continue;
        PassRow row;
        row.name = it.key();
        row.module = StringOr(entry, "module");
        row.category = StringOr(entry, "category", "work");
        row.meanMs = NumberOr(entry, "meanMs");
        row.selfMeanMs = NumberOr(entry, "selfMeanMs");
        row.callsMean = NumberOr(entry, "callsMean");
        row.callsMax = NumberOr(entry, "callsMax");
        if (!workOnly || row.category == "work")
            rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const PassRow& lhs, const PassRow& rhs) { return lhs.selfMeanMs > rhs.selfMeanMs; });
    return rows;
}

Json PassRowJson(const PassRow& row) {
    return {{"name", row.name}, {"module", row.module}, {"selfMeanMs", row.selfMeanMs}, {"meanMs", row.meanMs}};
}

// フレーム全体の行（CPU Frame / Command Frame）の平均。無ければ 0。
double FrameMean(const Json& passes, const char* name) { return NumberOr(Child(passes, name), "meanMs"); }

// ---- 指標の取り出し（比較用） ----
struct MetricValue {
    double value = 0.0;
    std::string unit;
    std::string module;
    bool higherIsBetter = false;
};
using MetricMap = std::map<std::string, MetricValue>;

void AddPassMetrics(MetricMap& metrics, const Json& passes, const char* domain) {
    if (!passes.is_object())
        return;
    for (auto it = passes.begin(); it != passes.end(); ++it) {
        const Json& entry = it.value();
        const std::string base = std::string(domain) + "." + it.key();
        const std::string module = StringOr(entry, "module");
        if (HasNumber(entry, "meanMs"))
            metrics[base + ".ms"] = {NumberOr(entry, "meanMs"), "ms", module};
        if (HasNumber(entry, "callsMean"))
            metrics[base + ".calls"] = {NumberOr(entry, "callsMean"), "count", module};
    }
}

MetricMap ExtractMetrics(const Json& capture) {
    MetricMap metrics;
    const Json& interval = Child(capture, "frameInterval");
    if (HasNumber(interval, "meanMs"))
        metrics["frame.intervalMeanMs"] = {NumberOr(interval, "meanMs"), "ms", "Runtime"};
    if (HasNumber(interval, "p95Ms"))
        metrics["frame.intervalP95Ms"] = {NumberOr(interval, "p95Ms"), "ms", "Runtime"};
    if (HasNumber(capture, "renderedFps"))
        metrics["frame.renderedFps"] = {NumberOr(capture, "renderedFps"), "fps", "Runtime", true};
    AddPassMetrics(metrics, Child(capture, "cpuPasses"), "cpu");
    AddPassMetrics(metrics, Child(capture, "gpuPasses"), "gpu");

    const Json& modules = Child(capture, "modules");
    if (modules.is_object()) {
        for (auto it = modules.begin(); it != modules.end(); ++it) {
            metrics["module." + it.key() + ".cpuSelfMs"] = {NumberOr(it.value(), "cpuSelfMs"), "ms", it.key()};
            metrics["module." + it.key() + ".gpuSelfMs"] = {NumberOr(it.value(), "gpuSelfMs"), "ms", it.key()};
        }
    }

    const Json& memory = Child(capture, "memory");
    if (HasNumber(memory, "peakLocalVideoUsageBytes"))
        metrics["memory.vram.peakUsage"] = {NumberOr(memory, "peakLocalVideoUsageBytes"), "bytes", "Gfx"};
    if (HasNumber(memory, "peakWorkingSetBytes"))
        metrics["memory.process.peakWorkingSet"] = {NumberOr(memory, "peakWorkingSetBytes"), "bytes", "Runtime"};
    if (HasNumber(memory, "peakPrivateBytes"))
        metrics["memory.process.peakPrivate"] = {NumberOr(memory, "peakPrivateBytes"), "bytes", "Runtime"};

    // 用途別の VRAM（最後の値）。usage と budget は peak の方で見る。
    const Json& finish = Child(capture, "metricsAtFinish");
    if (finish.is_object()) {
        for (auto it = finish.begin(); it != finish.end(); ++it) {
            const std::string& id = it.key();
            if (id.rfind("memory.vram.", 0) != 0 || id == "memory.vram.usage" || id == "memory.vram.budget" ||
                StringOr(it.value(), "unit") != "bytes" || !HasNumber(it.value(), "value"))
                continue;
            metrics[id] = {NumberOr(it.value(), "value"), "bytes", StringOr(it.value(), "module")};
        }
    }
    return metrics;
}

// ---- 比較の「同じ構成か」 ----
// 2 つのオブジェクトを項目ごとに比べ、違う項目を differences に書く。全部同じなら真。
bool SameFields(const std::string& prefix, const Json& left, const Json& right, std::vector<std::string>& differences) {
    bool same = true;
    std::vector<std::string> keys;
    for (const Json* object : {&left, &right}) {
        if (object->is_object()) {
            for (auto it = object->begin(); it != object->end(); ++it) {
                if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
                    keys.push_back(it.key());
            }
        }
    }
    for (const std::string& key : keys) {
        const Json& l = Child(left, key.c_str());
        const Json& r = Child(right, key.c_str());
        if (l != r) {
            same = false;
            differences.push_back(prefix + key + ": " + l.dump() + " -> " + r.dump());
        }
    }
    return same;
}

Json DescribeComparability(const Json& a, const Json& b) {
    const Json& left = Child(a, "context");
    const Json& right = Child(b, "context");
    std::vector<std::string> differences;
    std::vector<std::string> warnings;

    const bool buildKnown = Child(left, "build").is_object() && Child(right, "build").is_object();
    const bool sameBuild = buildKnown && SameFields("build.", Child(left, "build"), Child(right, "build"), differences);
    if (!buildKnown) {
        differences.push_back("build: identity missing in at least one capture");
    }
    const bool sameScene = SameFields(
        "", Json{{"sceneId", Child(left, "sceneId")}, {"sceneDocumentRevision", Child(left, "sceneDocumentRevision")}},
        Json{{"sceneId", Child(right, "sceneId")}, {"sceneDocumentRevision", Child(right, "sceneDocumentRevision")}},
        differences);
    bool sameQuality =
        SameFields("renderQuality.", Child(left, "renderQuality"), Child(right, "renderQuality"), differences);
    sameQuality &= SameFields("dimensions.", Child(left, "dimensions"), Child(right, "dimensions"), differences);
    const bool sameAdapter = SameFields("adapter.", Json{{"name", Child(Child(left, "adapter"), "name")}},
                                        Json{{"name", Child(Child(right, "adapter"), "name")}}, differences);
    const auto setup = [](const Json& context) {
        return Json{{"editorUiEnabled", Child(context, "editorUiEnabled")},
                    {"debugLayer", Child(context, "debugLayer")},
                    {"gpuValidation", Child(context, "gpuValidation")},
                    {"enableGpuFrameProfiler", Child(Child(context, "profiling"), "enableGpuFrameProfiler")},
                    {"enableGpuPipelineStats", Child(Child(context, "profiling"), "enableGpuPipelineStats")}};
    };
    const bool sameSetup = SameFields("setup.", setup(left), setup(right), differences);

    if (!sameBuild)
        warnings.push_back("The captures come from different builds (or the build identity is missing).");
    if (!sameScene)
        warnings.push_back("The captures are of different scenes or scene revisions.");
    if (!sameQuality)
        warnings.push_back("Render quality or resolution differs.");
    if (!sameAdapter)
        warnings.push_back("The captures ran on different GPUs.");
    if (!sameSetup)
        warnings.push_back("The capture setup differs (editor UI, debug layer or profiling options).");
    for (const char* side : {"a", "b"}) {
        const Json& capture = side[0] == 'a' ? a : b;
        if (!BoolOr(capture, "captureComplete", false) || !BoolOr(capture, "gpuCoverageComplete", false) ||
            !BoolOr(capture, "configurationStable", false))
            warnings.push_back(std::string("Capture ") + side +
                               " is incomplete or its configuration changed while it ran.");
    }
    return {{"sameBuild", sameBuild},     {"sameScene", sameScene}, {"sameQuality", sameQuality},
            {"sameAdapter", sameAdapter}, {"sameSetup", sameSetup}, {"differences", differences},
            {"warnings", warnings}};
}

Json ThresholdsJson() {
    const auto describe = [](const Threshold& threshold) {
        return Json{{"absolute", threshold.absolute}, {"percent", threshold.percent}};
    };
    return {{"ms", describe(kMsThreshold)},
            {"bytes", describe(kBytesThreshold)},
            {"fps", describe(kFpsThreshold)},
            {"count", describe(kCountThreshold)},
            {"rule", "A change counts only when both the absolute difference and the percentage reach the threshold."}};
}

// ---- 発見 ----
// CPU / GPU のどちらが足りないか。フレーム間隔・GPU のフレーム・CPU の稼働（待ちを除く）を比べる。
void AddBoundFinding(const Json& capture, std::vector<Json>& findings, std::string& bound) {
    const double interval = NumberOr(Child(capture, "frameInterval"), "meanMs");
    const Json& cpu = Child(capture, "cpuPasses");
    const Json& gpu = Child(capture, "gpuPasses");
    const double cpuFrame = FrameMean(cpu, "CPU Frame");
    const double gpuFrame = FrameMean(gpu, "Command Frame");
    double waits = 0.0;
    if (cpu.is_object()) {
        for (auto it = cpu.begin(); it != cpu.end(); ++it) {
            if (StringOr(it.value(), "category") == "wait")
                waits += NumberOr(it.value(), "meanMs");
        }
    }
    const double fenceWait = FrameMean(cpu, "Fence Wait");
    const double cpuBusy = (std::max)(0.0, cpuFrame - waits);
    bound = "unknown";
    if (interval <= 0.0 || cpuFrame <= 0.0) {
        findings.push_back(Finding("bound.unknown", "info", "CPU/GPU limit unknown",
                                   "The capture has no usable frame interval or CPU frame time.",
                                   {"capture.frameInterval.meanMs", "frame.cpuMs"}));
        return;
    }
    const double cpuShare = cpuBusy / interval;
    const double gpuShare = gpuFrame / interval;
    const double fenceShare = fenceWait / interval;
    const bool vsync = BoolOr(Child(Child(capture, "context"), "renderQuality"), "vSync", false);
    const std::string numbers = "interval " + Fixed(interval, 2) + " ms, CPU busy " + Fixed(cpuBusy, 2) + " ms (" +
                                Fixed(cpuShare * 100.0, 0) + "%), GPU frame " + Fixed(gpuFrame, 2) + " ms (" +
                                Fixed(gpuShare * 100.0, 0) + "%), fence wait " + Fixed(fenceWait, 2) + " ms";
    const std::vector<std::string> evidence = {"capture.frameInterval.meanMs", "frame.cpuMs", "frame.gpuMs",
                                               "cpu.Fence Wait.ms", "cpu.Present.ms"};
    Json data = {{"intervalMs", interval}, {"cpuBusyMs", cpuBusy},     {"cpuWaitMs", waits},
                 {"gpuFrameMs", gpuFrame}, {"fenceWaitMs", fenceWait}, {"vsync", vsync}};
    if (gpuFrame > 0.0 && gpuShare >= kBoundShare && cpuShare < kBoundShare) {
        bound = "gpu";
        findings.push_back(Finding("bound.gpu", "info", "GPU-limited",
                                   "The GPU fills the frame while the CPU has headroom: " + numbers + ".", evidence,
                                   std::move(data)));
    } else if (cpuShare >= kBoundShare && (gpuFrame <= 0.0 || gpuShare < kBoundShare)) {
        bound = "cpu";
        findings.push_back(Finding("bound.cpu", "info", "CPU-limited",
                                   "The CPU fills the frame while the GPU has headroom: " + numbers + ".", evidence,
                                   std::move(data)));
    } else if (vsync) {
        bound = "pacing";
        findings.push_back(Finding("bound.pacing", "info", "Paced by VSync",
                                   "VSync is on and neither side fills the interval: " + numbers + ".", evidence,
                                   std::move(data)));
    } else if (fenceShare >= kFenceWaitShare) {
        bound = "gpu";
        findings.push_back(Finding("bound.gpu", "info", "Waiting on the GPU",
                                   "The CPU spends a large part of the frame waiting for the GPU fence: " + numbers +
                                       ". GPU timing may be missing or coarse.",
                                   evidence, std::move(data)));
    } else {
        bound = "balanced";
        findings.push_back(Finding("bound.balanced", "info", "Neither side fills the frame",
                                   "The frame rate is limited by something else (frame pacing, present, a frame "
                                   "limiter): " +
                                       numbers + ".",
                                   evidence, std::move(data)));
    }
}

void AddTopFindings(const Json& capture, std::vector<Json>& findings) {
    const double gpuFrame = FrameMean(Child(capture, "gpuPasses"), "Command Frame");
    const std::vector<PassRow> gpuRows = PassRows(Child(capture, "gpuPasses"), false);
    if (!gpuRows.empty() && gpuRows.front().selfMeanMs > 0.0) {
        std::string detail;
        std::vector<std::string> evidence;
        Json items = Json::array();
        for (size_t i = 0; i < (std::min)(gpuRows.size(), size_t{3}); ++i) {
            const PassRow& row = gpuRows[i];
            detail += (i == 0 ? "" : "; ") + row.name + " " + Fixed(row.selfMeanMs, 2) + " ms (" + row.module + ")";
            evidence.push_back("gpu." + row.name + ".selfMs");
            items.push_back(PassRowJson(row));
        }
        const double share = gpuFrame > 0.0 ? gpuRows.front().selfMeanMs / gpuFrame * 100.0 : 0.0;
        findings.push_back(Finding("top.gpu", "info", "Most expensive GPU passes (self time)",
                                   detail + ". The first is " + Fixed(share, 0) + "% of the GPU frame.", evidence,
                                   {{"items", std::move(items)}, {"gpuFrameMs", gpuFrame}}));
    }
    const double cpuFrame = FrameMean(Child(capture, "cpuPasses"), "CPU Frame");
    const std::vector<PassRow> cpuRows = PassRows(Child(capture, "cpuPasses"), true);
    if (!cpuRows.empty() && cpuRows.front().selfMeanMs > 0.0) {
        std::string detail;
        std::vector<std::string> evidence;
        Json items = Json::array();
        for (size_t i = 0; i < (std::min)(cpuRows.size(), size_t{3}); ++i) {
            const PassRow& row = cpuRows[i];
            detail += (i == 0 ? "" : "; ") + row.name + " " + Fixed(row.selfMeanMs, 2) + " ms (" + row.module + ")";
            evidence.push_back("cpu." + row.name + ".selfMs");
            items.push_back(PassRowJson(row));
        }
        findings.push_back(Finding("top.cpu", "info", "Most expensive CPU scopes (self time, waits excluded)",
                                   detail + ".", evidence, {{"items", std::move(items)}, {"cpuFrameMs", cpuFrame}}));
    }
}

void AddModuleFindings(const Json& capture, std::vector<Json>& findings) {
    const Json& modules = Child(capture, "modules");
    if (!modules.is_object() || modules.empty())
        return;
    struct Row {
        std::string module;
        double cpu;
        double gpu;
        double wait;
    };
    std::vector<Row> rows;
    double cpuInstrumented = 0.0;
    double gpuInstrumented = 0.0;
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        rows.push_back({it.key(), NumberOr(it.value(), "cpuSelfMs"), NumberOr(it.value(), "gpuSelfMs"),
                        NumberOr(it.value(), "cpuWaitMs")});
        cpuInstrumented += rows.back().cpu;
        gpuInstrumented += rows.back().gpu;
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& lhs, const Row& rhs) { return lhs.cpu + lhs.gpu > rhs.cpu + rhs.gpu; });
    std::string detail;
    std::vector<std::string> evidence;
    Json items = Json::array();
    for (size_t i = 0; i < (std::min)(rows.size(), size_t{5}); ++i) {
        detail += (i == 0 ? "" : "; ") + rows[i].module + " GPU " + Fixed(rows[i].gpu, 2) + " ms, CPU " +
                  Fixed(rows[i].cpu, 2) + " ms";
        evidence.push_back("module." + rows[i].module + ".gpuSelfMs");
        evidence.push_back("module." + rows[i].module + ".cpuSelfMs");
        items.push_back({{"module", rows[i].module},
                         {"gpuSelfMs", rows[i].gpu},
                         {"cpuSelfMs", rows[i].cpu},
                         {"cpuWaitMs", rows[i].wait}});
    }
    findings.push_back(Finding("modules.cost", "info", "Cost by module (self time per frame, waiting excluded)",
                               detail + ".", evidence, {{"items", std::move(items)}}));

    // フレームのうち、計測区間の外にある時間。大きければ、計測の足りない所がある。
    const double gpuFrame = FrameMean(Child(capture, "gpuPasses"), "Command Frame");
    const double gpuUntimed = gpuFrame - gpuInstrumented;
    if (gpuFrame > 0.0 && gpuUntimed > 0.1 * gpuFrame && gpuUntimed >= 0.5) {
        findings.push_back(Finding("gpu.untimed", "info", "GPU time outside any timed pass",
                                   Fixed(gpuUntimed, 2) + " ms of the " + Fixed(gpuFrame, 2) +
                                       " ms GPU frame is not inside a timed pass (barriers, gaps or untimed work).",
                                   {"frame.gpuMs"}, {{"untimedMs", gpuUntimed}, {"gpuFrameMs", gpuFrame}}));
    }

    const Json& cpu = Child(capture, "cpuPasses");
    double waits = 0.0;
    if (cpu.is_object()) {
        for (auto it = cpu.begin(); it != cpu.end(); ++it) {
            if (StringOr(it.value(), "category") == "wait")
                waits += NumberOr(it.value(), "meanMs");
        }
    }
    const double cpuBusy = (std::max)(0.0, FrameMean(cpu, "CPU Frame") - waits);
    const double uninstrumented = cpuBusy - cpuInstrumented;
    if (cpuBusy > 0.0 && uninstrumented > 0.2 * cpuBusy && uninstrumented >= kMsThreshold.absolute) {
        findings.push_back(Finding("cpu.uninstrumented", "info", "CPU time outside any timed scope",
                                   Fixed(uninstrumented, 2) + " ms of " + Fixed(cpuBusy, 2) +
                                       " ms of CPU work per frame is not inside a timed scope.",
                                   {"frame.cpuMs"}, {{"uninstrumentedMs", uninstrumented}, {"cpuBusyMs", cpuBusy}}));
    }
}

void AddCallFindings(const Json& capture, std::vector<Json>& findings) {
    struct Row {
        std::string domain;
        PassRow pass;
    };
    std::vector<Row> rows;
    for (const char* domain : {"cpu", "gpu"}) {
        for (PassRow& row : PassRows(Child(capture, (std::string(domain) + "Passes").c_str()), false)) {
            if (row.callsMean >= kManyCallsPerFrame)
                rows.push_back({domain, std::move(row)});
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& lhs, const Row& rhs) { return lhs.pass.callsMean > rhs.pass.callsMean; });
    for (size_t i = 0; i < (std::min)(rows.size(), size_t{5}); ++i) {
        const Row& row = rows[i];
        const double perCall = row.pass.callsMean > 0.0 ? row.pass.meanMs / row.pass.callsMean : 0.0;
        findings.push_back(
            Finding("calls." + row.domain + "." + row.pass.name, "warning", "Many calls per frame",
                    row.pass.name + " (" + row.pass.module + ") runs " + Fixed(row.pass.callsMean, 1) +
                        " times per frame on "
                        "average (up to " +
                        Fixed(row.pass.callsMax, 0) + "), " + Fixed(perCall, 3) + " ms per call.",
                    {row.domain + "." + row.pass.name + ".calls", row.domain + "." + row.pass.name + ".ms"},
                    {{"callsMean", row.pass.callsMean}, {"callsMax", row.pass.callsMax}, {"module", row.pass.module}}));
    }
}

void AddMemoryFindings(const Json& capture, std::vector<Json>& findings) {
    const Json& memory = Child(capture, "memory");
    const double usage = NumberOr(memory, "peakLocalVideoUsageBytes", -1.0);
    const double budget = NumberOr(memory, "minimumLocalVideoBudgetBytes", -1.0);
    if (usage < 0.0 || budget <= 0.0)
        return;
    const double ratio = usage / budget;
    double tracked = 0.0;
    const Json& finish = Child(capture, "metricsAtFinish");
    Json categories = Json::object();
    if (finish.is_object()) {
        for (auto it = finish.begin(); it != finish.end(); ++it) {
            const std::string& id = it.key();
            if (id.rfind("memory.vram.", 0) != 0 || id == "memory.vram.usage" || id == "memory.vram.budget" ||
                !HasNumber(it.value(), "value"))
                continue;
            categories[id.substr(std::string("memory.vram.").size())] = NumberOr(it.value(), "value");
            if (id != "memory.vram.upload")
                tracked += NumberOr(it.value(), "value");
        }
    }
    const char* severity = ratio >= kVramCriticalRatio ? "critical" : ratio >= kVramWarnRatio ? "warning" : "info";
    std::string detail = "Peak local VRAM use is " + Fixed(usage / kMiB, 0) + " MiB of a " + Fixed(budget / kMiB, 0) +
                         " MiB budget (" + Fixed(ratio * 100.0, 0) + "%).";
    if (!categories.empty() && usage > 0.0)
        detail += " The tracked categories account for " + Fixed(tracked / usage * 100.0, 0) +
                  "% of the use; the rest is created outside the central creation functions.";
    findings.push_back(Finding("memory.vram", severity, "Video memory headroom", detail,
                               {"memory.vram.usage", "memory.vram.budget"},
                               {{"peakUsageBytes", usage},
                                {"budgetBytes", budget},
                                {"usageRatio", ratio},
                                {"trackedBytes", tracked},
                                {"categories", std::move(categories)}}));
}

// 値が出ていない指標とその理由。Unsupported は理由ごとに、Stale と NoData はまとめて。
void AddMetricStatusFindings(const Json& capture, std::vector<Json>& findings) {
    const Json& finish = Child(capture, "metricsAtFinish");
    if (!finish.is_object())
        return;
    const uint64_t lastFrame = static_cast<uint64_t>(NumberOr(capture, "lastFrameId"));
    std::map<std::string, std::vector<std::string>> unsupported;
    std::vector<std::string> stale;
    std::vector<std::string> noData;
    for (auto it = finish.begin(); it != finish.end(); ++it) {
        const std::string status = StringOr(it.value(), "status");
        if (status == "unsupported")
            unsupported[StringOr(it.value(), "note", "no reason given")].push_back(it.key());
        else if (status == "stale")
            stale.push_back(it.key());
        else if (status == "noData")
            noData.push_back(it.key());
    }
    // 1 つの区間や pass は .ms / .calls / .selfMs（管線統計は .pipeline.*）の複数の指標を持つので、名前は区間ごとに 1 つにする。
    const auto listExamples = [](const std::vector<std::string>& ids) {
        std::vector<std::string> families;
        for (const std::string& id : ids) {
            std::string family = id;
            for (const char* aspect : {".selfMs", ".calls", ".ms"}) {
                const std::string_view suffix = aspect;
                if (family.size() > suffix.size() &&
                    family.compare(family.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    family.resize(family.size() - suffix.size());
                    break;
                }
            }
            if (std::find(families.begin(), families.end(), family) == families.end())
                families.push_back(std::move(family));
        }
        std::string text;
        for (size_t i = 0; i < (std::min)(families.size(), size_t{5}); ++i)
            text += (i == 0 ? "" : ", ") + families[i];
        return families.size() > 5 ? text + ", ..." : text;
    };
    const auto examples = [](const std::vector<std::string>& ids) {
        return std::vector<std::string>(ids.begin(), ids.begin() + (std::min)(ids.size(), size_t{8}));
    };
    size_t group = 0;
    for (const auto& [note, ids] : unsupported) {
        // GPU の計時が出ない場合は、GPU 側の結論が欠けるので警告にする。
        const bool gpuTiming = std::find(ids.begin(), ids.end(), "frame.gpuMs") != ids.end();
        findings.push_back(Finding("metrics.unsupported." + std::to_string(group++), gpuTiming ? "warning" : "info",
                                   gpuTiming ? "GPU timing is unsupported" : "Unsupported metrics",
                                   std::to_string(ids.size()) + " metric(s) have no value in this configuration: " +
                                       note + " (" + listExamples(ids) + ").",
                                   examples(ids), {{"reason", note}, {"count", ids.size()}}));
    }
    if (!stale.empty()) {
        findings.push_back(Finding("metrics.stale", "info", "Stale metrics",
                                   std::to_string(stale.size()) +
                                       " metric(s) were last published long before the capture ended, usually "
                                       "because their pass or scope did not run recently (" +
                                       listExamples(stale) + ").",
                                   examples(stale), {{"count", stale.size()}, {"lastFrame", lastFrame}}));
    }
    if (!noData.empty()) {
        findings.push_back(Finding("metrics.noData", "info", "Metrics without data",
                                   std::to_string(noData.size()) + " registered metric(s) never produced a value (" +
                                       listExamples(noData) + ").",
                                   examples(noData), {{"count", noData.size()}}));
    }
}

void AddCaptureQualityFindings(const Json& capture, std::vector<Json>& findings) {
    const double wanted = NumberOr(capture, "sampleFramesRequested");
    const double observed = NumberOr(capture, "sampleFramesObserved");
    if (!BoolOr(capture, "captureComplete", false)) {
        findings.push_back(
            Finding("capture.incomplete", "warning", "Capture incomplete",
                    "Only " + Fixed(observed, 0) + " of " + Fixed(wanted, 0) + " sample frames were captured.",
                    {"capture.sampleFramesObserved"}));
    }
    const double gpuObserved = NumberOr(capture, "gpuFramesObserved");
    if (observed > 0.0 && gpuObserved < observed) {
        findings.push_back(Finding("capture.gpuCoverage", "warning", "GPU timing covers part of the capture",
                                   "GPU timing arrived for " + Fixed(gpuObserved, 0) + " of " + Fixed(observed, 0) +
                                       " sampled frames; GPU numbers describe only those.",
                                   {"capture.gpuFramesObserved"}));
    }
    if (!BoolOr(capture, "configurationStable", true)) {
        findings.push_back(Finding("capture.unstable", "warning", "Configuration changed during the capture",
                                   "Render quality, resolution, device, debug options or the scene changed while "
                                   "the capture ran; the numbers mix different setups.",
                                   {"capture.configurationStable"}));
    }
    if (NumberOr(capture, "rejectedRenderSamples") > 0.0) {
        findings.push_back(Finding("capture.renderFailures", "warning", "Frames failed to render",
                                   Fixed(NumberOr(capture, "rejectedRenderSamples"), 0) +
                                       " sampled frame(s) reported a render failure.",
                                   {"capture.rejectedRenderSamples"}));
    }
}

void AddHitchFindings(const Json& capture, std::vector<Json>& findings) {
    const Json& longFrames = Child(capture, "longFrames");
    double over50 = 0.0;
    double over100 = 0.0;
    double over33 = 0.0;
    if (longFrames.is_array()) {
        for (const Json& entry : longFrames) {
            const double threshold = NumberOr(entry, "thresholdMs");
            const double count = NumberOr(entry, "count");
            if (threshold < 34.0)
                over33 = count;
            else if (threshold < 60.0)
                over50 = count;
            else if (threshold < 150.0)
                over100 = count;
        }
    }
    const Json& interval = Child(capture, "frameInterval");
    const double mean = NumberOr(interval, "meanMs");
    const double p99 = NumberOr(interval, "p99Ms");
    if (over100 > 0.0 || over50 > 0.0 || over33 > 0.0) {
        findings.push_back(Finding("frame.hitches",
                                   over100 > 0.0  ? "critical"
                                   : over50 > 0.0 ? "warning"
                                                  : "info",
                                   "Long frames",
                                   "Frames over 33 ms: " + Fixed(over33, 0) + ", over 50 ms: " + Fixed(over50, 0) +
                                       ", over 100 ms: " + Fixed(over100, 0) + " (maximum " +
                                       Fixed(NumberOr(interval, "maximumMs"), 1) + " ms).",
                                   {"capture.longFrames", "capture.frameInterval.maximumMs"}));
    } else if (mean > 0.0 && p99 >= 2.0 * mean && p99 - mean >= 3.0) {
        findings.push_back(Finding("frame.spikes", "warning", "Frame time spikes",
                                   "The 99th percentile frame time (" + Fixed(p99, 1) +
                                       " ms) is more than twice the "
                                       "mean (" +
                                       Fixed(mean, 1) + " ms).",
                                   {"capture.frameInterval.p99Ms", "capture.frameInterval.meanMs"}));
    }
}

} // namespace

std::string CaptureSchemaError(const Json& capture) {
    if (!capture.is_object())
        return "The file is not a performance capture (a JSON object was expected).";
    if (StringOr(capture, "kind") == "breakdown")
        return "The file is a performance breakdown, not a capture.";
    const Json& version = Child(capture, "schemaVersion");
    if (!version.is_number_integer() || version.get<int>() != kCaptureSchemaVersion) {
        return "Unsupported capture format (schemaVersion " +
               (version.is_null() ? std::string("missing") : version.dump()) + ", expected " +
               std::to_string(kCaptureSchemaVersion) + "). Capture again with this build.";
    }
    return {};
}

Json BuildCaptureHighlights(const Json& capture) {
    const Json& interval = Child(capture, "frameInterval");
    const Json& cpu = Child(capture, "cpuPasses");
    const Json& gpu = Child(capture, "gpuPasses");
    Json highlights = Json::object();
    highlights["complete"] = BoolOr(capture, "captureComplete", false) &&
                             BoolOr(capture, "gpuCoverageComplete", false) &&
                             BoolOr(capture, "configurationStable", false);
    highlights["frame"] = {
        {"intervalMeanMs", Child(interval, "meanMs")},   {"intervalP95Ms", Child(interval, "p95Ms")},
        {"intervalMaxMs", Child(interval, "maximumMs")}, {"renderedFps", Child(capture, "renderedFps")},
        {"cpuFrameMs", FrameMean(cpu, "CPU Frame")},     {"gpuFrameMs", FrameMean(gpu, "Command Frame")}};
    const auto top = [](const Json& passes, bool workOnly) {
        Json items = Json::array();
        const std::vector<PassRow> rows = PassRows(passes, workOnly);
        for (size_t i = 0; i < (std::min)(rows.size(), size_t{5}); ++i)
            items.push_back(PassRowJson(rows[i]));
        return items;
    };
    highlights["gpuTop"] = top(gpu, false);
    highlights["cpuTop"] = top(cpu, true);

    Json modules = Json::array();
    const Json& moduleSummary = Child(capture, "modules");
    if (moduleSummary.is_object()) {
        std::vector<Json> rows;
        for (auto it = moduleSummary.begin(); it != moduleSummary.end(); ++it)
            rows.push_back({{"module", it.key()},
                            {"gpuSelfMs", NumberOr(it.value(), "gpuSelfMs")},
                            {"cpuSelfMs", NumberOr(it.value(), "cpuSelfMs")}});
        std::stable_sort(rows.begin(), rows.end(), [](const Json& lhs, const Json& rhs) {
            return NumberOr(lhs, "gpuSelfMs") + NumberOr(lhs, "cpuSelfMs") >
                   NumberOr(rhs, "gpuSelfMs") + NumberOr(rhs, "cpuSelfMs");
        });
        for (size_t i = 0; i < (std::min)(rows.size(), size_t{5}); ++i)
            modules.push_back(rows[i]);
    }
    highlights["modules"] = std::move(modules);

    const Json& memory = Child(capture, "memory");
    Json vram = {{"peakUsageBytes", Child(memory, "peakLocalVideoUsageBytes")},
                 {"minimumBudgetBytes", Child(memory, "minimumLocalVideoBudgetBytes")}};
    const double usage = NumberOr(memory, "peakLocalVideoUsageBytes", -1.0);
    const double budget = NumberOr(memory, "minimumLocalVideoBudgetBytes", -1.0);
    vram["usageRatio"] = usage >= 0.0 && budget > 0.0 ? Json(usage / budget) : Json(nullptr);
    Json categories = Json::object();
    const Json& finish = Child(capture, "metricsAtFinish");
    if (finish.is_object()) {
        for (auto it = finish.begin(); it != finish.end(); ++it) {
            const std::string& id = it.key();
            if (id.rfind("memory.vram.", 0) == 0 && id != "memory.vram.usage" && id != "memory.vram.budget" &&
                HasNumber(it.value(), "value"))
                categories[id.substr(std::string("memory.vram.").size())] = NumberOr(it.value(), "value");
        }
    }
    vram["categories"] = std::move(categories);
    highlights["vram"] = std::move(vram);
    return highlights;
}

Json CompareCaptures(const Json& a, const Json& b, size_t maxEntries) {
    const MetricMap left = ExtractMetrics(a);
    const MetricMap right = ExtractMetrics(b);

    struct Entry {
        Json json;
        int rank = 0;
        double impact = 0.0;
    };
    std::vector<Entry> entries;
    size_t regressions = 0, improvements = 0, unchanged = 0, added = 0, removed = 0;

    const auto describeOnly = [&entries](const std::string& id, const MetricValue& metric, const char* verdict,
                                         bool inA) {
        Entry entry;
        entry.json = {{"id", id},
                      {"unit", metric.unit},
                      {"module", metric.module},
                      {"a", inA ? Json(metric.value) : Json(nullptr)},
                      {"b", inA ? Json(nullptr) : Json(metric.value)},
                      {"verdict", verdict}};
        entry.rank = inA ? 3 : 2;
        entries.push_back(std::move(entry));
    };
    for (const auto& [id, before] : left) {
        const auto found = right.find(id);
        if (found == right.end()) {
            ++removed;
            describeOnly(id, before, "removed", true);
            continue;
        }
        const MetricValue& after = found->second;
        const Threshold threshold = ThresholdFor(before.unit);
        const double delta = after.value - before.value;
        const bool hasPercent = before.value != 0.0;
        const double percent = hasPercent ? delta / std::abs(before.value) * 100.0 : 0.0;
        const bool significant =
            std::abs(delta) >= threshold.absolute && (!hasPercent || std::abs(percent) >= threshold.percent);
        if (!significant) {
            ++unchanged;
            continue;
        }
        const bool worse = before.higherIsBetter ? delta < 0.0 : delta > 0.0;
        (worse ? regressions : improvements) += 1;
        Entry entry;
        entry.json = {{"id", id},
                      {"unit", before.unit},
                      {"module", before.module},
                      {"a", before.value},
                      {"b", after.value},
                      {"delta", delta},
                      {"percent", hasPercent ? Json(percent) : Json(nullptr)},
                      {"verdict", worse ? "regression" : "improvement"}};
        entry.rank = worse ? 0 : 1;
        entry.impact = std::abs(delta) / threshold.absolute;
        entries.push_back(std::move(entry));
    }
    for (const auto& [id, after] : right) {
        if (left.find(id) == left.end()) {
            ++added;
            describeOnly(id, after, "added", false);
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
        return lhs.rank != rhs.rank ? lhs.rank < rhs.rank : lhs.impact > rhs.impact;
    });

    Json shown = Json::array();
    for (size_t i = 0; i < (std::min)(entries.size(), maxEntries); ++i)
        shown.push_back(std::move(entries[i].json));
    Json result = Json::object();
    result["comparable"] = DescribeComparability(a, b);
    result["thresholds"] = ThresholdsJson();
    result["summary"] = {{"regressions", regressions},
                         {"improvements", improvements},
                         {"unchanged", unchanged},
                         {"added", added},
                         {"removed", removed},
                         {"shown", shown.size()},
                         {"omitted", entries.size() - shown.size()}};
    result["metrics"] = std::move(shown);
    return result;
}

Json BuildFindings(const Json& capture) {
    std::vector<Json> findings;
    std::string bound;
    AddCaptureQualityFindings(capture, findings);
    AddBoundFinding(capture, findings, bound);
    AddTopFindings(capture, findings);
    AddModuleFindings(capture, findings);
    AddCallFindings(capture, findings);
    AddMemoryFindings(capture, findings);
    AddHitchFindings(capture, findings);
    AddMetricStatusFindings(capture, findings);
    return FindingsResult(std::move(findings), bound);
}

} // namespace HIKARI::DIAGNOSTICS
