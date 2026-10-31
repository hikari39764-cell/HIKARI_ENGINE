#pragma once

#include <string>

#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::EDITOR {

// 登録表の指標を画面用の文字列にする。値のない状態は 0 ではなく状態名（データなし・非対応）で示し、
// 古い値（Stale）は値に「古い」の注記を添える。reading が無い（まだ登録されていない）時はデータなし。
std::string MetricText(const METRIC::MetricReading* reading, int decimals);

// 新しい値（Fresh）か。表の行や数値の表示は、この時だけ値を使う。
inline bool IsFresh(const METRIC::MetricReading* reading) {
    return reading != nullptr && reading->status == METRIC::MetricStatus::Fresh;
}

} // namespace HIKARI::EDITOR
