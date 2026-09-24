#pragma once
#include <vector>
namespace HIKARI {
struct ScalarCurveKey {
    float time = 0, value = 0, inSlope = 0, outSlope = 0;
};
// 秒と値を所有する純粋な曲線。特定のワークスペースや World に依存しない。
struct ScalarCurve {
    float value = 0;
    std::vector<ScalarCurveKey> keys;
    float Evaluate(float seconds) const noexcept;
};
void NormalizeScalarCurve(ScalarCurve& curve);
void SetScalarCurveKey(ScalarCurve& curve, float seconds, float value);
} // namespace HIKARI
