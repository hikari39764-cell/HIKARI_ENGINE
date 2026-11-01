#pragma once

#include <string>
#include <vector>

#include <json.hpp>

namespace HIKARI::DIAGNOSTICS {

// 性能の分解（PerformanceBreakdown が測って書く JSON）の解析。入力の JSON だけを見る：ファイルにもエンジンの状態にも
// 触れないので、一回限りの小さな道具からも呼べる。入力の形：
//   preset、targetPass（対象の GPU pass 名）、totalReference、complete、rounds、warmupFrames、sampleFrames、
//   settleWindows
//   steps:    [{id, label, role (reference|floor|base|part|info|system), baseline, status}]
//   segments: [{index, round, config, baseline, kind (baseline|variant), status, targetMs, frameMs}]
// 基準（reference）は何も省かない描画、変種（steps の他）は 1 項目を省いた描画。基準と変種を交互に測るので、変種の
// 費用は前後の基準の平均との差で求める（ゆっくりした揺れが相殺される）。
inline constexpr int kBreakdownSchemaVersion = 1;

// 分解の JSON か（捕獲ではなく）。
bool IsBreakdown(const nlohmann::json& file);
// 現在の形式の分解か。違えば理由を返す（空なら正常）。
std::string BreakdownSchemaError(const nlohmann::json& breakdown);

// 項目ごとの費用・占比・雑音・状態と、基準・分解・通常の描画との差。雑音は同じ基準を続けて測った区間どうしの
// 差の中央値。
//   steps[].status: ok、belowNoise（雑音以下だが数える）、unreliable（変種が基準より遅く、費用として数えない）、
//                   drift（前後の基準が食い違う測定しか無い。食い違って捨てた回数は driftedRounds）、
//                   notApplied（設定が効かず測れない）、unchanged（設定が今のままで測る意味が無い）、incomplete
//   decomposition: 完全な描画 = 底色だけ（floorMs）+ 基礎の着色（baseShadingMs = 項目を全部省いた描画 − 底色）
//                  + 項目を一緒に省いた節約（jointMs）。partsMs は項目を 1 つずつ省いた節約の和で、differenceMs =
//                  parts − joint（正なら節約が重なる）。uncoveredMs / uncoveredShare はどの開関にも入らない分
//                  （基礎が測れていなければ 完全な描画 − 底色 − parts の正の分）
//   normalOverGeneral: 通常の描画 − 汎用シェーダーの描画（SSR と次表面散乱の capture 出力の費用を含む）
nlohmann::json AnalyzeBreakdown(const nlohmann::json& breakdown);

// 規則による発見（perf.findings と同じ形）。各項目の費用、開関の外の分、節約の重なり、通常の描画との差、
// 効かなかった項目、漂移で捨てた測定。
nlohmann::json BuildBreakdownFindings(const nlohmann::json& breakdown);

// コンソールに出す表。1 行ずつ。
std::vector<std::string> FormatBreakdownTable(const nlohmann::json& breakdown);

} // namespace HIKARI::DIAGNOSTICS
