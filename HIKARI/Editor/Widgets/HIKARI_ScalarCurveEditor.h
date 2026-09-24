#pragma once

#include "Core/Animation/HIKARI_ScalarCurve.h"

namespace HIKARI::EDITOR {
struct ScalarCurveView {
    int selectedKey = -1;
    bool fitted = false;
    float low = -1, high = 1, duration = 1;
};

// 時刻・値・接線を同じ編集トランザクションで変更する。
bool DrawScalarCurve(HIKARI::ScalarCurve& parameter, float duration, float playhead, ScalarCurveView& view,
                     float height = 180);
} // namespace HIKARI::EDITOR
