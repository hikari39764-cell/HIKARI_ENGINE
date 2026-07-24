#include "HIKARI_ReflectionProbeGizmoRenderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

namespace HIKARI::EDITOR {

    namespace {

        constexpr unsigned int kDisabledColor = 0x6A6F78AA;
        constexpr unsigned int kInvalidColor = 0xFF4C5CFF;
        constexpr unsigned int kProbeCenterColor = 0x61E6A8FF;
        constexpr unsigned int kInfluenceColor = 0x5EC7FFCC;
        constexpr unsigned int kProjectionColor = 0xFFD166CC;

        unsigned int ResolveProbeColor(unsigned int validColor) {
            const REFLECTION::ReflectionProbeRuntimeData& probe =
                REFLECTION::GetActiveProbe();

            if (!probe.enabled) {
                return kDisabledColor;
            }
            if (!probe.valid) {
                return kInvalidColor;
            }
            return validColor;
        }

        RENDERER3D::DEBUG::DebugDepthMode ResolveDepthMode(const ViewportOverlayState& overlays) {
            return overlays.showXRayGizmos
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
        }

        bool HasUsableBoxSize(const MATH::Vec3& size) {
            return size.x > 0.001f && size.y > 0.001f && size.z > 0.001f;
        }

        int ResolveSphereSegments(
            const ReflectionProbeSettings& probe,
            const Camera3D& camera,
            const ViewportOverlayState& overlays) {

            if (overlays.editReflectionProbe) {
                return 64;
            }

            const float distance = MATH::Length(camera.GetPosition() - probe.position);
            const float farDistance = (std::max)(24.0f, probe.radius * 4.0f);
            if (distance > farDistance) {
                return 16;
            }
            return 32;
        }

        MATH::Vec3 CirclePoint(
            const MATH::Vec3& center,
            float radius,
            int axis,
            float t) {

            const float c = std::cos(t) * radius;
            const float s = std::sin(t) * radius;
            if (axis == 0) {
                return { center.x, center.y + c, center.z + s };
            }
            if (axis == 1) {
                return { center.x + c, center.y, center.z + s };
            }
            return { center.x + c, center.y + s, center.z };
        }

        void SubmitLine(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            RENDERER3D::DEBUG::SubmitLine3D({
                from,
                to,
                color,
                depthMode
            });
        }

        void SubmitCircle(
            const MATH::Vec3& center,
            float radius,
            int axis,
            int segments,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            const int safeSegments = (std::max)(8, segments);
            for (int i = 0; i < safeSegments; ++i) {
                const float t0 = (static_cast<float>(i) / static_cast<float>(safeSegments)) *
                    (2.0f * std::numbers::pi_v<float>);
                const float t1 = (static_cast<float>(i + 1) / static_cast<float>(safeSegments)) *
                    (2.0f * std::numbers::pi_v<float>);
                SubmitLine(
                    CirclePoint(center, radius, axis, t0),
                    CirclePoint(center, radius, axis, t1),
                    color,
                    depthMode);
            }
        }

        MATH::Vec3 BoxMin(const MATH::Vec3& center, const MATH::Vec3& size) {
            const MATH::Vec3 halfSize = size * 0.5f;
            return {
                center.x - halfSize.x,
                center.y - halfSize.y,
                center.z - halfSize.z
            };
        }

        MATH::Vec3 BoxMax(const MATH::Vec3& center, const MATH::Vec3& size) {
            const MATH::Vec3 halfSize = size * 0.5f;
            return {
                center.x + halfSize.x,
                center.y + halfSize.y,
                center.z + halfSize.z
            };
        }

        void BuildBoxCorners(
            const MATH::Vec3& center,
            const MATH::Vec3& size,
            MATH::Vec3 outCorners[8]) {

            const MATH::Vec3 bmin = BoxMin(center, size);
            const MATH::Vec3 bmax = BoxMax(center, size);
            outCorners[0] = { bmin.x, bmin.y, bmin.z };
            outCorners[1] = { bmax.x, bmin.y, bmin.z };
            outCorners[2] = { bmax.x, bmin.y, bmax.z };
            outCorners[3] = { bmin.x, bmin.y, bmax.z };
            outCorners[4] = { bmin.x, bmax.y, bmin.z };
            outCorners[5] = { bmax.x, bmax.y, bmin.z };
            outCorners[6] = { bmax.x, bmax.y, bmax.z };
            outCorners[7] = { bmin.x, bmax.y, bmax.z };
        }

        void SubmitCornerMarker(
            const MATH::Vec3& corner,
            float markerSize,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            SubmitLine(
                { corner.x - markerSize, corner.y, corner.z },
                { corner.x + markerSize, corner.y, corner.z },
                color,
                depthMode);
            SubmitLine(
                { corner.x, corner.y - markerSize, corner.z },
                { corner.x, corner.y + markerSize, corner.z },
                color,
                depthMode);
            SubmitLine(
                { corner.x, corner.y, corner.z - markerSize },
                { corner.x, corner.y, corner.z + markerSize },
                color,
                depthMode);
        }

        void SubmitBox(
            const MATH::Vec3& center,
            const MATH::Vec3& size,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode,
            bool cornerMarkers) {

            MATH::Vec3 corners[8]{};
            BuildBoxCorners(center, size, corners);
            const int edges[12][2] = {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            };

            for (const auto& edge : edges) {
                SubmitLine(corners[edge[0]], corners[edge[1]], color, depthMode);
            }

            if (!cornerMarkers) {
                return;
            }

            const float minSize = (std::max)(0.001f, (std::min)((std::min)(size.x, size.y), size.z));
            const float markerSize = (std::clamp)(minSize * 0.035f, 0.06f, 0.28f);
            for (const MATH::Vec3& corner : corners) {
                SubmitCornerMarker(corner, markerSize, color, depthMode);
            }
        }

        void SubmitCenterCross(
            const MATH::Vec3& center,
            float radius,
            unsigned int color,
            RENDERER3D::DEBUG::DebugDepthMode depthMode) {

            const float size = (std::clamp)(radius * 0.08f, 0.12f, 0.5f);
            SubmitLine(
                { center.x - size, center.y, center.z },
                { center.x + size, center.y, center.z },
                color,
                depthMode);
            SubmitLine(
                { center.x, center.y - size, center.z },
                { center.x, center.y + size, center.z },
                color,
                depthMode);
            SubmitLine(
                { center.x, center.y, center.z - size },
                { center.x, center.y, center.z + size },
                color,
                depthMode);
        }

    } // namespace

    void ReflectionProbeGizmoRenderer::Submit(
        const SceneEnvironment& environment,
        const ViewportOverlayState& overlays,
        const Camera3D& camera) const {

        if (!overlays.showReflectionProbe || !environment.reflectionProbe.enabled) {
            return;
        }

        const ReflectionProbeSettings& probe = environment.reflectionProbe;
        const MATH::Vec3 center = probe.position;
        const float radius = (std::max)(0.01f, probe.radius);
        const int sphereSegments = ResolveSphereSegments(probe, camera, overlays);
        const RENDERER3D::DEBUG::DebugDepthMode depthMode = ResolveDepthMode(overlays);
        const unsigned int centerColor = ResolveProbeColor(kProbeCenterColor);
        const unsigned int influenceColor = HasUsableBoxSize(probe.influenceBoxSize)
            ? ResolveProbeColor(kInfluenceColor)
            : kInvalidColor;
        const unsigned int projectionColor = HasUsableBoxSize(probe.projectionBoxSize)
            ? ResolveProbeColor(kProjectionColor)
            : kInvalidColor;

        // Probe center と authoring volume を editor overlay に描く。
        SubmitCenterCross(center, radius, centerColor, depthMode);
        if (probe.influenceShape == ReflectionProbeInfluenceShape::Box) {
            SubmitBox(probe.influenceBoxCenter, probe.influenceBoxSize, influenceColor, depthMode, true);
        } else {
            SubmitCircle(center, radius, 0, sphereSegments, influenceColor, depthMode);
            SubmitCircle(center, radius, 1, sphereSegments, influenceColor, depthMode);
            SubmitCircle(center, radius, 2, sphereSegments, influenceColor, depthMode);
        }
        if (probe.projectionShape == ReflectionProbeProjectionShape::Box) {
            SubmitBox(probe.projectionBoxCenter, probe.projectionBoxSize, projectionColor, depthMode, true);
        }
    }

} // namespace HIKARI::EDITOR
