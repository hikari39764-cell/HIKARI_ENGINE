#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionSourceOutline.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

constexpr size_t kMaximumTopologyEdges = 250000u;
constexpr float kNormalEpsilon = 1.0e-10f;
constexpr float kCreaseCosine = 0.7660444431f;

MATH::Vec3 TransformPoint(const MATH::Mat4 &matrix,
                          const MATH::Vec3 &point) noexcept {
  const MATH::Vec4 transformed =
      matrix.TransformPoint({point.x, point.y, point.z, 1.0f});
  return {transformed.x, transformed.y, transformed.z};
}

MATH::Vec3 FaceNormal(const MATH::Vec3 &first, const MATH::Vec3 &second,
                      const MATH::Vec3 &third) noexcept {
  const MATH::Vec3 normal = MATH::Cross(second - first, third - first);
  const float length = MATH::Length(normal);
  return length > kNormalEpsilon ? normal * (1.0f / length) : MATH::Vec3{};
}

#if defined(HIKARI_WITH_EDITOR)
bool ProjectPoint(const Camera3D &camera, const MATH::Vec3 &point,
                  const ImVec2 &origin, const ImVec2 &size,
                  ImVec2 &outPoint) noexcept {
  const MATH::Vec4 clip =
      camera.GetViewProj().TransformPoint({point.x, point.y, point.z, 1.0f});
  if (clip.w <= 1.0e-5f) {
    return false;
  }
  const float inverseW = 1.0f / clip.w;
  const float x = clip.x * inverseW;
  const float y = clip.y * inverseW;
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return false;
  }
  outPoint.x = origin.x + (x * 0.5f + 0.5f) * size.x;
  outPoint.y = origin.y + (0.5f - y * 0.5f) * size.y;
  return true;
}
#endif

} // namespace

void ModelCollisionSourceOutlineCache::Reset() noexcept {
  modelRevision_ = 0u;
  nodes_.clear();
}

const ModelCollisionSourceOutlineCache::NodeOutline *
ModelCollisionSourceOutlineCache::GetOrBuild(
    const ModelAsset &model, const ModelCollisionPreviewNode &node,
    uint64_t modelRevision) {
  if (modelRevision_ != modelRevision) {
    nodes_.clear();
    modelRevision_ = modelRevision;
  }
  const auto found = nodes_.find(node.nodeIndex);
  if (found != nodes_.end()) {
    return &found->second;
  }
  auto [inserted, _] = nodes_.emplace(node.nodeIndex, Build(model, node));
  return &inserted->second;
}

ModelCollisionSourceOutlineCache::NodeOutline
ModelCollisionSourceOutlineCache::Build(
    const ModelAsset &model, const ModelCollisionPreviewNode &node) const {
  NodeOutline outline{};
  if (node.meshIndex < 0 ||
      static_cast<size_t>(node.meshIndex) >= model.meshes.size()) {
    return outline;
  }

  const MeshAsset &mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
  for (const MeshPrimitive &primitive : mesh.primitives) {
    const size_t vertexCount =
        primitive.layout == VertexLayoutKind::SkinnedPNTTJW
            ? primitive.skinnedVertices.size()
            : primitive.staticVertices.size();
    if (vertexCount == 0u || primitive.indices.size() < 3u) {
      continue;
    }

    std::vector<MATH::Vec3> positions(vertexCount);
    for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
      const MATH::Vec3 localPosition =
          primitive.layout == VertexLayoutKind::SkinnedPNTTJW
              ? primitive.skinnedVertices[vertexIndex].position
              : primitive.staticVertices[vertexIndex].position;
      positions[vertexIndex] =
          TransformPoint(node.globalTransform, localPosition);
    }

    struct EdgeAccumulator {
      MATH::Vec3 first{};
      MATH::Vec3 second{};
      MATH::Vec3 normal1{};
      MATH::Vec3 normal2{};
      uint8_t faceCount = 0;
    };
    std::unordered_map<uint64_t, EdgeAccumulator> topology{};
    topology.reserve(
        (std::min)(primitive.indices.size(), kMaximumTopologyEdges));

    const auto addEdge = [&](uint32_t firstIndex, uint32_t secondIndex,
                             const MATH::Vec3 &normal) {
      const uint32_t minimum = (std::min)(firstIndex, secondIndex);
      const uint32_t maximum = (std::max)(firstIndex, secondIndex);
      const uint64_t key = (static_cast<uint64_t>(minimum) << 32u) | maximum;
      auto [found, inserted] = topology.try_emplace(key);
      EdgeAccumulator &edge = found->second;
      if (inserted) {
        edge.first = positions[firstIndex];
        edge.second = positions[secondIndex];
        edge.normal1 = normal;
        edge.faceCount = 1u;
      } else {
        if (edge.faceCount == 1u) {
          edge.normal2 = normal;
        }
        edge.faceCount = static_cast<uint8_t>(
            (std::min)(255, static_cast<int>(edge.faceCount) + 1));
      }
    };

    for (size_t index = 0; index + 2u < primitive.indices.size(); index += 3u) {
      const uint32_t index0 = primitive.indices[index];
      const uint32_t index1 = primitive.indices[index + 1u];
      const uint32_t index2 = primitive.indices[index + 2u];
      if (index0 >= vertexCount || index1 >= vertexCount ||
          index2 >= vertexCount || index0 == index1 || index1 == index2 ||
          index2 == index0) {
        continue;
      }
      const MATH::Vec3 normal =
          FaceNormal(positions[index0], positions[index1], positions[index2]);
      if (MATH::Dot(normal, normal) <= kNormalEpsilon) {
        continue;
      }
      addEdge(index0, index1, normal);
      addEdge(index1, index2, normal);
      addEdge(index2, index0, normal);
      if (topology.size() >= kMaximumTopologyEdges) {
        outline.truncated = true;
        break;
      }
    }

    outline.edges.reserve(outline.edges.size() + topology.size());
    for (const auto &[_, source] : topology) {
      Edge edge{};
      edge.first = source.first;
      edge.second = source.second;
      edge.firstFaceNormal = source.normal1;
      edge.secondFaceNormal = source.normal2;
      edge.faceCount = source.faceCount;
      edge.crease = source.faceCount != 2u ||
                    MATH::Dot(source.normal1, source.normal2) < kCreaseCosine;
      outline.edges.push_back(edge);
    }
    if (outline.truncated) {
      break;
    }
  }
  return outline;
}

bool ModelCollisionSourceOutlineCache::Draw(
    ImDrawList *drawList, const Camera3D &camera, float viewportX,
    float viewportY, float viewportWidth, float viewportHeight,
    const ModelAsset &model, const ModelCollisionPreviewNode &node,
    uint64_t modelRevision, uint32_t color, float thickness) {
#if defined(HIKARI_WITH_EDITOR)
  if (drawList == nullptr || viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
    return false;
  }
  const NodeOutline *outline = GetOrBuild(model, node, modelRevision);
  if (outline == nullptr || outline->edges.empty()) {
    return false;
  }

  const ImVec2 origin{viewportX, viewportY};
  const ImVec2 size{viewportWidth, viewportHeight};
  const MATH::Vec3 cameraPosition = camera.GetPosition();
  size_t drawnEdgeCount = 0u;
  for (const Edge &edge : outline->edges) {
    const MATH::Vec3 midpoint = (edge.first + edge.second) * 0.5f;
    const MATH::Vec3 toCamera = cameraPosition - midpoint;
    const float firstFacing = MATH::Dot(edge.firstFaceNormal, toCamera);
    const float secondFacing = MATH::Dot(edge.secondFaceNormal, toCamera);
    const bool boundary = edge.faceCount == 1u;
    const bool silhouette = edge.faceCount == 2u &&
                            ((firstFacing >= 0.0f) != (secondFacing >= 0.0f));
    const bool visibleCrease =
        edge.crease && (firstFacing >= 0.0f || secondFacing >= 0.0f);
    if (!boundary && !silhouette && !visibleCrease) {
      continue;
    }

    ImVec2 first{};
    ImVec2 second{};
    if (!ProjectPoint(camera, edge.first, origin, size, first) ||
        !ProjectPoint(camera, edge.second, origin, size, second)) {
      continue;
    }
    drawList->AddLine(first, second, IM_COL32(12, 16, 20, 230),
                      thickness + 2.6f);
    drawList->AddLine(first, second, color, thickness);
    ++drawnEdgeCount;
  }
  return drawnEdgeCount > 0u;
#else
  (void)drawList;
  (void)camera;
  (void)viewportX;
  (void)viewportY;
  (void)viewportWidth;
  (void)viewportHeight;
  (void)model;
  (void)node;
  (void)modelRevision;
  (void)color;
  (void)thickness;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
