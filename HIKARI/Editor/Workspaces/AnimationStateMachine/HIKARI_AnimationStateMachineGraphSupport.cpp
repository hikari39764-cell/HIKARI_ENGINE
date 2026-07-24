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
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineGraphSupport.h"

namespace HIKARI::EDITOR::ANIMATION_STATE_MACHINE_GRAPH {

#if defined(HIKARI_WITH_EDITOR)
std::string MotionLabel(const ANIMATION::AnimationStateMotion &motion) {
  if (const auto *clip = std::get_if<ANIMATION::AnimationClipMotion>(&motion)) {
    return clip->clip.fallbackName.empty() ? "<no animation>"
                                           : clip->clip.fallbackName;
  }
  const auto &blendTree =
      std::get<ANIMATION::AnimationBlendTree1DMotion>(motion);
  return "1D Blend  |  " + std::to_string(blendTree.samples.size()) +
         " samples";
}

bool Contains(const ImVec2 &minimum, const ImVec2 &maximum,
              const ImVec2 &point) noexcept {
  return point.x >= minimum.x && point.y >= minimum.y && point.x <= maximum.x &&
         point.y <= maximum.y;
}

float DistanceToSegment(const ImVec2 &point, const ImVec2 &start,
                        const ImVec2 &end) noexcept {
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const float lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 0.0001f) {
    const float px = point.x - start.x;
    const float py = point.y - start.y;
    return std::sqrt(px * px + py * py);
  }
  const float projection = std::clamp(
      ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared,
      0.0f, 1.0f);
  const float closestX = start.x + dx * projection;
  const float closestY = start.y + dy * projection;
  const float px = point.x - closestX;
  const float py = point.y - closestY;
  return std::sqrt(px * px + py * py);
}
#endif

} // namespace HIKARI::EDITOR::ANIMATION_STATE_MACHINE_GRAPH
