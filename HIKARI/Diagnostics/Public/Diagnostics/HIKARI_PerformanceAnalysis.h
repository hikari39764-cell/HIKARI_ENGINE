#pragma once

#include <cstddef>
#include <string>
#include <json.hpp>

namespace HIKARI::DIAGNOSTICS {

// 性能捕獲の JSON 摘要（BenchmarkRun::BuildReport と PerformanceCapture の追記）を読む関数。
// 入力の JSON だけを見る：ファイルにもエンジンの状態にも触れないので、一回限りの小さな道具からも呼べる。
// 現在の形式（schemaVersion）だけを読み、古い形式は読まない。
inline constexpr int kCaptureSchemaVersion = 2;

// 現在の形式の捕獲か。違えば理由を返す（空なら正常）。
std::string CaptureSchemaError(const nlohmann::json& capture);

// 要点：フレーム時間、いちばん重い区間、モジュール別、VRAM。
nlohmann::json BuildCaptureHighlights(const nlohmann::json& capture);

// 2 つの捕獲を指標ごとに比べる。耗時のような量は、差の絶対値と比率の両方が閾値を超えた時だけ回帰・改善とする
// （小さい値の揺れを拾わない）。同じ構成・場面・画質かも書く。maxEntries は返す指標の上限（変化の大きい順）。
nlohmann::json CompareCaptures(const nlohmann::json& a, const nlohmann::json& b, size_t maxEntries);

// 規則による発見：CPU か GPU が足りないか、重い区間とそのモジュール、呼び出しが多い区間、VRAM の余裕、
// 値が出ていない指標とその理由。どれも根拠の指標 id を持つ。
nlohmann::json BuildFindings(const nlohmann::json& capture);

} // namespace HIKARI::DIAGNOSTICS
