#include "Editor/Authoring/HIKARI_EditorObjectPlacement.h"

#include <algorithm>
#include <cmath>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::EDITOR {
    namespace {
        constexpr float kMinimumForwardLength = 1.0e-5f;

        MATH::Vec3 ResolveForward(const Camera3D& camera) noexcept {
            const MATH::Vec3 direction =
                camera.GetTarget() - camera.GetPosition();
            const float length = MATH::Length(direction);
            return length > kMinimumForwardLength
                ? direction * (1.0f / length)
                : MATH::Vec3{ 0.0f, 0.0f, 1.0f };
        }
    }

    float EstimatePrimitivePlacementRadius(
        const ProceduralMeshSettings& rawSettings) noexcept {
        const ProceduralMeshSettings settings =
            SanitizeProceduralMeshSettings(rawSettings);
        const float halfWidth = settings.width * 0.5f;
        const float halfHeight = settings.height * 0.5f;
        const float halfDepth = settings.depth * 0.5f;
        switch (settings.kind) {
        case ProceduralMeshKind::Plane:
        case ProceduralMeshKind::GridPlane:
            return std::sqrt(
                halfWidth * halfWidth +
                halfDepth * halfDepth);
        case ProceduralMeshKind::Sphere:
            return halfWidth;
        case ProceduralMeshKind::Cylinder:
        case ProceduralMeshKind::Capsule:
            return std::sqrt(
                halfWidth * halfWidth +
                halfHeight * halfHeight);
        case ProceduralMeshKind::Box:
        default:
            return std::sqrt(
                halfWidth * halfWidth +
                halfHeight * halfHeight +
                halfDepth * halfDepth);
        }
    }

    MATH::Vec3 ComputeObjectPlacementInView(
        const Camera3D& camera,
        float boundingRadius) noexcept {
        const float radius = (std::max)(boundingRadius, 0.05f);
        const float halfFov = (std::max)(
            camera.GetFovYRad() * 0.5f,
            0.05f);
        const float fitDistance =
            radius / (std::max)(std::tan(halfFov), 0.05f) * 1.25f;
        const float minimumDistance = (std::max)(
            3.0f,
            camera.GetNearZ() * 4.0f);
        const float maximumDistance = (std::max)(
            minimumDistance,
            camera.GetFarZ() * 0.35f);
        const float distance = std::clamp(
            fitDistance,
            minimumDistance,
            maximumDistance);
        return camera.GetPosition() + ResolveForward(camera) * distance;
    }

    MATH::Vec3 ComputePrimitivePlacementInView(
        const Camera3D& camera,
        const ProceduralMeshSettings& settings) noexcept {
        return ComputeObjectPlacementInView(
            camera,
            EstimatePrimitivePlacementRadius(settings));
    }

} // namespace HIKARI::EDITOR
