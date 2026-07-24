#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPreviewScene.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

struct ImDrawList;

namespace HIKARI::EDITOR {

class ModelCollisionSourceOutlineCache {
public:
  void Reset() noexcept;

  bool Draw(ImDrawList *drawList, const Camera3D &camera, float viewportX,
            float viewportY, float viewportWidth, float viewportHeight,
            const ModelAsset &model, const ModelCollisionPreviewNode &node,
            uint64_t modelRevision, uint32_t color, float thickness);

private:
  struct Edge {
    MATH::Vec3 first{};
    MATH::Vec3 second{};
    MATH::Vec3 firstFaceNormal{};
    MATH::Vec3 secondFaceNormal{};
    uint8_t faceCount = 0;
    bool crease = false;
  };

  struct NodeOutline {
    std::vector<Edge> edges{};
    bool truncated = false;
  };

  const NodeOutline *GetOrBuild(const ModelAsset &model,
                                const ModelCollisionPreviewNode &node,
                                uint64_t modelRevision);
  NodeOutline Build(const ModelAsset &model,
                    const ModelCollisionPreviewNode &node) const;

  uint64_t modelRevision_ = 0u;
  std::unordered_map<int32_t, NodeOutline> nodes_{};
};

} // namespace HIKARI::EDITOR
