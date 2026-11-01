#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

// 性能の解析（PerformanceAnalysis と PerformanceBreakdownAnalysis）が JSON を読み、発見を作る小さな道具。
// 入力の JSON は信用せず、無い・null・型違いは既定値にする。
namespace HIKARI::DIAGNOSTICS::ANALYSIS {

inline const nlohmann::json& Child(const nlohmann::json& object, const char* key) {
    static const nlohmann::json kNull;
    if (!object.is_object())
        return kNull;
    const auto found = object.find(key);
    return found != object.end() ? *found : kNull;
}

inline double NumberOr(const nlohmann::json& object, const char* key, double fallback = 0.0) {
    const nlohmann::json& value = Child(object, key);
    return value.is_number() ? value.get<double>() : fallback;
}

inline bool BoolOr(const nlohmann::json& object, const char* key, bool fallback) {
    const nlohmann::json& value = Child(object, key);
    return value.is_boolean() ? value.get<bool>() : fallback;
}

inline std::string StringOr(const nlohmann::json& object, const char* key, const std::string& fallback = {}) {
    const nlohmann::json& value = Child(object, key);
    return value.is_string() ? value.get<std::string>() : fallback;
}

inline bool HasNumber(const nlohmann::json& object, const char* key) { return Child(object, key).is_number(); }

inline std::string Fixed(double value, int decimals) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

inline int SeverityRank(std::string_view severity) {
    return severity == "critical" ? 0 : severity == "warning" ? 1 : 2;
}

inline nlohmann::json Finding(std::string id, const char* severity, std::string title, std::string detail,
                              std::vector<std::string> evidence, nlohmann::json data = nlohmann::json::object()) {
    return {{"id", std::move(id)},
            {"severity", severity},
            {"title", std::move(title)},
            {"detail", std::move(detail)},
            {"evidence", std::move(evidence)},
            {"data", std::move(data)}};
}

// 発見の並べ替えと件数、結果の形（perf.findings が返す）。severity の順（critical、warning、info）で安定に並べる。
inline nlohmann::json FindingsResult(std::vector<nlohmann::json> findings, std::string_view bound) {
    std::stable_sort(findings.begin(), findings.end(), [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
        return SeverityRank(StringOr(lhs, "severity")) < SeverityRank(StringOr(rhs, "severity"));
    });
    size_t critical = 0, warning = 0, info = 0;
    for (const nlohmann::json& finding : findings) {
        const std::string severity = StringOr(finding, "severity");
        (severity == "critical" ? critical : severity == "warning" ? warning : info) += 1;
    }
    nlohmann::json result = nlohmann::json::object();
    result["bound"] = bound;
    result["summary"] = {{"critical", critical}, {"warning", warning}, {"info", info}};
    result["findings"] = std::move(findings);
    return result;
}

} // namespace HIKARI::DIAGNOSTICS::ANALYSIS
