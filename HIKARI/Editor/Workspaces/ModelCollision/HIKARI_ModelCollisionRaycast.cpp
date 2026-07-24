#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionRaycast.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::EDITOR::DETAIL {
namespace {
constexpr float kDegreesToRadians = 0.01745329251994329577f;

MATH::Vec3 TransformPoint(const MATH::Mat4 &transform,
                          const MATH::Vec3 &point) noexcept {
  const MATH::Vec4 result =
      transform.TransformPoint({point.x, point.y, point.z, 1.0f});
  if (std::abs(result.w) <= 1.0e-7f) {
    return {result.x, result.y, result.z};
  }
  const float inverseW = 1.0f / result.w;
  return {result.x * inverseW, result.y * inverseW, result.z * inverseW};
}

bool IntersectRayTriangle(const ModelCollisionPointerRay &ray,
                          const MATH::Vec3 &a, const MATH::Vec3 &b,
                          const MATH::Vec3 &c, float &outDistance) noexcept {
  constexpr float epsilon = 1.0e-7f;
  const MATH::Vec3 edge1 = b - a;
  const MATH::Vec3 edge2 = c - a;
  const MATH::Vec3 p = MATH::Cross(ray.direction, edge2);
  const float determinant = MATH::Dot(edge1, p);
  if (std::abs(determinant) <= epsilon) {
    return false;
  }
  const float inverseDeterminant = 1.0f / determinant;
  const MATH::Vec3 t = ray.origin - a;
  const float u = MATH::Dot(t, p) * inverseDeterminant;
  if (u < 0.0f || u > 1.0f) {
    return false;
  }
  const MATH::Vec3 q = MATH::Cross(t, edge1);
  const float v = MATH::Dot(ray.direction, q) * inverseDeterminant;
  if (v < 0.0f || u + v > 1.0f) {
    return false;
  }
  const float distance = MATH::Dot(edge2, q) * inverseDeterminant;
  if (!std::isfinite(distance) || distance < 0.0f) {
    return false;
  }
  outDistance = distance;
  return true;
}

bool IntersectGeometry(const ModelCollisionPointerRay &ray,
                       const std::vector<MATH::Vec3> &vertices,
                       const std::vector<uint32_t> &indices,
                       const MATH::Mat4 &transform,
                       float &outDistance) noexcept {
  if (vertices.size() < 3u) {
    return false;
  }
  float best = (std::numeric_limits<float>::max)();
  const size_t indexCount = indices.empty()
                                ? vertices.size() - vertices.size() % 3u
                                : indices.size() - indices.size() % 3u;
  for (size_t base = 0u; base < indexCount; base += 3u) {
    const uint32_t ia =
        indices.empty() ? static_cast<uint32_t>(base) : indices[base];
    const uint32_t ib =
        indices.empty() ? static_cast<uint32_t>(base + 1u) : indices[base + 1u];
    const uint32_t ic =
        indices.empty() ? static_cast<uint32_t>(base + 2u) : indices[base + 2u];
    if (ia >= vertices.size() || ib >= vertices.size() ||
        ic >= vertices.size()) {
      continue;
    }
    float distance = 0.0f;
    if (IntersectRayTriangle(ray, TransformPoint(transform, vertices[ia]),
                             TransformPoint(transform, vertices[ib]),
                             TransformPoint(transform, vertices[ic]),
                             distance)) {
      best = (std::min)(best, distance);
    }
  }
  if (best == (std::numeric_limits<float>::max)()) {
    return false;
  }
  outDistance = best;
  return true;
}

ModelCollisionPointerRay
TransformRay(const ModelCollisionPointerRay &ray,
             const MATH::Mat4 &inverseTransform) noexcept {
  ModelCollisionPointerRay local{};
  local.origin = TransformPoint(inverseTransform, ray.origin);
  const MATH::Vec3 end =
      TransformPoint(inverseTransform, ray.origin + ray.direction);
  local.direction = MATH::Normalize(end - local.origin);
  return local;
}

bool IntersectRaySphere(const ModelCollisionPointerRay &ray,
                        const MATH::Vec3 &center, float radius,
                        float &outDistance) noexcept {
  const MATH::Vec3 offset = ray.origin - center;
  const float b = MATH::Dot(offset, ray.direction);
  const float c = MATH::Dot(offset, offset) - radius * radius;
  const float discriminant = b * b - c;
  if (discriminant < 0.0f) {
    return false;
  }
  const float root = std::sqrt(discriminant);
  const float nearDistance = -b - root;
  const float farDistance = -b + root;
  const float distance = nearDistance >= 0.0f ? nearDistance : farDistance;
  if (distance < 0.0f || !std::isfinite(distance)) {
    return false;
  }
  outDistance = distance;
  return true;
}

bool IntersectRayCapsule(const ModelCollisionPointerRay &ray, float radius,
                         float height, float &outDistance) noexcept {
  const float halfSegment = (std::max)(0.0f, height * 0.5f - radius);
  const MATH::Vec3 a{0.0f, -halfSegment, 0.0f};
  const MATH::Vec3 b{0.0f, halfSegment, 0.0f};
  const MATH::Vec3 ba = b - a;
  const MATH::Vec3 oa = ray.origin - a;
  const float baba = MATH::Dot(ba, ba);
  if (baba <= 1.0e-8f) {
    return IntersectRaySphere(ray, {}, radius, outDistance);
  }
  const float bard = MATH::Dot(ba, ray.direction);
  const float baoa = MATH::Dot(ba, oa);
  const float rdoa = MATH::Dot(ray.direction, oa);
  const float oaoa = MATH::Dot(oa, oa);
  const float quadraticA = baba - bard * bard;
  const float quadraticB = baba * rdoa - baoa * bard;
  const float quadraticC = baba * oaoa - baoa * baoa - radius * radius * baba;
  if (std::abs(quadraticA) > 1.0e-8f) {
    const float discriminant =
        quadraticB * quadraticB - quadraticA * quadraticC;
    if (discriminant >= 0.0f) {
      const float distance =
          (-quadraticB - std::sqrt(discriminant)) / quadraticA;
      const float heightAlongAxis = baoa + distance * bard;
      if (distance >= 0.0f && heightAlongAxis > 0.0f &&
          heightAlongAxis < baba) {
        outDistance = distance;
        return true;
      }
    }
  }
  float distanceA = 0.0f;
  float distanceB = 0.0f;
  const bool hitA = IntersectRaySphere(ray, a, radius, distanceA);
  const bool hitB = IntersectRaySphere(ray, b, radius, distanceB);
  if (!hitA && !hitB) {
    return false;
  }
  outDistance = hitA && hitB ? (std::min)(distanceA, distanceB)
                : hitA       ? distanceA
                             : distanceB;
  return true;
}

bool IntersectPrimitiveShape(
    const ModelCollisionPointerRay &worldRay,
    const ASSETS::COLLISION::ModelCollisionShape &shape,
    float &outDistance) noexcept {
  const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
      shape.rotationEulerDegrees.x * kDegreesToRadians,
      shape.rotationEulerDegrees.y * kDegreesToRadians,
      shape.rotationEulerDegrees.z * kDegreesToRadians);
  const ModelCollisionPointerRay ray =
      TransformRay(worldRay, MATH::Inverse(MATH::Mat4::TRS(
                                 shape.center, rotation, {1.0f, 1.0f, 1.0f})));
  switch (shape.type) {
  case ASSETS::COLLISION::CollisionGeometryShapeType::Sphere:
    return IntersectRaySphere(ray, {}, shape.radius, outDistance);
  case ASSETS::COLLISION::CollisionGeometryShapeType::Capsule:
    return IntersectRayCapsule(ray, shape.radius, shape.height, outDistance);
  case ASSETS::COLLISION::CollisionGeometryShapeType::Box: {
    const MATH::Vec3 half = shape.size * 0.5f;
    return IntersectModelCollisionBounds(ray, {half * -1.0f, half},
                                         outDistance);
  }
  default:
    return false;
  }
}
} // namespace

bool IntersectModelCollisionBounds(const ModelCollisionPointerRay &ray,
                                   const Bounds &bounds,
                                   float &outDistance) noexcept {
  if (!BOUNDS::IsUsable(bounds)) {
    return false;
  }
  float minimumDistance = 0.0f;
  float maximumDistance = (std::numeric_limits<float>::max)();
  const std::array<float, 3> origins{ray.origin.x, ray.origin.y, ray.origin.z};
  const std::array<float, 3> directions{ray.direction.x, ray.direction.y,
                                        ray.direction.z};
  const std::array<float, 3> minimums{bounds.min.x, bounds.min.y, bounds.min.z};
  const std::array<float, 3> maximums{bounds.max.x, bounds.max.y, bounds.max.z};
  for (size_t axis = 0; axis < 3u; ++axis) {
    if (std::abs(directions[axis]) <= 1.0e-7f) {
      if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis]) {
        return false;
      }
      continue;
    }
    const float inverseDirection = 1.0f / directions[axis];
    float first = (minimums[axis] - origins[axis]) * inverseDirection;
    float second = (maximums[axis] - origins[axis]) * inverseDirection;
    if (first > second) {
      std::swap(first, second);
    }
    minimumDistance = (std::max)(minimumDistance, first);
    maximumDistance = (std::min)(maximumDistance, second);
    if (minimumDistance > maximumDistance) {
      return false;
    }
  }
  outDistance = minimumDistance;
  return std::isfinite(outDistance);
}

float ComputeModelCollisionBoundsVolume(const Bounds &bounds) noexcept {
  const MATH::Vec3 size = bounds.max - bounds.min;
  return (std::max)(0.0f, size.x * size.y * size.z);
}

bool IntersectModelCollisionShapeExact(
    const ModelCollisionPointerRay &ray,
    const ASSETS::COLLISION::ModelCollisionShape &shape,
    float &outDistance) noexcept {
  if (shape.type != ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull &&
      shape.type !=
          ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh) {
    return IntersectPrimitiveShape(ray, shape, outDistance);
  }
  const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
      shape.rotationEulerDegrees.x * kDegreesToRadians,
      shape.rotationEulerDegrees.y * kDegreesToRadians,
      shape.rotationEulerDegrees.z * kDegreesToRadians);
  return IntersectGeometry(
      ray, shape.vertices, shape.indices,
      MATH::Mat4::TRS(shape.center, rotation, {1.0f, 1.0f, 1.0f}), outDistance);
}

bool IntersectModelCollisionSourceNodeExact(
    const ModelCollisionPointerRay &ray, const ModelCollisionPreviewNode &node,
    const ModelAsset &model, float &outDistance) noexcept {
  if (node.meshIndex < 0 ||
      static_cast<size_t>(node.meshIndex) >= model.meshes.size()) {
    return false;
  }
  float best = (std::numeric_limits<float>::max)();
  const MeshAsset &mesh = model.meshes[node.meshIndex];
  for (const MeshPrimitive &primitive : mesh.primitives) {
    const bool skinned = primitive.layout == VertexLayoutKind::SkinnedPNTTJW;
    const size_t vertexCount = skinned ? primitive.skinnedVertices.size()
                                       : primitive.staticVertices.size();
    if (vertexCount < 3u) {
      continue;
    }
    const size_t indexCount =
        primitive.indices.empty()
            ? vertexCount - vertexCount % 3u
            : primitive.indices.size() - primitive.indices.size() % 3u;
    for (size_t base = 0u; base < indexCount; base += 3u) {
      const uint32_t ia = primitive.indices.empty()
                              ? static_cast<uint32_t>(base)
                              : primitive.indices[base];
      const uint32_t ib = primitive.indices.empty()
                              ? static_cast<uint32_t>(base + 1u)
                              : primitive.indices[base + 1u];
      const uint32_t ic = primitive.indices.empty()
                              ? static_cast<uint32_t>(base + 2u)
                              : primitive.indices[base + 2u];
      if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
        continue;
      }
      const auto position = [&](uint32_t index) {
        return skinned ? primitive.skinnedVertices[index].position
                       : primitive.staticVertices[index].position;
      };
      float distance = 0.0f;
      if (IntersectRayTriangle(
              ray, TransformPoint(node.globalTransform, position(ia)),
              TransformPoint(node.globalTransform, position(ib)),
              TransformPoint(node.globalTransform, position(ic)), distance)) {
        best = (std::min)(best, distance);
      }
    }
  }
  if (best == (std::numeric_limits<float>::max)()) {
    return false;
  }
  outDistance = best;
  return true;
}

} // namespace HIKARI::EDITOR::DETAIL
