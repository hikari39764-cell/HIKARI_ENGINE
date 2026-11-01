#pragma once

#include "Core/Math/HIKARI_Math3D.h"

// シーケンスの画面効果を、ある時刻で評価した値。Scene のシーケンサーが書き、後処理が読む。
// 型の置き場だけ Vfx/Post にあり、名前空間は変えない。
namespace HIKARI::SEQUENCER {

enum class ScreenEffectKind {
    BloomIntensity,
    BloomThreshold,
    Exposure,
    Saturation,
    Contrast,
    Temperature,
    Vignette,
    Monochrome,
    Tint,
    Fade,
    Pixelate,
    Chromatic,
    // 色を反転する（HDR のまま中間の灰を軸に反転）。
    Invert,
    // 画面中心へ向かって放射状にぼかす。量は中心へ寄せる割合。
    RadialBlur
};

struct ScreenEffectSample {
    ScreenEffectKind kind{};
    float amount = 0.0f;
    float weight = 0.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
};

} // namespace HIKARI::SEQUENCER
