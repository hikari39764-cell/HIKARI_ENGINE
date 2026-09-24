#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <algorithm>
#include <cmath>
namespace HIKARI {
namespace {
float Safe(float v, float fallback = 0) { return std::isfinite(v) ? v : fallback; }
float Hermite(float a, float b, float out, float in, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * out + (-2 * t3 + 3 * t2) * b + (t3 - t2) * in;
}
void Normalize(ScalarCurve& p) {
    p.value = Safe(p.value);
    std::erase_if(p.keys, [](const auto& k) { return !std::isfinite(k.time) || !std::isfinite(k.value); });
    for (auto& k : p.keys) {
        k.time = (std::max)(0.0f, k.time);
        k.inSlope = Safe(k.inSlope);
        k.outSlope = Safe(k.outSlope);
    }
    std::stable_sort(p.keys.begin(), p.keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}
} // namespace
float ScalarCurve::Evaluate(float seconds) const noexcept {
    seconds = Safe(seconds);
    if (keys.empty())
        return value;
    if (seconds < keys.front().time)
        return keys.front().value;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), seconds,
                                        [](float time, const ScalarCurveKey& key) { return time < key.time; });
    if (upper != keys.end() && upper != keys.begin()) {
        const auto& a = *std::prev(upper);
        const auto& b = *upper;
        const float dt = b.time - a.time;
        if (dt <= .00001f)
            return b.value;
        return Hermite(a.value, b.value, a.outSlope * dt, b.inSlope * dt, (seconds - a.time) / dt);
    }
    return keys.back().value;
}

void NormalizeScalarCurve(ScalarCurve& parameter) {
    Normalize(parameter);
    // 同じ時刻は最後に書き込んだキーを優先する。
    size_t count = 0;
    for (const auto key : parameter.keys) {
        if (count && std::abs(parameter.keys[count - 1].time - key.time) < 0.00001f)
            parameter.keys[count - 1] = key;
        else
            parameter.keys[count++] = key;
    }
    parameter.keys.resize(count);
}

void SetScalarCurveKey(ScalarCurve& parameter, float seconds, float value) {
    seconds = (std::max)(0.0f, Safe(seconds));
    const auto found = std::find_if(parameter.keys.begin(), parameter.keys.end(),
                                    [&](const auto& key) { return std::abs(key.time - seconds) < 0.00001f; });
    if (found != parameter.keys.end())
        found->value = value;
    else
        parameter.keys.push_back({seconds, value, 0, 0});
    NormalizeScalarCurve(parameter);
}

} // namespace HIKARI
