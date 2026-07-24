#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceInteraction.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegreesToRadians = 0.01745329251994329577f;

bool Unproject(const MATH::Mat4 &inverseViewProjection, float x, float y,
               float z, MATH::Vec3 &outPoint) noexcept {
  const MATH::Vec4 world =
      inverseViewProjection.TransformPoint({x, y, z, 1.0f});
  if (!MATH::IsFinite(world) || std::abs(world.w) <= 1.0e-6f) {
    return false;
  }
  const float inverseW = 1.0f / world.w;
  outPoint = {world.x * inverseW, world.y * inverseW, world.z * inverseW};
  return MATH::IsFinite(outPoint);
}
} // namespace

bool BuildModelCollisionPointerRay(const Camera3D &camera,
                                   float normalizedViewportX,
                                   float normalizedViewportY,
                                   ModelCollisionPointerRay &outRay) noexcept {
  const float clipX = normalizedViewportX * 2.0f - 1.0f;
  const float clipY = 1.0f - normalizedViewportY * 2.0f;
  const MATH::Mat4 inverseViewProjection = MATH::Inverse(camera.GetViewProj());
  MATH::Vec3 nearPoint{};
  MATH::Vec3 farPoint{};
  if (!Unproject(inverseViewProjection, clipX, clipY, 0.0f, nearPoint) ||
      !Unproject(inverseViewProjection, clipX, clipY, 1.0f, farPoint)) {
    return false;
  }
  const MATH::Vec3 direction = farPoint - nearPoint;
  if (MATH::Length(direction) <= 1.0e-6f) {
    return false;
  }
  outRay.origin = nearPoint;
  outRay.direction = MATH::Normalize(direction);
  return true;
}

Bounds ComputeModelCollisionShapeBounds(
    const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept {
  if (shape.type == ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull ||
      shape.type ==
          ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh) {
    Bounds geometryBounds = BOUNDS::EmptyBounds();
    for (const MATH::Vec3 &vertex : shape.vertices) {
      BOUNDS::Encapsulate(geometryBounds, vertex);
    }
    if (!BOUNDS::IsUsable(geometryBounds)) {
      return {};
    }
    const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
        shape.rotationEulerDegrees.x * kDegreesToRadians,
        shape.rotationEulerDegrees.y * kDegreesToRadians,
        shape.rotationEulerDegrees.z * kDegreesToRadians);
    return BOUNDS::TransformBounds(
        geometryBounds,
        MATH::Mat4::TRS(shape.center, rotation, {1.0f, 1.0f, 1.0f}));
  }

  MATH::Vec3 half{};
  switch (shape.type) {
  case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
    half = {shape.radius, shape.radius, shape.radius};
    break;
  case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
    half = {shape.radius, shape.height * 0.5f, shape.radius};
    break;
  case ASSETS::COLLISION::CollisionGeometryShapeType::Box:
  default:
    half = shape.size * 0.5f;
    break;
  }
  const Bounds localBounds{half * -1.0f, half};
  const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
      shape.rotationEulerDegrees.x * kDegreesToRadians,
      shape.rotationEulerDegrees.y * kDegreesToRadians,
      shape.rotationEulerDegrees.z * kDegreesToRadians);
  return BOUNDS::TransformBounds(
      localBounds, MATH::Mat4::TRS(shape.center, rotation, {1.0f, 1.0f, 1.0f}));
}

uint64_t
PickModelCollisionShape(const ModelCollisionPointerRay &ray,
                        const ASSETS::COLLISION::ModelCollisionSetup &setup,
                        const std::unordered_set<uint64_t> &hiddenShapeIds,
                        bool generatedOnly) noexcept {
  float bestDistance = (std::numeric_limits<float>::max)();
  float bestVolume = (std::numeric_limits<float>::max)();
  uint64_t bestShapeId = 0u;
  for (const ASSETS::COLLISION::ModelCollisionShape &shape : setup.shapes) {
    if (hiddenShapeIds.contains(shape.id) ||
        (generatedOnly && !shape.generated)) {
      continue;
    }
    const Bounds bounds = ComputeModelCollisionShapeBounds(shape);
    float distance = 0.0f;
    if (!DETAIL::IntersectModelCollisionBounds(ray, bounds, distance) ||
        !DETAIL::IntersectModelCollisionShapeExact(ray, shape, distance)) {
      continue;
    }
    const float volume = DETAIL::ComputeModelCollisionBoundsVolume(bounds);
    const float distanceTolerance = (std::max)(0.001f, bestDistance * 0.002f);
    if (distance < bestDistance - distanceTolerance ||
        (std::abs(distance - bestDistance) <= distanceTolerance &&
         volume < bestVolume)) {
      bestDistance = distance;
      bestVolume = volume;
      bestShapeId = shape.id;
    }
  }
  return bestShapeId;
}

int32_t PickModelCollisionSourceNode(
    const ModelCollisionPointerRay &ray,
    const std::vector<ModelCollisionPreviewNode> &nodes,
    const ModelAsset &model) noexcept {
  float bestDistance = (std::numeric_limits<float>::max)();
  float bestVolume = (std::numeric_limits<float>::max)();
  int32_t bestNodeIndex = -1;
  for (const ModelCollisionPreviewNode &node : nodes) {
    float distance = 0.0f;
    if (!DETAIL::IntersectModelCollisionBounds(ray, node.bounds, distance) ||
        !DETAIL::IntersectModelCollisionSourceNodeExact(ray, node, model,
                                                        distance)) {
      continue;
    }
    const float volume = DETAIL::ComputeModelCollisionBoundsVolume(node.bounds);
    const float distanceTolerance = (std::max)(0.001f, bestDistance * 0.002f);
    if (distance < bestDistance - distanceTolerance ||
        (std::abs(distance - bestDistance) <= distanceTolerance &&
         volume < bestVolume)) {
      bestDistance = distance;
      bestVolume = volume;
      bestNodeIndex = node.nodeIndex;
    }
  }
  return bestNodeIndex;
}

} // namespace HIKARI::EDITOR
