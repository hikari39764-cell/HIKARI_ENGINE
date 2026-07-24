#pragma once

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPreviewScene.h"

namespace HIKARI::EDITOR {

struct ModelCollisionPointerRay {
  MATH::Vec3 origin{};
  MATH::Vec3 direction{};
};

namespace DETAIL {

bool IntersectModelCollisionBounds(const ModelCollisionPointerRay &ray,
                                   const Bounds &bounds,
                                   float &outDistance) noexcept;

float ComputeModelCollisionBoundsVolume(const Bounds &bounds) noexcept;

bool IntersectModelCollisionShapeExact(
    const ModelCollisionPointerRay &ray,
    const ASSETS::COLLISION::ModelCollisionShape &shape,
    float &outDistance) noexcept;

bool IntersectModelCollisionSourceNodeExact(
    const ModelCollisionPointerRay &ray, const ModelCollisionPreviewNode &node,
    const ModelAsset &model, float &outDistance) noexcept;

} // namespace DETAIL

} // namespace HIKARI::EDITOR
