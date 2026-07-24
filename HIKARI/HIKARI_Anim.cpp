#include "HIKARI_Anim.h"
#include <cmath>

namespace HIKARI {
    namespace ANIM {

        // -------- イージング実装 --------
        namespace EASE {
            static inline float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

            float Linear(float t) { return Clamp01(t); }

            float InQuad(float t) { t = Clamp01(t); return t * t; }
            float OutQuad(float t) { t = Clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
            float InOutQuad(float t) { t = Clamp01(t); return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f); }

            float InCubic(float t) { t = Clamp01(t); return t * t * t; }
            float OutCubic(float t) { t = Clamp01(t); t = 1.0f - t; return 1.0f - t * t * t; }
            float InOutCubic(float t) { t = Clamp01(t); return (t < 0.5f) ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f; }

            static inline float BackPoly(float t, float s) { return t * t * ((s + 1.0f) * t - s); }
            float OutBack(float t) { t = Clamp01(t); const float s = 1.70158f; t = t - 1.0f; return 1.0f + BackPoly(t, s); }
            float InBack(float t) { t = Clamp01(t); const float s = 1.70158f; return BackPoly(t, s); }
            float InOutBack(float t) {
                t = Clamp01(t); const float s = 1.70158f * 1.525f; return (t < 0.5f)
                    ? ((t * 2.0f) * (t * 2.0f) * ((s + 1.0f) * (t * 2.0f) - s)) * 0.5f
                    : (1.0f + ((t * 2.0f - 2.0f) * (t * 2.0f - 2.0f) * ((s + 1.0f) * (t * 2.0f - 2.0f) + s)) * 0.5f);
            }
        }

        // -------- TweenFloat 実装 --------
        // （Update で用いる処理以外はヘッダ内にインライン化）

        // -------- TweenVec2 実装 --------
        // （実装はすべてヘッダ内で TweenFloat に委譲）

        // -------- TransformAnimator 実装 --------
        // （すべてヘッダ内。Update は3つのツイーンを合成して Transform2D に反映）

        // -------- Sequence 実装 --------
        // （全てヘッダ内で実装）

    }
} // namespace HIKARI::ANIM
