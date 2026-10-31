#include "Editor/Performance/HIKARI_MetricText.h"

#include <cstdio>

#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR {

std::string MetricText(const METRIC::MetricReading* reading, int decimals) {
    if (reading == nullptr || reading->status == METRIC::MetricStatus::NoData) {
        return TEXT::Localize("editor.performance.metricNoData");
    }
    if (reading->status == METRIC::MetricStatus::Unsupported) {
        return TEXT::Localize("editor.performance.metricUnsupported");
    }
    char number[64]{};
    std::snprintf(number, sizeof(number), "%.*f", decimals, reading->value);
    if (reading->status == METRIC::MetricStatus::Stale) {
        return std::string(number) + " (" + TEXT::Localize("editor.performance.metricStale") + ")";
    }
    return number;
}

} // namespace HIKARI::EDITOR
