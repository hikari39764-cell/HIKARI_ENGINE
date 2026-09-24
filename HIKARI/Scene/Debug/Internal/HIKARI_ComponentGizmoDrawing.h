#pragma once

#include <algorithm>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::COMPONENT_GIZMO_DRAW {

inline constexpr unsigned int kSelectionColor = 0xFFD166FF;
inline constexpr unsigned int kSelectionHaloColor = 0x101820B8;
inline constexpr float kNormalStrokePixels = 1.6f;
inline constexpr float kSelectedStrokePixels = 2.8f;
inline constexpr float kSelectionHaloPixels = 5.4f;

inline unsigned int WithAlpha(unsigned int color, unsigned int alpha) noexcept {
    return (color & 0xFFFFFF00u) | (alpha & 0xFFu);
}

inline MATH::Vec3 TransformPoint(const MATH::Mat4& world, const MATH::Vec3& local) {
    const MATH::Vec4 point = world.TransformPoint({local.x, local.y, local.z, 1.0f});
    return {point.x, point.y, point.z};
}

inline void SubmitStroke(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int color,
                         float thicknessPixels = kNormalStrokePixels,
                         RENDERER3D::DEBUG::DebugDepthMode depthMode = RENDERER3D::DEBUG::DebugDepthMode::DepthTest) {
    RENDERER3D::DEBUG::Stroke3D stroke{};
    stroke.from = from;
    stroke.to = to;
    stroke.rgba = color;
    stroke.depthMode = depthMode;
    stroke.thicknessPixels = thicknessPixels;
    RENDERER3D::DEBUG::SubmitStroke3D(stroke);
}

inline void SubmitStyledStroke(const MATH::Vec3& from, const MATH::Vec3& to, unsigned int typeColor, bool selected) {
    if (selected) {
        SubmitStroke(from, to, kSelectionHaloColor, kSelectionHaloPixels, RENDERER3D::DEBUG::DebugDepthMode::XRay);
    }
    SubmitStroke(from, to, selected ? kSelectionColor : typeColor,
                 selected ? kSelectedStrokePixels : kNormalStrokePixels);
}

inline void SubmitStyledBox(const MATH::Mat4& world, const MATH::Vec3& localCenter, const MATH::Vec3& localSize,
                            unsigned int typeColor, bool selected, bool keepFillWhenUnselected = false) {
    Transform3D boxTransform{};
    boxTransform.useExplicitMatrix = true;
    boxTransform.explicitMatrix = world * MATH::Mat4::Translate(localCenter);
    if (selected) {
        RENDERER3D::DEBUG::Box3D halo{};
        halo.transform = boxTransform;
        halo.size = localSize;
        halo.outlineRgba = kSelectionHaloColor;
        halo.outlineThicknessPixels = kSelectionHaloPixels;
        halo.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
        RENDERER3D::DEBUG::SubmitBox3D(halo);
    }
    RENDERER3D::DEBUG::Box3D box{};
    box.transform = boxTransform;
    box.size = localSize;
    box.outlineRgba = selected ? kSelectionColor : typeColor;
    box.fillRgba = selected ? WithAlpha(typeColor, 0x26u) : (keepFillWhenUnselected ? WithAlpha(typeColor, 0x14u) : 0u);
    box.outlineThicknessPixels = selected ? kSelectedStrokePixels : kNormalStrokePixels;
    RENDERER3D::DEBUG::SubmitBox3D(box);
}

} // namespace HIKARI::COMPONENT_GIZMO_DRAW
