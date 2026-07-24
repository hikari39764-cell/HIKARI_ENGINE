#pragma once

#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

// HIKARIエンジンのアニメーションステートマシングラフ用の補助関数を提供するヘッダーファイル
namespace HIKARI::EDITOR::ANIMATION_STATE_MACHINE_GRAPH {
#if defined(HIKARI_WITH_EDITOR)

	// アニメーションモーションのラベルを取得する関数
std::string MotionLabel(const ANIMATION::AnimationStateMotion &motion);

    // 指定された矩形領域内に点が含まれているかを判定する関数
bool Contains(const ImVec2 &minimum, const ImVec2 &maximum,const ImVec2 &point) noexcept;

    // 点から線分までの距離を計算する関数
float DistanceToSegment(const ImVec2 &point, const ImVec2 &start,const ImVec2 &end) noexcept;

#endif

} // namespace HIKARI::EDITOR::ANIMATION_STATE_MACHINE_GRAPH
