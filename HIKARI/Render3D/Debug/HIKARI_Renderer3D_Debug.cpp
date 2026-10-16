#include "HIKARI_Renderer3D_Debug.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "Render3D/Debug/Internal/HIKARI_DebugPrimitiveRendererBackend.h"

namespace HIKARI::RENDERER3D::DEBUG {
namespace {
constexpr std::array<std::array<int, 2>, 12> kBoxEdges{{
    {0, 1},
    {1, 2},
    {2, 3},
    {3, 0},
    {4, 5},
    {5, 6},
    {6, 7},
    {7, 4},
    {0, 4},
    {1, 5},
    {2, 6},
    {3, 7},
}};
constexpr std::array<int, 36> kBoxTriangleIndices{{
    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
}};

std::vector<Box3D> g_boxes;
std::vector<Stroke3D> g_strokes;
std::vector<PointMarker3D> g_pointMarkers;
std::vector<Axis3D> g_axes;
std::vector<Grid3D> g_grids;
std::vector<WireSphere3D> g_wireSpheres;
std::vector<WireCone3D> g_wireCones;
std::vector<Stroke3D> g_expandedStrokes;
std::vector<Stroke3D> g_depthStrokes;
std::vector<Stroke3D> g_xrayStrokes;
std::vector<INTERNAL::SolidVertex3D> g_solidVertices;
DebugRendererFrameStats g_submitStats{};
DebugRendererFrameStats g_frameStats{};
#if defined(HIKARI_WITH_EDITOR)
bool g_renderingEditorView = false;
#endif

void PushStroke(const MATH::Vec3& from, const MATH::Vec3& to, uint32_t rgba, DebugDepthMode depthMode,
                float thicknessPixels) {
    Stroke3D stroke{};
    stroke.from = from;
    stroke.to = to;
    stroke.rgba = rgba;
    stroke.depthMode = depthMode;
    stroke.thicknessPixels = (std::max)(thicknessPixels, 0.5f);
    g_expandedStrokes.push_back(stroke);
}

size_t EstimateStrokeCount() {
    size_t count = g_strokes.size();
    count += g_axes.size() * 3u;
    count += g_boxes.size() * kBoxEdges.size();
    count += g_pointMarkers.size() * 3u;
    for (const Grid3D& grid : g_grids) {
        const int halfCount = (std::max)(grid.halfCount, 1);
        count += static_cast<size_t>((halfCount * 2 + 1) * 2);
    }
    for (const WireSphere3D& sphere : g_wireSpheres) {
        count += static_cast<size_t>((std::max)(sphere.segments, 8) * 3);
    }
    for (const WireCone3D& cone : g_wireCones) {
        count += static_cast<size_t>((std::max)(cone.segments, 8) + (cone.drawRadials ? 4 : 0));
    }
    return count;
}

std::array<MATH::Vec3, 8> BuildBoxCorners(const Box3D& box) {
    const MATH::Vec3 extent = box.size * 0.5f;
    const std::array<MATH::Vec3, 8> local{{
        {-extent.x, -extent.y, -extent.z},
        {extent.x, -extent.y, -extent.z},
        {extent.x, extent.y, -extent.z},
        {-extent.x, extent.y, -extent.z},
        {-extent.x, -extent.y, extent.z},
        {extent.x, -extent.y, extent.z},
        {extent.x, extent.y, extent.z},
        {-extent.x, extent.y, extent.z},
    }};
    const MATH::Mat4 world = box.transform.GetWorldMatrix();
    std::array<MATH::Vec3, 8> corners{};
    for (size_t index = 0; index < corners.size(); ++index) {
        corners[index] = MATH::TransformPoint(world, local[index]);
    }
    return corners;
}

void ExpandAxis(const Axis3D& axis) {
    const MATH::Mat4 world = axis.transform.GetWorldMatrix();
    const MATH::Vec3 origin = MATH::TransformPoint(world, {});
    PushStroke(origin, MATH::TransformPoint(world, {axis.length, 0.0f, 0.0f}), axis.xColor, axis.depthMode,
               axis.thicknessPixels);
    PushStroke(origin, MATH::TransformPoint(world, {0.0f, axis.length, 0.0f}), axis.yColor, axis.depthMode,
               axis.thicknessPixels);
    PushStroke(origin, MATH::TransformPoint(world, {0.0f, 0.0f, axis.length}), axis.zColor, axis.depthMode,
               axis.thicknessPixels);
}

void ExpandGrid(const Grid3D& grid) {
    const int halfCount = (std::max)(grid.halfCount, 1);
    const float spacing = grid.spacing > 0.0f ? grid.spacing : 1.0f;
    const float extent = static_cast<float>(halfCount) * spacing;
    const int majorEvery = (std::max)(grid.majorLineEvery, 0);
    if ((grid.fillRgba & 0xFFu) != 0u) {
        const MATH::Vec4 fillColor = DecodeRgba(grid.fillRgba);
        const MATH::Vec3 corners[4] = {
            {-extent, 0.0f, -extent},
            {extent, 0.0f, -extent},
            {extent, 0.0f, extent},
            {-extent, 0.0f, extent},
        };
        g_solidVertices.push_back({corners[0], fillColor});
        g_solidVertices.push_back({corners[1], fillColor});
        g_solidVertices.push_back({corners[2], fillColor});
        g_solidVertices.push_back({corners[0], fillColor});
        g_solidVertices.push_back({corners[2], fillColor});
        g_solidVertices.push_back({corners[3], fillColor});
    }
    for (int index = -halfCount; index <= halfCount; ++index) {
        const float position = static_cast<float>(index) * spacing;
        const bool isMajor = majorEvery > 0 && (index % majorEvery) == 0;
        const unsigned int lineRgba = isMajor ? grid.majorRgba : grid.rgba;
        PushStroke({-extent, 0.0f, position}, {extent, 0.0f, position}, lineRgba, grid.depthMode, grid.thicknessPixels);
        PushStroke({position, 0.0f, -extent}, {position, 0.0f, extent}, lineRgba, grid.depthMode, grid.thicknessPixels);
    }
}

void ExpandBox(const Box3D& box) {
    const std::array<MATH::Vec3, 8> corners = BuildBoxCorners(box);
    for (const auto& edge : kBoxEdges) {
        PushStroke(corners[edge[0]], corners[edge[1]], box.outlineRgba, box.depthMode, box.outlineThicknessPixels);
    }
    if ((box.fillRgba & 0xFFu) == 0u) {
        return;
    }
    const MATH::Vec4 fillColor = DecodeRgba(box.fillRgba);
    for (int index : kBoxTriangleIndices) {
        g_solidVertices.push_back({corners[static_cast<size_t>(index)], fillColor});
    }
}

void ExpandPointMarker(const PointMarker3D& marker) {
    const float radius = (std::max)(marker.radius, 0.001f);
    PushStroke(marker.position - MATH::Vec3{radius, 0.0f, 0.0f}, marker.position + MATH::Vec3{radius, 0.0f, 0.0f},
               marker.rgba, marker.depthMode, marker.thicknessPixels);
    PushStroke(marker.position - MATH::Vec3{0.0f, radius, 0.0f}, marker.position + MATH::Vec3{0.0f, radius, 0.0f},
               marker.rgba, marker.depthMode, marker.thicknessPixels);
    PushStroke(marker.position - MATH::Vec3{0.0f, 0.0f, radius}, marker.position + MATH::Vec3{0.0f, 0.0f, radius},
               marker.rgba, marker.depthMode, marker.thicknessPixels);
}

void ExpandWireSphere(const WireSphere3D& sphere) {
    const int segments = (std::max)(sphere.segments, 8);
    const float radius = (std::max)(sphere.radius, 0.001f);
    for (int axis = 0; axis < 3; ++axis) {
        for (int index = 0; index < segments; ++index) {
            const float angle0 =
                (static_cast<float>(index) / static_cast<float>(segments)) * (2.0f * std::numbers::pi_v<float>);
            const float angle1 =
                (static_cast<float>(index + 1) / static_cast<float>(segments)) * (2.0f * std::numbers::pi_v<float>);
            MATH::Vec3 point0 = sphere.center;
            MATH::Vec3 point1 = sphere.center;
            if (axis == 0) {
                point0.y += std::cos(angle0) * radius;
                point0.z += std::sin(angle0) * radius;
                point1.y += std::cos(angle1) * radius;
                point1.z += std::sin(angle1) * radius;
            } else if (axis == 1) {
                point0.x += std::cos(angle0) * radius;
                point0.z += std::sin(angle0) * radius;
                point1.x += std::cos(angle1) * radius;
                point1.z += std::sin(angle1) * radius;
            } else {
                point0.x += std::cos(angle0) * radius;
                point0.y += std::sin(angle0) * radius;
                point1.x += std::cos(angle1) * radius;
                point1.y += std::sin(angle1) * radius;
            }
            PushStroke(point0, point1, sphere.rgba, sphere.depthMode, sphere.thicknessPixels);
        }
    }
}

void ExpandWireCone(const WireCone3D& cone) {
    const int segments = (std::max)(cone.segments, 8);
    MATH::Vec3 direction = MATH::Normalize(cone.direction);
    if (MATH::Length(direction) <= 1e-6f) {
        direction = {0.0f, -1.0f, 0.0f};
    }
    MATH::Vec3 right = MATH::Cross(direction, {0.0f, 1.0f, 0.0f});
    if (MATH::Length(right) <= 1e-6f) {
        right = MATH::Cross(direction, {1.0f, 0.0f, 0.0f});
    }
    right = MATH::Normalize(right);
    const MATH::Vec3 up = MATH::Normalize(MATH::Cross(right, direction));
    const float length = (std::max)(cone.length, 0.001f);
    const float angleDegrees = std::clamp(cone.angleDegrees, 0.0f, 89.0f);
    const float radius = std::tan(angleDegrees * std::numbers::pi_v<float> / 180.0f) * length;
    const MATH::Vec3 center = cone.origin + direction * length;

    MATH::Vec3 previous{};
    for (int index = 0; index <= segments; ++index) {
        const float angle =
            (static_cast<float>(index % segments) / static_cast<float>(segments)) * (2.0f * std::numbers::pi_v<float>);
        const MATH::Vec3 point = center + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
        if (index > 0) {
            PushStroke(previous, point, cone.rgba, cone.depthMode, cone.thicknessPixels);
        }
        previous = point;
        if (cone.drawRadials &&
            (index == 0 || index == segments / 4 || index == segments / 2 || index == (segments * 3) / 4)) {
            PushStroke(cone.origin, point, cone.rgba, cone.depthMode, cone.thicknessPixels);
        }
    }
}

void ExpandPrimitives() {
    g_expandedStrokes.clear();
    g_solidVertices.clear();
    g_expandedStrokes.reserve(EstimateStrokeCount());
    g_solidVertices.reserve(g_boxes.size() * 36u + g_grids.size() * 6u);
    g_expandedStrokes.insert(g_expandedStrokes.end(), g_strokes.begin(), g_strokes.end());
    for (const Axis3D& axis : g_axes) {
        ExpandAxis(axis);
    }
    for (const Grid3D& grid : g_grids) {
        ExpandGrid(grid);
    }
    for (const Box3D& box : g_boxes) {
        ExpandBox(box);
    }
    for (const PointMarker3D& marker : g_pointMarkers) {
        ExpandPointMarker(marker);
    }
    for (const WireSphere3D& sphere : g_wireSpheres) {
        ExpandWireSphere(sphere);
    }
    for (const WireCone3D& cone : g_wireCones) {
        ExpandWireCone(cone);
    }
}

bool ShouldWriteFrameStats() {
#if defined(HIKARI_WITH_EDITOR)
    return !g_renderingEditorView;
#else
    return true;
#endif
}
} // namespace

void Reset() {
    g_boxes.clear();
    g_strokes.clear();
    g_pointMarkers.clear();
    g_axes.clear();
    g_grids.clear();
    g_wireSpheres.clear();
    g_wireCones.clear();
    g_submitStats = {};
}

void SubmitBox3D(const Box3D& box) {
    g_boxes.push_back(box);
    ++g_submitStats.boxCount;
}

void SubmitStroke3D(const Stroke3D& stroke) {
    g_strokes.push_back(stroke);
    ++g_submitStats.submittedStrokeCount;
    if (stroke.depthMode == DebugDepthMode::XRay) {
        ++g_submitStats.submittedXRayStrokeCount;
    }
}

void SubmitPointMarker3D(const PointMarker3D& marker) {
    g_pointMarkers.push_back(marker);
    ++g_submitStats.pointMarkerCount;
}

void SubmitAxis3D(const Axis3D& axis) {
    g_axes.push_back(axis);
    ++g_submitStats.axisCount;
}

void SubmitGrid3D(const Grid3D& grid) {
    g_grids.push_back(grid);
    ++g_submitStats.gridCount;
}

void SubmitWireSphere3D(const WireSphere3D& sphere) {
    g_wireSpheres.push_back(sphere);
    ++g_submitStats.wireSphereCount;
}

void SubmitWireCone3D(const WireCone3D& cone) {
    g_wireCones.push_back(cone);
    ++g_submitStats.wireConeCount;
}

void AccumulateLightProbeVolumeGizmoStats(uint32_t totalPointCount, uint32_t drawnPointCount, uint32_t mode,
                                          bool capped) {
    const auto saturatingAdd = [](uint32_t left, uint32_t right) {
        return static_cast<uint32_t>((std::min)(static_cast<uint64_t>(left) + right,
                                                static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
    };
    g_submitStats.lightProbeGizmoTotalPointCount =
        saturatingAdd(g_submitStats.lightProbeGizmoTotalPointCount, totalPointCount);
    g_submitStats.lightProbeGizmoDrawnPointCount =
        saturatingAdd(g_submitStats.lightProbeGizmoDrawnPointCount, drawnPointCount);
    g_submitStats.lightProbeGizmoMode = (std::max)(g_submitStats.lightProbeGizmoMode, mode);
    g_submitStats.lightProbeGizmoCapped = g_submitStats.lightProbeGizmoCapped || capped;
}

const DebugRendererFrameStats& GetDebugRendererFrameStats() { return g_frameStats; }

void RenderAll(const Camera3D& camera, float screenWidth, float screenHeight) {
    ExpandPrimitives();
    g_depthStrokes.clear();
    g_xrayStrokes.clear();
    g_depthStrokes.reserve(g_expandedStrokes.size());
    g_xrayStrokes.reserve(g_expandedStrokes.size());
    for (const Stroke3D& stroke : g_expandedStrokes) {
        (stroke.depthMode == DebugDepthMode::XRay ? g_xrayStrokes : g_depthStrokes).push_back(stroke);
    }
    if (ShouldWriteFrameStats()) {
        g_frameStats = g_submitStats;
        g_frameStats.expandedStrokeCount = g_expandedStrokes.size();
        g_frameStats.depthTestStrokeCount = g_depthStrokes.size();
        g_frameStats.xrayStrokeCount = g_xrayStrokes.size();
        g_frameStats.solidTriangleCount = g_solidVertices.size() / 3u;
    }
#if defined(HIKARI_WITH_EDITOR)
    const bool editorView = g_renderingEditorView;
#else
    constexpr bool editorView = false;
#endif
    INTERNAL::RenderPrimitiveBatches(camera, screenWidth, screenHeight, editorView, g_xrayStrokes, g_depthStrokes,
                                     g_solidVertices);
}

#if defined(HIKARI_WITH_EDITOR)
void RenderAllForView(RENDER3D::RenderViewId viewId, const Camera3D& camera, float screenWidth, float screenHeight) {
    if (viewId == RENDER3D::kPrimaryRenderViewId) {
        RenderAll(camera, screenWidth, screenHeight);
        return;
    }
    g_renderingEditorView = true;
    RenderAll(camera, screenWidth, screenHeight);
    g_renderingEditorView = false;
}

void ShutdownEditorViewResources() {
    g_renderingEditorView = false;
    INTERNAL::ShutdownEditorViewResources();
}
#endif

} // namespace HIKARI::RENDERER3D::DEBUG
