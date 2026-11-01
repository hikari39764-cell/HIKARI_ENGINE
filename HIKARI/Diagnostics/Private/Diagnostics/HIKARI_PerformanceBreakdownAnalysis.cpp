#include "Diagnostics/HIKARI_PerformanceBreakdownAnalysis.h"
#include "Diagnostics/Internal/HIKARI_AnalysisJson.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace HIKARI::DIAGNOSTICS {
namespace {
using Json = nlohmann::json;
using namespace ANALYSIS;

// どの開関にも入らない分がこの割合を超えたら、分解がまだ粗いと警告する。
constexpr double kUncoveredWarnShare = 0.15;
// 項目を 1 つずつ省いた節約の和と一緒に省いた節約の差が、完全な描画のこの割合を超えたら発見に書く。
constexpr double kOverlapReportShare = 0.05;
// 発見に項目ごとに書く最小の占比と件数の上限。
constexpr double kMinimumReportedShare = 0.03;
constexpr size_t kMaximumReportedSteps = 6;
// 変種の前後の基準の差がこれを超えた測定は、その間に GPU の状態か場面が変わったとみなして数えない
// （雑音の 4 倍と基準の 2% の大きい方）。
constexpr double kDriftNoiseFactor = 4.0;
constexpr double kDriftShare = 0.02;

struct Segment {
    size_t order = 0;
    std::string config;
    std::string status;
    bool isBaseline = false;
    bool ok = false;
    double target = 0.0;
    double frame = 0.0;
};

struct Reference {
    double target = 0.0;
    double frame = 0.0;
    // 続けて測った同じ設定の区間どうしの差の中央値（一度きりの段差に引きずられない）。
    // 区間が 1 つしか無ければ負（無し）。
    double noise = -1.0;
    size_t count = 0;
};

// 成功した基準の区間を、基準の id ごとに実行順で持つ。
using BaselineSeries = std::map<std::string, std::vector<const Segment*>>;

double Mean(const std::vector<double>& values) {
    double sum = 0.0;
    for (const double value : values)
        sum += value;
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double Median(std::vector<double> values) {
    if (values.empty())
        return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if (values.size() % 2 == 1)
        return upper;
    return (*std::max_element(values.begin(), values.begin() + middle) + upper) / 2.0;
}

std::vector<Segment> ParseSegments(const Json& breakdown) {
    std::vector<Segment> segments;
    const Json& list = Child(breakdown, "segments");
    if (!list.is_array())
        return segments;
    for (const Json& item : list) {
        Segment segment;
        segment.order = static_cast<size_t>(NumberOr(item, "index", static_cast<double>(segments.size())));
        segment.config = StringOr(item, "config");
        segment.status = StringOr(item, "status");
        segment.isBaseline = StringOr(item, "kind") == "baseline";
        segment.ok = segment.status == "ok" && HasNumber(item, "targetMs");
        segment.target = NumberOr(item, "targetMs");
        segment.frame = NumberOr(item, "frameMs");
        segments.push_back(std::move(segment));
    }
    std::stable_sort(segments.begin(), segments.end(),
                     [](const Segment& lhs, const Segment& rhs) { return lhs.order < rhs.order; });
    return segments;
}

std::map<std::string, Reference> BuildReferences(const BaselineSeries& series) {
    std::map<std::string, Reference> references;
    for (const auto& [id, list] : series) {
        Reference reference;
        reference.count = list.size();
        std::vector<double> differences;
        for (size_t index = 0; index < list.size(); ++index) {
            reference.target += list[index]->target;
            reference.frame += list[index]->frame;
            if (index > 0)
                differences.push_back(std::abs(list[index]->target - list[index - 1]->target));
        }
        reference.target /= static_cast<double>(list.size());
        reference.frame /= static_cast<double>(list.size());
        if (!differences.empty())
            reference.noise = Median(std::move(differences));
        references[id] = reference;
    }
    return references;
}

// 変種 1 つ分の解析。前後の基準の平均との差が費用になる（前後のどちらかしか無ければそれとの差）。
// 前後の基準が漂移の限度を超えて食い違う測定は、費用にも底色・基礎の耗時にも数えない。
Json AnalyzeStep(const Json& step, const std::vector<Segment>& segments, const BaselineSeries& series,
                 const std::map<std::string, Reference>& references) {
    const std::string id = StringOr(step, "id");
    const std::string role = StringOr(step, "role");
    const std::string baseline = StringOr(step, "baseline");
    Json entry = {
        {"id", id}, {"label", StringOr(step, "label")}, {"role", role}, {"baseline", baseline}, {"counted", false}};
    if (StringOr(step, "status") == "unchanged") {
        entry["status"] = "unchanged";
        return entry;
    }

    const auto baselineSeries = series.find(baseline);
    const auto reference = references.find(baseline);
    const double noise = reference != references.end() ? reference->second.noise : -1.0;
    const double driftLimit = reference != references.end() ? (std::max)(kDriftNoiseFactor * (std::max)(noise, 0.0),
                                                                         kDriftShare * reference->second.target)
                                                            : std::numeric_limits<double>::infinity();
    std::vector<double> deltas, frameDeltas, absolute;
    bool notApplied = false;
    size_t drifted = 0;
    for (const Segment& segment : segments) {
        if (segment.isBaseline || segment.config != id)
            continue;
        notApplied |= segment.status == "notApplied";
        if (!segment.ok)
            continue;
        const Segment* before = nullptr;
        const Segment* after = nullptr;
        if (baselineSeries != series.end()) {
            for (const Segment* candidate : baselineSeries->second) {
                if (candidate->order < segment.order)
                    before = candidate;
                else if (candidate->order > segment.order) {
                    after = candidate;
                    break;
                }
            }
        }
        if (before != nullptr && after != nullptr && std::abs(before->target - after->target) > driftLimit) {
            ++drifted;
            continue;
        }
        absolute.push_back(segment.target);
        if (before == nullptr && after == nullptr)
            continue;
        const double referenceTarget =
            before && after ? (before->target + after->target) / 2.0 : (before ? before : after)->target;
        const double referenceFrame =
            before && after ? (before->frame + after->frame) / 2.0 : (before ? before : after)->frame;
        deltas.push_back(referenceTarget - segment.target);
        frameDeltas.push_back(referenceFrame - segment.frame);
    }
    if (drifted > 0)
        entry["driftedRounds"] = drifted;
    if ((role == "floor" || role == "base") && !absolute.empty())
        entry[role == "floor" ? "floorMs" : "baseMs"] = Mean(absolute);
    if (deltas.empty()) {
        entry["status"] = notApplied ? "notApplied" : (drifted > 0 ? "drift" : "incomplete");
        return entry;
    }

    const double delta = Mean(deltas);
    entry["deltaMs"] = delta;
    entry["frameDeltaMs"] = Mean(frameDeltas);
    entry["rounds"] = deltas;
    entry["spreadMs"] =
        *std::max_element(deltas.begin(), deltas.end()) - *std::min_element(deltas.begin(), deltas.end());
    if (reference != references.end()) {
        entry["baselineMs"] = reference->second.target;
        entry["share"] = reference->second.target > 0.0 ? Json(delta / reference->second.target) : Json(nullptr);
    }
    if (noise >= 0.0)
        entry["noiseMs"] = noise;
    // 変種が基準より遅い測定は、費用として数えない。
    const char* status = delta < 0.0 ? "unreliable" : (noise >= 0.0 && delta <= noise ? "belowNoise" : "ok");
    entry["status"] = status;
    entry["counted"] = delta >= 0.0;
    return entry;
}

std::string Millis(double value) { return Fixed(value, 2) + " ms"; }

std::string Percent(double share, int decimals = 0) { return Fixed(share * 100.0, decimals) + "%"; }

} // namespace

bool IsBreakdown(const Json& file) { return file.is_object() && StringOr(file, "kind") == "breakdown"; }

std::string BreakdownSchemaError(const Json& breakdown) {
    if (!IsBreakdown(breakdown))
        return "The file is not a performance breakdown.";
    const Json& version = Child(breakdown, "schemaVersion");
    if (!version.is_number_integer() || version.get<int>() != kBreakdownSchemaVersion) {
        return "Unsupported breakdown format (schemaVersion " +
               (version.is_null() ? std::string("missing") : version.dump()) + ", expected " +
               std::to_string(kBreakdownSchemaVersion) + "). Run the breakdown again with this build.";
    }
    return {};
}

Json AnalyzeBreakdown(const Json& breakdown) {
    const std::vector<Segment> segments = ParseSegments(breakdown);
    BaselineSeries series;
    for (const Segment& segment : segments) {
        if (segment.isBaseline && segment.ok)
            series[segment.config].push_back(&segment);
    }
    const std::map<std::string, Reference> references = BuildReferences(series);

    const Json& stepList = Child(breakdown, "steps");
    Json steps = Json::array();
    Json referencesJson = Json::object();
    if (stepList.is_array()) {
        for (const Json& step : stepList) {
            const std::string id = StringOr(step, "id");
            if (StringOr(step, "role") != "reference") {
                steps.push_back(AnalyzeStep(step, segments, series, references));
                continue;
            }
            Json reference = {{"label", StringOr(step, "label")}, {"status", "incomplete"}};
            if (const auto found = references.find(id); found != references.end()) {
                reference["status"] = "ok";
                reference["targetMs"] = found->second.target;
                reference["frameMs"] = found->second.frame;
                reference["segments"] = found->second.count;
                if (found->second.noise >= 0.0)
                    reference["noiseMs"] = found->second.noise;
            }
            referencesJson[id] = std::move(reference);
        }
    }

    Json result = Json::object();
    result["targetPass"] = StringOr(breakdown, "targetPass");
    result["references"] = std::move(referencesJson);

    // 完全な描画 = 底色だけの描画（floor）+ 基礎の着色（base = 項目を全部省いた描画 − 底色。どの開関でも消えない）
    // + 項目を一緒に省いた節約（joint）。項目を 1 つずつ省いた節約の和（parts）は joint と一致するとは限らない：和が
    // 大きければ節約が重なり（GPU の占有率やメモリ待ちを共有する）、小さければ全部を省いた時だけ消える共有の仕事が
    // ある（differenceMs = parts − joint）。基礎の着色が測れていない時は、完全な描画 − 底色 − parts の正の分を
    // どの開関にも入らない分（uncovered）、負の分を重なりとする。
    Json decomposition(nullptr);
    const std::string totalId = StringOr(breakdown, "totalReference");
    const auto total = references.find(totalId);
    if (total != references.end() && total->second.target > 0.0) {
        const Json* floorStep = nullptr;
        const Json* baseStep = nullptr;
        double parts = 0.0;
        for (const Json& step : steps) {
            if (StringOr(step, "baseline") != totalId)
                continue;
            if (StringOr(step, "role") == "floor" && HasNumber(step, "floorMs"))
                floorStep = &step;
            else if (StringOr(step, "role") == "base" && HasNumber(step, "baseMs"))
                baseStep = &step;
            else if (StringOr(step, "role") == "part" && BoolOr(step, "counted", false))
                parts += NumberOr(step, "deltaMs");
        }
        if (floorStep != nullptr) {
            const double totalMs = total->second.target;
            const double floor = NumberOr(*floorStep, "floorMs");
            double uncovered = totalMs - floor - parts;
            decomposition = {{"totalMs", totalMs}, {"floorMs", floor}, {"partsMs", parts}};
            if (baseStep != nullptr) {
                // 基礎の着色は底色の下には入らない（測定の揺れで基礎が底色を下回っても 0 にそろえる）。
                const double baseShading = (std::max)(NumberOr(*baseStep, "baseMs") - floor, 0.0);
                const double joint = totalMs - floor - baseShading;
                decomposition["baseShadingMs"] = baseShading;
                decomposition["jointMs"] = joint;
                decomposition["differenceMs"] = parts - joint;
                uncovered = baseShading;
            } else if (uncovered < 0.0) {
                decomposition["differenceMs"] = -uncovered;
                uncovered = 0.0;
            }
            decomposition["uncoveredMs"] = uncovered;
            decomposition["uncoveredShare"] = uncovered / totalMs;
        }
    }
    result["decomposition"] = std::move(decomposition);

    // 通常の描画 − 何も省かない汎用シェーダーの描画。通常の描画だけが SSR と次表面散乱の capture 出力を書くので、
    // それを使う場面ではほぼその出力の費用（専用シェーダーとの差も含む）。
    const auto normal = references.find(StringOr(breakdown, "productionReference"));
    if (normal != references.end() && total != references.end() && normal->second.target > 0.0) {
        const double extra = normal->second.target - total->second.target;
        result["normalOverGeneral"] = {{"ms", extra}, {"share", extra / normal->second.target}};
    }
    result["steps"] = std::move(steps);
    return result;
}

namespace {
const Json& AnalysisOf(const Json& breakdown, Json& computed) {
    const Json& stored = Child(breakdown, "analysis");
    if (stored.is_object())
        return stored;
    computed = AnalyzeBreakdown(breakdown);
    return computed;
}

std::string Pad(std::string text, size_t width) {
    if (text.size() < width)
        text.resize(width, ' ');
    return text;
}

std::string Right(const std::string& text, size_t width) {
    return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string Number(const Json& entry, const char* key, int decimals) {
    return HasNumber(entry, key) ? Fixed(NumberOr(entry, key), decimals) : "-";
}
} // namespace

Json BuildBreakdownFindings(const Json& breakdown) {
    Json computed;
    const Json& analysis = AnalysisOf(breakdown, computed);
    const std::string target = StringOr(analysis, "targetPass", "the target pass");
    const Json& steps = Child(analysis, "steps");
    std::vector<Json> findings;

    if (!BoolOr(breakdown, "complete", true)) {
        findings.push_back(Finding(
            "breakdown.incomplete", "warning", "Breakdown incomplete",
            "The breakdown stopped before every segment was measured" +
                (StringOr(breakdown, "message").empty() ? std::string() : ": " + StringOr(breakdown, "message")) +
                ". Missing segments are reported as incomplete.",
            {"breakdown.complete"}));
    }

    std::vector<const Json*> parts;
    for (const Json& step : steps) {
        if (StringOr(step, "role") == "part" && BoolOr(step, "counted", false))
            parts.push_back(&step);
    }
    std::stable_sort(parts.begin(), parts.end(), [](const Json* lhs, const Json* rhs) {
        return NumberOr(*lhs, "deltaMs") > NumberOr(*rhs, "deltaMs");
    });
    const Json& decomposition = Child(analysis, "decomposition");
    if (!parts.empty() && decomposition.is_object()) {
        std::string detail;
        Json items = Json::array();
        std::vector<std::string> evidence;
        for (size_t index = 0; index < (std::min)(parts.size(), size_t{4}); ++index) {
            const Json& step = *parts[index];
            detail += (index == 0 ? "" : "; ") + StringOr(step, "label") + " " + Millis(NumberOr(step, "deltaMs")) +
                      " (" + Percent(NumberOr(step, "share")) + ")";
            evidence.push_back("breakdown.steps." + StringOr(step, "id") + ".deltaMs");
            items.push_back({{"id", StringOr(step, "id")},
                             {"deltaMs", NumberOr(step, "deltaMs")},
                             {"share", NumberOr(step, "share")}});
        }
        findings.push_back(Finding("breakdown.summary", "info", target + " cost by feature",
                                   target + " takes " + Millis(NumberOr(decomposition, "totalMs")) +
                                       " with every feature on; the largest: " + detail + ".",
                                   evidence, {{"items", std::move(items)}, {"decomposition", decomposition}}));
    }
    size_t reported = 0;
    for (const Json* step : parts) {
        const double share = NumberOr(*step, "share");
        if (share < kMinimumReportedShare || reported >= kMaximumReportedSteps)
            continue;
        ++reported;
        const std::string id = StringOr(*step, "id");
        findings.push_back(Finding(
            "breakdown.step." + id, "info",
            StringOr(*step, "label") + " costs " + Millis(NumberOr(*step, "deltaMs")) + " in " + target + " (" +
                Percent(share) + ")",
            "Turning it off saves " + Millis(NumberOr(*step, "deltaMs")) + " of " + target + " and " +
                Millis(NumberOr(*step, "frameDeltaMs")) + " of the GPU frame (noise " + Number(*step, "noiseMs", 2) +
                " ms over " + std::to_string(Child(*step, "rounds").size()) + " measurements).",
            {"breakdown.steps." + id + ".deltaMs"},
            {{"deltaMs", NumberOr(*step, "deltaMs")}, {"share", share}, {"status", StringOr(*step, "status")}}));
    }

    if (decomposition.is_object()) {
        const double share = NumberOr(decomposition, "uncoveredShare");
        const bool coarse = share > kUncoveredWarnShare;
        const bool hasBase = HasNumber(decomposition, "baseShadingMs");
        const std::string floor = Millis(NumberOr(decomposition, "floorMs"));
        findings.push_back(Finding(
            "breakdown.uncovered", coarse ? "warning" : "info",
            std::string(coarse ? "Part of " : "Little of ") + target + " is outside every switch",
            Millis(NumberOr(decomposition, "uncoveredMs")) + " (" + Percent(share) + ") of " + target +
                (hasBase ? " is base shading: with every optional feature off it still costs that much above the "
                           "base-colour floor (" +
                               floor + "). Reducing it needs a change to the shader itself."
                         : " is not accounted for by the base-colour floor (" + floor + ") or any measured feature (" +
                               Millis(NumberOr(decomposition, "partsMs")) + ").") +
                (coarse ? " A switch for part of it would make the breakdown finer." : ""),
            {"breakdown.decomposition.uncoveredMs"},
            {{"uncoveredMs", NumberOr(decomposition, "uncoveredMs")}, {"share", share}}));
        const double difference = NumberOr(decomposition, "differenceMs");
        if (std::abs(difference) > kOverlapReportShare * NumberOr(decomposition, "totalMs")) {
            const std::string sum = Millis(NumberOr(decomposition, "partsMs"));
            const std::string together =
                Millis(hasBase ? NumberOr(decomposition, "jointMs")
                               : NumberOr(decomposition, "totalMs") - NumberOr(decomposition, "floorMs"));
            findings.push_back(Finding(
                "breakdown.overlap", "info",
                difference > 0.0 ? "Feature savings overlap by " + Millis(difference)
                                 : Millis(-difference) + " is only saved with every feature off",
                difference > 0.0
                    ? "Turning the features off one at a time saves " + sum + " in total, " + Millis(difference) +
                          " more than " +
                          (hasBase ? "turning them all off together" : "the pass costs above the base-colour floor") +
                          " (" + together +
                          "): they share a bottleneck such as occupancy or memory latency, so their savings do not "
                          "add up. Each feature's cost is still what removing it alone would save."
                    : "Turning every optional feature off together saves " + together + ", " + Millis(-difference) +
                          " more than the features one at a time (" + sum +
                          "): work shared by the features goes only when all of them are off.",
                {"breakdown.decomposition.differenceMs"}, {{"differenceMs", difference}}));
        }
    }
    const Json& extra = Child(analysis, "normalOverGeneral");
    if (extra.is_object()) {
        findings.push_back(Finding(
            "breakdown.normalOverGeneral", "info",
            "The normal " + target + " costs " + Millis(NumberOr(extra, "ms")) + " more than the general shader",
            "The normal pass is " + Millis(NumberOr(extra, "ms")) + " (" + Percent(NumberOr(extra, "share")) +
                ") above the one-shader-for-everything baseline the features are measured against. Only the normal "
                "pass writes the SSR and subsurface capture outputs, so in scenes that use them this is mostly their "
                "cost (it also includes the specialised shaders' difference).",
            {"breakdown.normalOverGeneral.ms"}, extra));
    }
    for (const Json& step : steps) {
        const std::string status = StringOr(step, "status");
        const std::string id = StringOr(step, "id");
        const std::string label = StringOr(step, "label");
        if (status == "notApplied") {
            findings.push_back(Finding("breakdown.notApplied." + id, "warning", label + " did not take effect",
                                       "The setting for this step never became active (the render quality did not "
                                       "apply, or the forward pass reported a different mode), so it was not measured.",
                                       {"breakdown.steps." + id + ".status"}));
        } else if (status == "unreliable") {
            findings.push_back(Finding("breakdown.unreliable." + id, "info",
                                       label + " measured slower than its baseline",
                                       "Switching it off made " + target + " " + Millis(-NumberOr(step, "deltaMs")) +
                                           " slower, so the value is noise or a shader side effect and is not counted.",
                                       {"breakdown.steps." + id + ".deltaMs"}));
        } else if (StringOr(step, "role") == "system" && (status == "ok" || status == "belowNoise")) {
            findings.push_back(
                Finding("breakdown.system." + id, "info",
                        label + " changes the frame by " + Millis(NumberOr(step, "frameDeltaMs")),
                        "Applying it changes " + target + " by " + Millis(NumberOr(step, "deltaMs")) + " (" +
                            Percent(NumberOr(step, "share")) + ") and the whole GPU frame by " +
                            Millis(NumberOr(step, "frameDeltaMs")) + ".",
                        {"breakdown.steps." + id + ".deltaMs", "breakdown.steps." + id + ".frameDeltaMs"}));
        }
        if (HasNumber(step, "driftedRounds")) {
            findings.push_back(Finding(
                "breakdown.drift." + id, "warning", label + " had measurements dropped for drift",
                "The baselines before and after " + Fixed(NumberOr(step, "driftedRounds"), 0) +
                    " of its measurements differ by more than the drift limit (the GPU clocks or the scene changed "
                    "during the run), so those were not counted" +
                    (status == "drift" ? " and nothing was left to report" : "") +
                    ". Run the breakdown again on a settled scene.",
                {"breakdown.steps." + id + ".driftedRounds"}));
        }
    }
    return FindingsResult(std::move(findings), "");
}

std::vector<std::string> FormatBreakdownTable(const Json& breakdown) {
    Json computed;
    const Json& analysis = AnalysisOf(breakdown, computed);
    const Json& steps = Child(analysis, "steps");
    const Json& references = Child(analysis, "references");
    // 左が名前、右が数の列。名前の列は一番長い名前に幅を合わせる。
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("Reference", Right("pass ms", 9) + Right("frame ms", 10) + Right("noise", 8));
    if (references.is_object()) {
        for (auto it = references.begin(); it != references.end(); ++it) {
            rows.emplace_back("  " + StringOr(it.value(), "label"), Right(Number(it.value(), "targetMs", 2), 9) +
                                                                        Right(Number(it.value(), "frameMs", 2), 10) +
                                                                        Right(Number(it.value(), "noiseMs", 2), 8));
        }
    }
    rows.emplace_back("Feature (cost = baseline - feature off)",
                      Right("cost ms", 9) + Right("share", 8) + Right("noise", 8) + Right("frame ms", 10) + "  status");
    for (const Json& step : steps) {
        const std::string role = StringOr(step, "role");
        const bool info = role == "info";
        // 底色と基礎は省いた描画そのものの耗時（基準に対する割合）、他は基準との差。
        const char* absoluteKey = role == "floor" ? "floorMs" : (role == "base" ? "baseMs" : nullptr);
        std::string cost = "-", share = "-", frame = Number(step, "frameDeltaMs", 2);
        if (absoluteKey != nullptr && HasNumber(step, absoluteKey)) {
            const double absolute = NumberOr(step, absoluteKey);
            cost = Fixed(absolute, 2);
            if (NumberOr(step, "baselineMs") > 0.0)
                share = Percent(absolute / NumberOr(step, "baselineMs"), 1);
            frame = "-";
        } else if (HasNumber(step, "deltaMs")) {
            cost = Fixed(NumberOr(step, "deltaMs"), 2);
            if (HasNumber(step, "share"))
                share = Percent(NumberOr(step, "share"), 1);
        }
        std::string label = std::string(info ? "    " : "  ") + StringOr(step, "label");
        if (absoluteKey != nullptr)
            label += " [absolute]";
        else if (info)
            label += " [not summed]";
        rows.emplace_back(std::move(label), Right(cost, 9) + Right(share, 8) + Right(Number(step, "noiseMs", 2), 8) +
                                                Right(frame, 10) + "  " + StringOr(step, "status"));
    }
    const Json& decomposition = Child(analysis, "decomposition");
    if (decomposition.is_object()) {
        // 割合は何も省かない汎用シェーダーの描画（完全な描画）に対して。
        const double total = NumberOr(decomposition, "totalMs");
        const auto addRow = [&](const char* label, const char* key, bool withShare, const char* note) {
            if (!HasNumber(decomposition, key))
                return;
            const double value = NumberOr(decomposition, key);
            rows.emplace_back(label, Right(Fixed(value, 2), 9) +
                                         (withShare && total > 0.0 ? Right(Percent(value / total, 1), 8) : "") + note);
        };
        const char* coarse = NumberOr(decomposition, "uncoveredShare") > kUncoveredWarnShare ? "  (beyond 15%)" : "";
        addRow("Base-colour floor", "floorMs", true, "");
        addRow("Base shading (no switch removes it)", "baseShadingMs", true, coarse);
        addRow("Features off together", "jointMs", true, "");
        addRow("Features off one at a time (sum)", "partsMs", false, "");
        addRow("  sum - together (positive: the savings overlap)", "differenceMs", false, "");
        if (!HasNumber(decomposition, "baseShadingMs"))
            addRow("Not covered by any switch", "uncoveredMs", true, coarse);
    }
    const Json& extra = Child(analysis, "normalOverGeneral");
    if (extra.is_object()) {
        rows.emplace_back("Normal - general (SSR/subsurface capture outputs)",
                          Right(Fixed(NumberOr(extra, "ms"), 2), 9) + Right(Percent(NumberOr(extra, "share"), 1), 8));
    }

    size_t width = 0;
    for (const auto& row : rows)
        width = (std::max)(width, row.first.size());
    std::vector<std::string> lines;
    lines.push_back("Performance breakdown of " + StringOr(analysis, "targetPass") + " (preset " +
                    StringOr(breakdown, "preset") + ", " + Fixed(NumberOr(breakdown, "rounds"), 0) + " rounds x " +
                    Fixed(NumberOr(breakdown, "sampleFrames"), 0) + " frames per segment, settled after " +
                    Fixed(NumberOr(breakdown, "settleWindows"), 0) + " windows" +
                    (BoolOr(breakdown, "complete", true) ? ")" : ", INCOMPLETE)"));
    for (auto& [label, values] : rows)
        lines.push_back(Pad(std::move(label), width + 2) + values);
    return lines;
}

} // namespace HIKARI::DIAGNOSTICS
