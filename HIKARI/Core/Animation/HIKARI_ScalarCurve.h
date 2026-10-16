#pragma once
#include <vector>
#include <span>
#include "Core/Text/HIKARI_EnumNames.h"
namespace HIKARI {
enum class ScalarCurveInterpolation { Cubic, Linear, Hold };
enum class ScalarTangentMode { Free, Continuous, Auto };
inline constexpr TEXT::EnumName<ScalarTangentMode> kScalarTangentNames[] = {
    {ScalarTangentMode::Free, "bezier"},
    {ScalarTangentMode::Continuous, "continuous"},
    {ScalarTangentMode::Auto, "auto"}};
// 重みは隣接区間の時間に対する割合。既定の 1/3 は従来の Hermite と同じ形になる。
struct ScalarCurveShape {
    float inWeight = 1.f / 3, outWeight = 1.f / 3;
    ScalarTangentMode tangent = ScalarTangentMode::Free;
    bool operator==(const ScalarCurveShape&) const = default;
};
struct ScalarCurveKey {
    float time = 0, value = 0, inSlope = 0, outSlope = 0;
    ScalarCurveInterpolation interpolation = ScalarCurveInterpolation::Cubic;
    ScalarCurveShape shape{};
    bool operator==(const ScalarCurveKey&) const = default;
};
// 秒と値を所有する純粋な曲線。特定のワークスペースや World に依存しない。
struct ScalarCurve {
    float value = 0;
    std::vector<ScalarCurveKey> keys;
    float Evaluate(float seconds) const noexcept;
    float Velocity(float seconds) const noexcept;
    bool operator==(const ScalarCurve&) const = default;
};
void NormalizeScalarCurve(ScalarCurve& curve);
void SetScalarCurveKey(ScalarCurve& curve, float seconds, float value);
// 編集用グラフと姿勢評価もこの区間評価を使い、表示だけの別補間を持たない。
float EvaluateScalarSegment(const ScalarCurveKey& a, const ScalarCurveKey& b, float seconds,
                            bool velocity = false) noexcept;
ScalarCurveKey ResolvedScalarKey(std::span<const ScalarCurveKey> keys, size_t index);
void NormalizeScalarCurveShape(ScalarCurveShape& shape);
} // namespace HIKARI
