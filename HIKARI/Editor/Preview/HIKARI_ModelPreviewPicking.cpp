#include "Editor/Preview/HIKARI_ModelPreviewPicking.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Editor/Preview/HIKARI_ModelPreviewScene.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::EDITOR {
namespace {

bool Unproject(const MATH::Mat4 &inverseViewProjection, float x, float y,
               float z, MATH::Vec3 &outPoint) noexcept {
  const MATH::Vec4 world =
      inverseViewProjection.TransformPoint({x, y, z, 1.0f});
  if (!MATH::IsFinite(world) || std::abs(world.w) <= 1.0e-6f) {
    return false;
  }
  const float inverseW = 1.0f / world.w;
  outPoint = {world.x * inverseW, world.y * inverseW,
              world.z * inverseW};
  return MATH::IsFinite(outPoint);
}

MATH::Vec3 TransformPoint(const MATH::Mat4 &transform,
                          const MATH::Vec3 &point) noexcept {
  const MATH::Vec4 result =
      transform.TransformPoint({point.x, point.y, point.z, 1.0f});
  if (std::abs(result.w) <= 1.0e-7f) {
    return {result.x, result.y, result.z};
  }
  const float inverseW = 1.0f / result.w;
  return {result.x * inverseW, result.y * inverseW,
          result.z * inverseW};
}

bool IntersectBounds(const ModelPreviewPointerRay &ray,
                     const Bounds &bounds) noexcept {
  float nearDistance = 0.0f;
  float farDistance = (std::numeric_limits<float>::max)();
  const float origins[]{ray.origin.x, ray.origin.y, ray.origin.z};
  const float directions[]{ray.direction.x, ray.direction.y,
                           ray.direction.z};
  const float minimums[]{bounds.min.x, bounds.min.y, bounds.min.z};
  const float maximums[]{bounds.max.x, bounds.max.y, bounds.max.z};
  for (size_t axis = 0u; axis < 3u; ++axis) {
    if (std::abs(directions[axis]) <= 1.0e-7f) {
      if (origins[axis] < minimums[axis] ||
          origins[axis] > maximums[axis]) {
        return false;
      }
      continue;
    }
    const float inverseDirection = 1.0f / directions[axis];
    float a = (minimums[axis] - origins[axis]) * inverseDirection;
    float b = (maximums[axis] - origins[axis]) * inverseDirection;
    if (a > b) {
      std::swap(a, b);
    }
    nearDistance = (std::max)(nearDistance, a);
    farDistance = (std::min)(farDistance, b);
    if (nearDistance > farDistance) {
      return false;
    }
  }
  return true;
}

bool IntersectTriangle(const ModelPreviewPointerRay &ray,
                       const MATH::Vec3 &a, const MATH::Vec3 &b,
                       const MATH::Vec3 &c,
                       float &outDistance) noexcept {
  const MATH::Vec3 edgeA = b - a;
  const MATH::Vec3 edgeB = c - a;
  const MATH::Vec3 p = MATH::Cross(ray.direction, edgeB);
  const float determinant = MATH::Dot(edgeA, p);
  if (std::abs(determinant) <= 1.0e-7f) {
    return false;
  }
  const float inverseDeterminant = 1.0f / determinant;
  const MATH::Vec3 fromA = ray.origin - a;
  const float u = MATH::Dot(fromA, p) * inverseDeterminant;
  if (u < 0.0f || u > 1.0f) {
    return false;
  }
  const MATH::Vec3 q = MATH::Cross(fromA, edgeA);
  const float v = MATH::Dot(ray.direction, q) * inverseDeterminant;
  if (v < 0.0f || u + v > 1.0f) {
    return false;
  }
  const float distance = MATH::Dot(edgeB, q) * inverseDeterminant;
  if (!std::isfinite(distance) || distance < 0.0f) {
    return false;
  }
  outDistance = distance;
  return true;
}

} // namespace

bool BuildModelPreviewPointerRay(const Camera3D &camera,
                                 float normalizedViewportX,
                                 float normalizedViewportY,
                                 ModelPreviewPointerRay &outRay) noexcept {
  const float clipX = normalizedViewportX * 2.0f - 1.0f;
  const float clipY = 1.0f - normalizedViewportY * 2.0f;
  const MATH::Mat4 inverseViewProjection =
      MATH::Inverse(camera.GetViewProj());
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

ModelPreviewSurfacePick PickModelPreviewSurface(
    const ModelPreviewPointerRay &ray,
    const ModelPreviewScene &scene) noexcept {
  ModelPreviewSurfacePick result{};
  const ModelAsset *model = scene.GetModel();
  if (model == nullptr) {
    return result;
  }
  float bestDistance = (std::numeric_limits<float>::max)();
  for (const ModelPreviewNode &node : scene.GetSourceNodes()) {
    if (!IntersectBounds(ray, node.bounds) || node.meshIndex < 0 ||
        static_cast<size_t>(node.meshIndex) >= model->meshes.size()) {
      continue;
    }
    const MeshAsset &mesh = model->meshes[node.meshIndex];
    for (size_t primitiveIndex = 0u;
         primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
      const MeshPrimitive &primitive = mesh.primitives[primitiveIndex];
      const bool skinned =
          primitive.layout == VertexLayoutKind::SkinnedPNTTJW;
      const size_t vertexCount = skinned
          ? primitive.skinnedVertices.size()
          : primitive.staticVertices.size();
      const size_t indexCount = primitive.indices.empty()
          ? vertexCount - vertexCount % 3u
          : primitive.indices.size() - primitive.indices.size() % 3u;
      for (size_t base = 0u; base < indexCount; base += 3u) {
        const auto indexAt = [&](size_t offset) {
          return primitive.indices.empty()
              ? static_cast<uint32_t>(base + offset)
              : primitive.indices[base + offset];
        };
        const uint32_t ia = indexAt(0u);
        const uint32_t ib = indexAt(1u);
        const uint32_t ic = indexAt(2u);
        if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
          continue;
        }
        const auto position = [&](uint32_t index) {
          return skinned ? primitive.skinnedVertices[index].position
                         : primitive.staticVertices[index].position;
        };
        float distance = 0.0f;
        if (IntersectTriangle(
                ray, TransformPoint(node.globalTransform, position(ia)),
                TransformPoint(node.globalTransform, position(ib)),
                TransformPoint(node.globalTransform, position(ic)),
                distance) && distance < bestDistance) {
          bestDistance = distance;
          result.materialIndex = primitive.materialIndex;
          result.nodeIndex = node.nodeIndex;
          result.meshIndex = node.meshIndex;
          result.primitiveIndex = static_cast<uint32_t>(primitiveIndex);
          result.distance = distance;
          result.valid = true;
        }
      }
    }
  }
  return result;
}

} // namespace HIKARI::EDITOR
