#pragma once

#include <cstdint>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/HIKARI_SceneDocument.h"

struct ImDrawList;

namespace HIKARI::EDITOR {

TransformData BuildModelCollisionShapeTransform(
    const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept;

void DrawModelCollisionShapeOverlay(
    ImDrawList *drawList, const Camera3D &camera, float viewportX,
    float viewportY, float viewportWidth, float viewportHeight,
    const ASSETS::COLLISION::ModelCollisionShape &shape, bool selected,
    bool hovered, bool locked, float opacity = 1.0f);

void DrawModelCollisionBoundsOverlay(ImDrawList *drawList,
                                     const Camera3D &camera, float viewportX,
                                     float viewportY, float viewportWidth,
                                     float viewportHeight, const Bounds &bounds,
                                     uint32_t color, float thickness);

} // namespace HIKARI::EDITOR
