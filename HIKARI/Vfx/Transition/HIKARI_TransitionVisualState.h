#pragma once

#include <string>

namespace HIKARI {

// 後処理が描く場面遷移の見た目。Scene の遷移バスが毎フレーム書き、後処理は読むだけ。
struct TransitionVisualState {
    bool active = false;
    std::string profileId{};
    float progress = 0.0f;
    bool isTransitionIn = false;
    float outDuration = 0.0f; // 追加: outDuration メンバー
    float inDuration = 0.0f;  // 追加: inDuration メンバー（必要に応じて）
};

} // namespace HIKARI
