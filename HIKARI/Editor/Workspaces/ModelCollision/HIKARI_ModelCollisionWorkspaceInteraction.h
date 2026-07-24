#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPreviewScene.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionRaycast.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::EDITOR {

bool BuildModelCollisionPointerRay(const Camera3D &camera,
                                   float normalizedViewportX,
                                   float normalizedViewportY,
                                   ModelCollisionPointerRay &outRay) noexcept;

Bounds ComputeModelCollisionShapeBounds(
    const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept;

uint64_t
PickModelCollisionShape(const ModelCollisionPointerRay &ray,
                        const ASSETS::COLLISION::ModelCollisionSetup &setup,
                        const std::unordered_set<uint64_t> &hiddenShapeIds,
                        bool generatedOnly) noexcept;

int32_t PickModelCollisionSourceNode(
    const ModelCollisionPointerRay &ray,
    const std::vector<ModelCollisionPreviewNode> &nodes,
    const ModelAsset &model) noexcept;

} // namespace HIKARI::EDITOR
