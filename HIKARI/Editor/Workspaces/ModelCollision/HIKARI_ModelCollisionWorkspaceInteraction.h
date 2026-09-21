#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionRaycast.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::EDITOR {


Bounds ComputeModelCollisionShapeBounds(
    const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept;

uint64_t
PickModelCollisionShape(const EditorPointerRay &ray,
                        const ASSETS::COLLISION::ModelCollisionSetup &setup,
                        const std::unordered_set<uint64_t> &hiddenShapeIds,
                        bool generatedOnly) noexcept;

int32_t PickModelCollisionSourceNode(
    const EditorPointerRay &ray,
    const std::vector<ModelPreviewNode> &nodes,
    const ModelAsset &model) noexcept;

} // namespace HIKARI::EDITOR
