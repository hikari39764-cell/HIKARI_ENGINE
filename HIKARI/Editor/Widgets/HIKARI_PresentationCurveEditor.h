#pragma once

#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"

namespace HIKARI::EDITOR {
    struct PresentationCurveView {
        int selectedKey = -1;
        bool fitted = false;
        float low = -1, high = 1, duration = 1;
    };

    // 時刻・値・接線を同じ編集トランザクションで変更する。
    bool DrawPresentationCurve(SEQUENCER::PresentationParameter& parameter,
        float duration, float playhead, PresentationCurveView& view, float height = 180);
}
