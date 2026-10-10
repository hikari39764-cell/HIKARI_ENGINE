#include "Editor/Workspaces/LookDev/HIKARI_LookDevShapeHandles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

constexpr float kMinimumWorldSize = 0.05f;

bool IsRound(ProceduralMeshKind kind) {
    return kind == ProceduralMeshKind::Cylinder || kind == ProceduralMeshKind::Capsule;
}

bool IsFlat(ProceduralMeshKind kind) {
    return kind == ProceduralMeshKind::Plane || kind == ProceduralMeshKind::GridPlane;
}

} // namespace

LookDevBoxFrame LookDevFrameFromWorld(const MATH::Mat4& world, const MATH::Vec3& localCenter) {
    const MATH::Vec4 center = world.TransformPoint({localCenter.x, localCenter.y, localCenter.z, 1.0f});
    return {{center.x, center.y, center.z},
            {MATH::Vec3{world.m[0][0], world.m[0][1], world.m[0][2]},
             MATH::Vec3{world.m[1][0], world.m[1][1], world.m[1][2]},
             MATH::Vec3{world.m[2][0], world.m[2][1], world.m[2][2]}}};
}

std::vector<LookDevShapeHandle> BuildLookDevBoxHandles(const LookDevBoxFrame& frame, const MATH::Vec3& extent,
                                                       uint8_t axisMask, uint8_t symmetricMask) {
    std::vector<LookDevShapeHandle> handles;
    for (uint8_t axis = 0; axis < 3; ++axis) {
        if ((axisMask & (1u << axis)) == 0u) {
            continue;
        }
        const bool symmetric = (symmetricMask & (1u << axis)) != 0u;
        for (const float sign : {1.0f, -1.0f}) {
            if (symmetric && sign < 0.0f) {
                continue;
            }
            handles.push_back(
                {axis, sign, symmetric, frame.center + frame.axes[axis] * (sign * LookDevAxis(extent, axis) * 0.5f)});
        }
    }
    return handles;
}

std::optional<LookDevBoxResize> ResizeLookDevBox(const LookDevBoxFrame& frame, float startExtent,
                                                 const LookDevShapeHandle& handle, const EditorPointerRay& ray,
                                                 float grid) {
    const MATH::Vec3& row = frame.axes[handle.axis];
    const float scale = MATH::Length(row);
    if (!(scale > 1.0e-6f)) {
        return std::nullopt;
    }
    const MATH::Vec3 axis = row * (handle.sign / scale);
    const MATH::Vec3 face = frame.center + axis * (startExtent * scale * 0.5f);
    float along = 0.0f;
    if (!ClosestEditorLineParameter(ray, face, axis, along)) {
        return std::nullopt;
    }
    // 対称なら面の動きの倍だけ寸法が変わる。
    float worldSize = startExtent * scale + along * (handle.symmetric ? 2.0f : 1.0f);
    const float step = grid > 0.0f ? grid : kMinimumWorldSize;
    worldSize = (std::max)(kMinimumWorldSize, std::round(worldSize / step) * step);
    LookDevBoxResize resize{};
    resize.extent = worldSize / scale;
    if (!handle.symmetric) {
        resize.localShift = handle.sign * (resize.extent - startExtent) * 0.5f;
        resize.worldShift = axis * ((worldSize - startExtent * scale) * 0.5f);
    }
    return resize;
}

#if defined(HIKARI_WITH_EDITOR)
void DrawLookDevBox(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view, const LookDevBoxFrame& frame,
                    const MATH::Vec3& extent, const MATH::Vec3& worldShift, uint32_t color) {
    const MATH::Vec3 center = frame.center + worldShift;
    std::array<MATH::Vec2, 8> corners{};
    for (int index = 0; index < 8; ++index) {
        const MATH::Vec3 corner = center + frame.axes[0] * ((index & 1) ? extent.x * 0.5f : -extent.x * 0.5f) +
                                  frame.axes[1] * ((index & 2) ? extent.y * 0.5f : -extent.y * 0.5f) +
                                  frame.axes[2] * ((index & 4) ? extent.z * 0.5f : -extent.z * 0.5f);
        if (!ProjectLookDevPoint(camera, view, corner, corners[static_cast<size_t>(index)])) {
            return;
        }
    }
    constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                   {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    drawList.PushClipRect({view.x, view.y}, {view.x + view.width, view.y + view.height}, true);
    for (const auto& edge : kEdges) {
        const MATH::Vec2& a = corners[static_cast<size_t>(edge[0])];
        const MATH::Vec2& b = corners[static_cast<size_t>(edge[1])];
        drawList.AddLine({a.x, a.y}, {b.x, b.y}, color, 1.5f);
    }
    drawList.PopClipRect();
}

void DrawLookDevCylinder(ImDrawList& drawList, const Camera3D& camera, const LookDevView& view,
                         const LookDevBoxFrame& frame, int axis, float radius, float length, uint32_t color) {
    constexpr int kSegments = 40;
    const MATH::Vec3& along = frame.axes[static_cast<size_t>(axis)];
    const MATH::Vec3& u = frame.axes[static_cast<size_t>((axis + 1) % 3)];
    const MATH::Vec3& v = frame.axes[static_cast<size_t>((axis + 2) % 3)];
    const auto point = [&](float end, float angle) {
        return frame.center + along * (end * length * 0.5f) + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    };
    const auto line = [&](const MATH::Vec3& from, const MATH::Vec3& to) {
        MATH::Vec2 a{};
        MATH::Vec2 b{};
        if (ProjectLookDevPoint(camera, view, from, a) && ProjectLookDevPoint(camera, view, to, b)) {
            drawList.AddLine({a.x, a.y}, {b.x, b.y}, color, 1.5f);
        }
    };
    drawList.PushClipRect({view.x, view.y}, {view.x + view.width, view.y + view.height}, true);
    for (const float end : {-1.0f, 1.0f}) {
        for (int segment = 0; segment < kSegments; ++segment) {
            const float a0 = 6.2831853f * static_cast<float>(segment) / kSegments;
            const float a1 = 6.2831853f * static_cast<float>(segment + 1) / kSegments;
            line(point(end, a0), point(end, a1));
        }
    }
    for (int side = 0; side < 4; ++side) {
        const float angle = 1.5707963f * static_cast<float>(side);
        line(point(-1.0f, angle), point(1.0f, angle));
    }
    drawList.PopClipRect();
}
#else
void DrawLookDevBox(ImDrawList&, const Camera3D&, const LookDevView&, const LookDevBoxFrame&, const MATH::Vec3&,
                    const MATH::Vec3&, uint32_t) {}
void DrawLookDevCylinder(ImDrawList&, const Camera3D&, const LookDevView&, const LookDevBoxFrame&, int, float, float,
                         uint32_t) {}
#endif

std::vector<LookDevShapeHandle> BuildLookDevShapeHandles(const ProceduralMeshSettings& settings,
                                                         const MATH::Mat4& world) {
    const LookDevBoxFrame frame = LookDevFrameFromWorld(world);
    const MATH::Vec3 extent = GetProceduralMeshExtent(settings);
    if (settings.kind == ProceduralMeshKind::Sphere) {
        return BuildLookDevBoxHandles(frame, extent, 0b001u, 0b001u);
    }
    if (IsRound(settings.kind)) {
        return BuildLookDevBoxHandles(frame, extent, 0b011u, 0b001u);
    }
    return BuildLookDevBoxHandles(frame, extent, IsFlat(settings.kind) ? 0b101u : 0b111u, 0u);
}

ProceduralMeshSettings WithLookDevShapeExtent(ProceduralMeshSettings settings, int axis, float value) {
    if (settings.kind == ProceduralMeshKind::Sphere || (IsRound(settings.kind) && axis != 1) || axis == 0) {
        settings.width = value;
    } else if (axis == 1) {
        settings.height = value;
    } else {
        settings.depth = value;
    }
    return SanitizeProceduralMeshSettings(settings);
}

std::string FormatLookDevSize(const LookDevBoxFrame& frame, const MATH::Vec3& extent, bool flat) {
    char text[96]{};
    const float x = extent.x * MATH::Length(frame.axes[0]);
    const float y = extent.y * MATH::Length(frame.axes[1]);
    const float z = extent.z * MATH::Length(frame.axes[2]);
    if (flat) {
        std::snprintf(text, sizeof(text), "%.2f x %.2f m", x, z);
    } else {
        std::snprintf(text, sizeof(text), "%.2f x %.2f x %.2f m", x, y, z);
    }
    return text;
}

int LookDevThinAxis(const ProceduralMeshSettings& settings, const MATH::Vec3& scale) noexcept {
    const std::array<float, 3> world{settings.width * scale.x, settings.height * scale.y, settings.depth * scale.z};
    return static_cast<int>(std::min_element(world.begin(), world.end()) - world.begin());
}

ProceduralMeshCut BuildLookDevOpening(const ProceduralMeshSettings& settings, const MATH::Vec3& scale,
                                      LookDevOpeningKind kind) noexcept {
    const MATH::Vec3 extent{settings.width, settings.height, settings.depth};
    const int thin = LookDevThinAxis(settings, scale);
    // 世界の大きさ（m）を局所へ。
    const auto local = [&](float meters, int axis, float limit) {
        return (std::min)(meters / (std::max)(LookDevAxis(scale, axis), 1.0e-4f), LookDevAxis(extent, axis) * limit);
    };
    ProceduralMeshCut cut{};
    LookDevAxis(cut.size, thin) = LookDevAxis(extent, thin) + 0.2f;
    if (kind == LookDevOpeningKind::RoundHole || kind == LookDevOpeningKind::RoundWindow) {
        const bool window = kind == LookDevOpeningKind::RoundWindow && thin != 1;
        const float diameter = (std::min)(local(window ? 1.0f : 1.6f, (thin + 1) % 3, 0.6f),
                                          local(window ? 1.0f : 1.6f, (thin + 2) % 3, 0.6f));
        LookDevAxis(cut.size, (thin + 1) % 3) = diameter;
        LookDevAxis(cut.size, (thin + 2) % 3) = diameter;
        if (window) {
            cut.center.y = -extent.y * 0.5f + (std::min)(local(1.0f, 1, 1.0f), extent.y - diameter) + diameter * 0.5f;
        }
        cut.roundAxis = static_cast<int8_t>(thin);
    } else if (kind == LookDevOpeningKind::Hole || thin == 1) {
        for (int axis = 0; axis < 3; ++axis) {
            if (axis != thin) {
                LookDevAxis(cut.size, axis) = local(2.0f, axis, 0.6f);
            }
        }
    } else {
        const int along = thin == 0 ? 2 : 0;
        const bool door = kind == LookDevOpeningKind::Door;
        LookDevAxis(cut.size, along) = local(door ? 1.0f : 1.2f, along, 0.6f);
        cut.size.y = local(door ? 2.1f : 1.2f, 1, 0.9f);
        // 門は床から、窓は 0.9 m の窓台の上から。
        const float bottom = door ? 0.0f : (std::min)(local(0.9f, 1, 1.0f), extent.y - cut.size.y);
        cut.center.y = -extent.y * 0.5f + bottom + cut.size.y * 0.5f;
    }
    return cut;
}

} // namespace HIKARI::EDITOR
