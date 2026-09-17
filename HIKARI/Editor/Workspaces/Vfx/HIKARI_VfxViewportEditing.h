#pragma once
#include "Render3D/HIKARI_Math3D.h"
#include <string>
namespace HIKARI::EDITOR {
struct VfxViewportHandleState {
    std::string id, layerId, targetId, propertyId;
    bool pathKey = false, angular = false;
    float seconds = 0, initialValue = 0;
    MATH::Vec2 startMouse{}, screenAxis{};
    MATH::Vec3 worldPoint{}, planeNormal{}, scaleValue{1, 1, 1};
    MATH::Mat4 frame = MATH::Mat4::Identity();
};
} // namespace HIKARI::EDITOR
