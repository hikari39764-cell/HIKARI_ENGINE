#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionViewportOverlay.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegreesToRadians = 0.01745329251994329577f;

MATH::Vec3
ShapeDisplaySize(const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept {

  if (shape.type == ASSETS::COLLISION::CollisionGeometryShapeType::Sphere) {
    return {shape.radius * 2.0f, shape.radius * 2.0f, shape.radius * 2.0f};
  }
  if (shape.type == ASSETS::COLLISION::CollisionGeometryShapeType::Capsule) {
    return {shape.radius * 2.0f, shape.height, shape.radius * 2.0f};
  }
  if (shape.type == ASSETS::COLLISION::CollisionGeometryShapeType::ConvexHull ||
      shape.type ==
          ASSETS::COLLISION::CollisionGeometryShapeType::TriangleMesh) {
    Bounds bounds = BOUNDS::EmptyBounds();
    for (const MATH::Vec3 &vertex : shape.vertices) {
      BOUNDS::Encapsulate(bounds, vertex);
    }
    if (BOUNDS::IsUsable(bounds)) {
      return bounds.max - bounds.min;
    }
  }
  return shape.size;
}

#if defined(HIKARI_WITH_EDITOR)
bool ProjectPoint(const Camera3D &camera, const MATH::Vec3 &point,
                  const ImVec2 &origin, const ImVec2 &size, ImVec2 &out) {

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
  out.x = origin.x + (x * 0.5f + 0.5f) * size.x;
  out.y = origin.y + (0.5f - y * 0.5f) * size.y;
  return true;
}

MATH::Vec3 TransformLocalPoint(const MATH::Mat4 &matrix,
                               const MATH::Vec3 &point) {

  const MATH::Vec4 transformed =
      matrix.TransformPoint({point.x, point.y, point.z, 1.0f});
  return {transformed.x, transformed.y, transformed.z};
}

void DrawWorldLine(ImDrawList *drawList, const Camera3D &camera,
                   const ImVec2 &origin, const ImVec2 &size,
                   const MATH::Vec3 &first, const MATH::Vec3 &second,
                   ImU32 color, float thickness) {

  ImVec2 screenFirst{};
  ImVec2 screenSecond{};
  if (ProjectPoint(camera, first, origin, size, screenFirst) &&
      ProjectPoint(camera, second, origin, size, screenSecond)) {
    drawList->AddLine(screenFirst, screenSecond, color, thickness);
  }
}

void DrawLocalLine(ImDrawList *drawList, const Camera3D &camera,
                   const ImVec2 &origin, const ImVec2 &size,
                   const MATH::Mat4 &matrix, const MATH::Vec3 &first,
                   const MATH::Vec3 &second, ImU32 color, float thickness) {

  DrawWorldLine(drawList, camera, origin, size,
                TransformLocalPoint(matrix, first),
                TransformLocalPoint(matrix, second), color, thickness);
}

void DrawCirclePlane(ImDrawList *drawList, const Camera3D &camera,
                     const ImVec2 &origin, const ImVec2 &size,
                     const MATH::Mat4 &matrix, int plane, float radius,
                     float offset, ImU32 color, float thickness) {

  constexpr int kSegments = 40;
  for (int segment = 0; segment < kSegments; ++segment) {
    const float firstAngle = static_cast<float>(segment) /
                             static_cast<float>(kSegments) * 6.28318530718f;
    const float secondAngle = static_cast<float>(segment + 1) /
                              static_cast<float>(kSegments) * 6.28318530718f;
    MATH::Vec3 first{};
    MATH::Vec3 second{};
    if (plane == 0) {
      first = {offset, std::cos(firstAngle) * radius,
               std::sin(firstAngle) * radius};
      second = {offset, std::cos(secondAngle) * radius,
                std::sin(secondAngle) * radius};
    } else if (plane == 1) {
      first = {std::cos(firstAngle) * radius, offset,
               std::sin(firstAngle) * radius};
      second = {std::cos(secondAngle) * radius, offset,
                std::sin(secondAngle) * radius};
    } else {
      first = {std::cos(firstAngle) * radius, std::sin(firstAngle) * radius,
               offset};
      second = {std::cos(secondAngle) * radius, std::sin(secondAngle) * radius,
                offset};
    }
    DrawLocalLine(drawList, camera, origin, size, matrix, first, second, color,
                  thickness);
  }
}
#endif
} // namespace

TransformData BuildModelCollisionShapeTransform(
    const ASSETS::COLLISION::ModelCollisionShape &shape) noexcept {

  TransformData transform{};
  transform.position = shape.center;
  transform.rotationEulerDeg = shape.rotationEulerDegrees;
  transform.scale = ShapeDisplaySize(shape);
  return transform;
}

void DrawModelCollisionShapeOverlay(
    ImDrawList *drawList, const Camera3D &camera, float viewportX,
    float viewportY, float viewportWidth, float viewportHeight,
    const ASSETS::COLLISION::ModelCollisionShape &shape, bool selected,
    bool hovered, bool locked, float opacity) {

#if defined(HIKARI_WITH_EDITOR)
  if (drawList == nullptr || viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
    return;
  }
  const ImVec2 origin{viewportX, viewportY};
  const ImVec2 size{viewportWidth, viewportHeight};
  ImU32 color = !shape.enabled    ? IM_COL32(110, 120, 132, 150)
                : selected        ? IM_COL32(255, 194, 72, 255)
                : hovered         ? IM_COL32(255, 226, 132, 245)
                : locked          ? IM_COL32(112, 176, 232, 205)
                : shape.generated ? IM_COL32(88, 207, 239, 220)
                                  : IM_COL32(89, 235, 151, 230);
  opacity = (std::clamp)(opacity, 0.05f, 1.0f);
  const uint32_t alpha = static_cast<uint32_t>(
      static_cast<float>((color >> 24u) & 0xffu) * opacity);
  color = (color & 0x00ffffffu) | (alpha << 24u);
  const float thickness = selected  ? 2.5f
                          : hovered ? 2.0f
                                    : 1.0f + 0.5f * opacity;
  const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
      shape.rotationEulerDegrees.x * kDegreesToRadians,
      shape.rotationEulerDegrees.y * kDegreesToRadians,
      shape.rotationEulerDegrees.z * kDegreesToRadians);
  const MATH::Mat4 matrix =
      MATH::Mat4::TRS(shape.center, rotation, {1.0f, 1.0f, 1.0f});

  if (shape.type == ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
    const MATH::Vec3 half = shape.size * 0.5f;
    const std::array<MATH::Vec3, 8> corners{
        MATH::Vec3{-half.x, -half.y, -half.z},
        MATH::Vec3{half.x, -half.y, -half.z},
        MATH::Vec3{-half.x, half.y, -half.z},
        MATH::Vec3{half.x, half.y, -half.z},
        MATH::Vec3{-half.x, -half.y, half.z},
        MATH::Vec3{half.x, -half.y, half.z},
        MATH::Vec3{-half.x, half.y, half.z},
        MATH::Vec3{half.x, half.y, half.z},
    };
    constexpr std::array<std::array<int, 2>, 12> edges{{{0, 1},
                                                        {0, 2},
                                                        {1, 3},
                                                        {2, 3},
                                                        {4, 5},
                                                        {4, 6},
                                                        {5, 7},
                                                        {6, 7},
                                                        {0, 4},
                                                        {1, 5},
                                                        {2, 6},
                                                        {3, 7}}};
    for (const auto &edge : edges) {
      DrawLocalLine(drawList, camera, origin, size, matrix,
                    corners[static_cast<size_t>(edge[0])],
                    corners[static_cast<size_t>(edge[1])], color, thickness);
    }
  } else if (shape.type ==
             ASSETS::COLLISION::CollisionGeometryShapeType::Sphere) {
    DrawCirclePlane(drawList, camera, origin, size, matrix, 0, shape.radius,
                    0.0f, color, thickness);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 1, shape.radius,
                    0.0f, color, thickness);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 2, shape.radius,
                    0.0f, color, thickness);
  } else if (shape.type ==
             ASSETS::COLLISION::CollisionGeometryShapeType::Capsule) {
    const float halfLine = (std::max)(0.0f, shape.height * 0.5f - shape.radius);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 1, shape.radius,
                    -halfLine, color, thickness);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 1, shape.radius,
                    halfLine, color, thickness);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 0, shape.radius,
                    -halfLine, color, thickness);
    DrawCirclePlane(drawList, camera, origin, size, matrix, 0, shape.radius,
                    halfLine, color, thickness);
    for (int sideIndex = 0; sideIndex < 4; ++sideIndex) {
      const float angle = static_cast<float>(sideIndex) * 1.57079632679f;
      const MATH::Vec3 side{std::cos(angle) * shape.radius, 0.0f,
                            std::sin(angle) * shape.radius};
      DrawLocalLine(drawList, camera, origin, size, matrix,
                    {side.x, -halfLine, side.z}, {side.x, halfLine, side.z},
                    color, thickness);
    }
  } else if (!shape.indices.empty()) {
    const size_t triangleCount = shape.indices.size() / 3u;
    const size_t step =
        (std::max)(static_cast<size_t>(1u), triangleCount / 2048u);
    for (size_t triangle = 0u; triangle < triangleCount; triangle += step) {
      const size_t offset = triangle * 3u;
      const uint32_t first = shape.indices[offset];
      const uint32_t second = shape.indices[offset + 1u];
      const uint32_t third = shape.indices[offset + 2u];
      if (first >= shape.vertices.size() || second >= shape.vertices.size() ||
          third >= shape.vertices.size()) {
        continue;
      }
      DrawLocalLine(drawList, camera, origin, size, matrix,
                    shape.vertices[first], shape.vertices[second], color,
                    thickness);
      DrawLocalLine(drawList, camera, origin, size, matrix,
                    shape.vertices[second], shape.vertices[third], color,
                    thickness);
      DrawLocalLine(drawList, camera, origin, size, matrix,
                    shape.vertices[third], shape.vertices[first], color,
                    thickness);
    }
  } else {
    Bounds bounds = BOUNDS::EmptyBounds();
    for (const MATH::Vec3 &vertex : shape.vertices) {
      BOUNDS::Encapsulate(bounds, vertex);
    }
    if (BOUNDS::IsUsable(bounds)) {
      const std::array<MATH::Vec3, 8> corners{
          MATH::Vec3{bounds.min.x, bounds.min.y, bounds.min.z},
          MATH::Vec3{bounds.max.x, bounds.min.y, bounds.min.z},
          MATH::Vec3{bounds.min.x, bounds.max.y, bounds.min.z},
          MATH::Vec3{bounds.max.x, bounds.max.y, bounds.min.z},
          MATH::Vec3{bounds.min.x, bounds.min.y, bounds.max.z},
          MATH::Vec3{bounds.max.x, bounds.min.y, bounds.max.z},
          MATH::Vec3{bounds.min.x, bounds.max.y, bounds.max.z},
          MATH::Vec3{bounds.max.x, bounds.max.y, bounds.max.z},
      };
      constexpr std::array<std::array<int, 2>, 12> edges{{{0, 1},
                                                          {0, 2},
                                                          {1, 3},
                                                          {2, 3},
                                                          {4, 5},
                                                          {4, 6},
                                                          {5, 7},
                                                          {6, 7},
                                                          {0, 4},
                                                          {1, 5},
                                                          {2, 6},
                                                          {3, 7}}};
      for (const auto &edge : edges) {
        DrawLocalLine(drawList, camera, origin, size, matrix,
                      corners[static_cast<size_t>(edge[0])],
                      corners[static_cast<size_t>(edge[1])], color, thickness);
      }
    }
  }
  ImVec2 center{};
  if (ProjectPoint(camera, shape.center, origin, size, center)) {
    drawList->AddCircleFilled(center, selected ? 4.0f : 2.5f, color);
  }
#else
  (void)drawList;
  (void)camera;
  (void)viewportX;
  (void)viewportY;
  (void)viewportWidth;
  (void)viewportHeight;
  (void)shape;
  (void)selected;
  (void)hovered;
  (void)locked;
#endif
}

void DrawModelCollisionBoundsOverlay(ImDrawList *drawList,
                                     const Camera3D &camera, float viewportX,
                                     float viewportY, float viewportWidth,
                                     float viewportHeight, const Bounds &bounds,
                                     uint32_t color, float thickness) {

#if defined(HIKARI_WITH_EDITOR)
  if (drawList == nullptr || !BOUNDS::IsUsable(bounds)) {
    return;
  }
  const ImVec2 origin{viewportX, viewportY};
  const ImVec2 size{viewportWidth, viewportHeight};
  const std::array<MATH::Vec3, 8> corners{
      MATH::Vec3{bounds.min.x, bounds.min.y, bounds.min.z},
      MATH::Vec3{bounds.max.x, bounds.min.y, bounds.min.z},
      MATH::Vec3{bounds.min.x, bounds.max.y, bounds.min.z},
      MATH::Vec3{bounds.max.x, bounds.max.y, bounds.min.z},
      MATH::Vec3{bounds.min.x, bounds.min.y, bounds.max.z},
      MATH::Vec3{bounds.max.x, bounds.min.y, bounds.max.z},
      MATH::Vec3{bounds.min.x, bounds.max.y, bounds.max.z},
      MATH::Vec3{bounds.max.x, bounds.max.y, bounds.max.z},
  };
  constexpr std::array<std::array<int, 2>, 12> edges{{{0, 1},
                                                      {0, 2},
                                                      {1, 3},
                                                      {2, 3},
                                                      {4, 5},
                                                      {4, 6},
                                                      {5, 7},
                                                      {6, 7},
                                                      {0, 4},
                                                      {1, 5},
                                                      {2, 6},
                                                      {3, 7}}};
  for (const auto &edge : edges) {
    DrawWorldLine(drawList, camera, origin, size,
                  corners[static_cast<size_t>(edge[0])],
                  corners[static_cast<size_t>(edge[1])], color, thickness);
  }
#else
  (void)drawList;
  (void)camera;
  (void)viewportX;
  (void)viewportY;
  (void)viewportWidth;
  (void)viewportHeight;
  (void)bounds;
  (void)color;
  (void)thickness;
#endif
}

} // namespace HIKARI::EDITOR
