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
        if (k.interpolation != ScalarCurveInterpolation::Cubic && k.interpolation != ScalarCurveInterpolation::Linear &&
            k.interpolation != ScalarCurveInterpolation::Hold)
            k.interpolation = ScalarCurveInterpolation::Cubic;
        k.inSlope = Safe(k.inSlope);
        k.outSlope = Safe(k.outSlope);
        NormalizeScalarCurveShape(k.shape);
    }
    std::stable_sort(p.keys.begin(), p.keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}
} // namespace
void NormalizeScalarCurveShape(ScalarCurveShape& shape) {
    shape.inWeight = std::clamp(Safe(shape.inWeight, 1.f / 3), .001f, .999f);
    shape.outWeight = std::clamp(Safe(shape.outWeight, 1.f / 3), .001f, .999f);
    if (!TEXT::IsNamedEnum(kScalarTangentNames, shape.tangent))
        shape.tangent = ScalarTangentMode::Free;
}
ScalarCurveKey ResolvedScalarKey(std::span<const ScalarCurveKey> keys, size_t index) {
    auto key = keys[index];
    if (key.shape.tangent == ScalarTangentMode::Auto) {
        const float span = index && index + 1 < keys.size() ? keys[index + 1].time - keys[index - 1].time : 0;
        key.inSlope = key.outSlope = span > .00001f ? (keys[index + 1].value - keys[index - 1].value) / span : 0;
    }
    if (index && keys[index - 1].interpolation != ScalarCurveInterpolation::Cubic)
        key.inSlope = keys[index - 1].interpolation == ScalarCurveInterpolation::Hold
                          ? 0
                          : (key.value - keys[index - 1].value) / (std::max)(.00001f, key.time - keys[index - 1].time);
    if (index + 1 < keys.size() && key.interpolation != ScalarCurveInterpolation::Cubic)
        key.outSlope = key.interpolation == ScalarCurveInterpolation::Hold
                           ? 0
                           : (keys[index + 1].value - key.value) / (std::max)(.00001f, keys[index + 1].time - key.time);
    return key;
}
float EvaluateScalarSegment(const ScalarCurveKey& a, const ScalarCurveKey& b, float seconds, bool velocity) noexcept {
    const float dt = b.time - a.time;
    if (dt <= .00001f)
        return velocity ? 0 : b.value;
    const float t = std::clamp((seconds - a.time) / dt, 0.f, 1.f);
    if (a.interpolation == ScalarCurveInterpolation::Hold)
        return velocity ? 0 : a.value;
    if (a.interpolation == ScalarCurveInterpolation::Linear)
        return velocity ? (b.value - a.value) / dt : a.value + (b.value - a.value) * t;
    // 既存曲線では元の算術経路を保つ。重みを編集した区間だけ時間 Bezier を解く。
    if (a.shape.outWeight == 1.f / 3 && b.shape.inWeight == 1.f / 3) {
        if (!velocity)
            return Hermite(a.value, b.value, a.outSlope * dt, b.inSlope * dt, t);
        return ((6 * t * t - 6 * t) * a.value + (3 * t * t - 4 * t + 1) * a.outSlope * dt +
                (-6 * t * t + 6 * t) * b.value + (3 * t * t - 2 * t) * b.inSlope * dt) /
               dt;
    }
    const auto bezier = [](float p0, float p1, float p2, float p3, float u) {
        const float v = 1 - u;
        return v * v * v * p0 + 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u * p3;
    };
    const auto derivative = [](float p0, float p1, float p2, float p3, float u) {
        const float v = 1 - u;
        return 3 * (v * v * (p1 - p0) + 2 * v * u * (p2 - p1) + u * u * (p3 - p2));
    };
    const float w0 = std::clamp(a.shape.outWeight, .001f, .999f);
    const float w1 = std::clamp(b.shape.inWeight, .001f, .999f);
    float lo = 0, hi = 1, u = t;
    for (int i = 0; i < 24; ++i) {
        const float x = bezier(0, w0, 1 - w1, 1, u);
        if (x < t)
            lo = u;
        else
            hi = u;
        u = (lo + hi) * .5f;
    }
    const float y0 = a.value + a.outSlope * dt * w0;
    const float y1 = b.value - b.inSlope * dt * w1;
    return velocity
               ? derivative(a.value, y0, y1, b.value, u) / (std::max)(.000001f, dt * derivative(0, w0, 1 - w1, 1, u))
               : bezier(a.value, y0, y1, b.value, u);
}
float ScalarCurve::Evaluate(float seconds) const noexcept {
    seconds = Safe(seconds);
    if (keys.empty())
        return value;
    if (seconds < keys.front().time)
        return keys.front().value;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), seconds,
                                        [](float time, const ScalarCurveKey& key) { return time < key.time; });
    if (upper != keys.end() && upper != keys.begin()) {
        const auto i = static_cast<size_t>(upper - keys.begin());
        return EvaluateScalarSegment(ResolvedScalarKey(keys, i - 1), ResolvedScalarKey(keys, i), seconds);
    }
    return keys.back().value;
}
float ScalarCurve::Velocity(float seconds) const noexcept {
    if (keys.size() < 2 || seconds < keys.front().time || seconds > keys.back().time)
        return 0;
    auto upper = std::upper_bound(keys.begin(), keys.end(), Safe(seconds),
                                  [](float time, const auto& key) { return time < key.time; });
    const size_t i = std::clamp(static_cast<size_t>(upper - keys.begin()), size_t{1}, keys.size() - 1);
    return EvaluateScalarSegment(ResolvedScalarKey(keys, i - 1), ResolvedScalarKey(keys, i), seconds, true);
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
