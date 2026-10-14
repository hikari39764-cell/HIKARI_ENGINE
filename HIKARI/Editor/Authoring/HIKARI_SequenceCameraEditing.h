#pragma once
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {
// 外部相机轨道の作者値。読み取りだけでは鍵・補間・所有方式を変更しない。
struct BoundCameraPose {
    MATH::Vec3 position{}, rotationEulerDeg{};
    float fov = 60, nearClip = .1f, farClip = 100;
};
enum CameraPosePart : uint32_t { BoundPosition = 1, BoundRotation = 2, BoundFov = 4, BoundAll = 7 };
bool ReadBoundCameraPose(const CinematicSequence&, SEQUENCER::SequenceBindingId, float, BoundCameraPose&);
bool WriteBoundCameraPose(CinematicSequence&, SEQUENCER::SequenceBindingId, float, const BoundCameraPose&, uint32_t);
std::vector<float> BoundCameraKeyTimes(const CinematicSequence&, SEQUENCER::SequenceBindingId);
bool MoveBoundCameraKey(CinematicSequence&, SEQUENCER::SequenceBindingId, float from, float to);
bool RemoveBoundCameraKey(CinematicSequence&, SEQUENCER::SequenceBindingId, float);
} // namespace HIKARI::EDITOR
