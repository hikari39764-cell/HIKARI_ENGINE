#pragma once

#include <cstdint>
#include <vector>

#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::EDITOR {

enum class CameraTimelineKeyframeKind : uint8_t {
  None,
  Transform,
  Lens,
};

struct CameraTimelineKeyframeSelection {
  CameraTimelineKeyframeKind kind = CameraTimelineKeyframeKind::None;
  SEQUENCER::SequenceBindingId bindingId{};
  uint64_t keyframeId = 0;

  bool IsValid() const noexcept;
  bool operator==(const CameraTimelineKeyframeSelection &rhs) const noexcept =
      default;
};

enum class CameraTimelineSelectionOperation : uint8_t {
  Replace,
  Add,
  Toggle,
};

class CameraTimelineSelectionSet {
public:
  bool Empty() const noexcept;
  size_t Size() const noexcept;
  bool
  Contains(const CameraTimelineKeyframeSelection &selection) const noexcept;
  const std::vector<CameraTimelineKeyframeSelection> &Items() const noexcept;
  CameraTimelineKeyframeSelection Primary() const noexcept;

  bool Clear() noexcept;
  bool Apply(const CameraTimelineKeyframeSelection &selection,
             CameraTimelineSelectionOperation operation);
  bool Apply(const std::vector<CameraTimelineKeyframeSelection> &selections,
             CameraTimelineSelectionOperation operation);
  bool Remove(const CameraTimelineKeyframeSelection &selection) noexcept;

private:
  std::vector<CameraTimelineKeyframeSelection> items_{};
};

} // namespace HIKARI::EDITOR
