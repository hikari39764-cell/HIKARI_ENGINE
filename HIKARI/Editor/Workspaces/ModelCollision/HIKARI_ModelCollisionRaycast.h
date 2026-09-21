#pragma once
#include "Editor/Views/HIKARI_EditorViewportGeometry.h"

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"

namespace HIKARI::EDITOR {


namespace DETAIL {

bool IntersectModelCollisionBounds(const EditorPointerRay &ray,
                                   const Bounds &bounds,
                                   float &outDistance) noexcept;

float ComputeModelCollisionBoundsVolume(const Bounds &bounds) noexcept;

bool IntersectModelCollisionShapeExact(
    const EditorPointerRay &ray,
    const ASSETS::COLLISION::ModelCollisionShape &shape,
    float &outDistance) noexcept;

bool IntersectModelCollisionSourceNodeExact(
    const EditorPointerRay &ray, const ModelPreviewNode &node,
    const ModelAsset &model, float &outDistance) noexcept;

} // namespace DETAIL

} // namespace HIKARI::EDITOR
