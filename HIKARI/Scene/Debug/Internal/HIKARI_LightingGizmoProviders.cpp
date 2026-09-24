#include "Scene/Debug/Internal/HIKARI_LightingGizmoProviders.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/Internal/HIKARI_ComponentGizmoDrawing.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
namespace {
namespace DRAW = COMPONENT_GIZMO_DRAW;

constexpr unsigned int kReflectionProbeColor = 0x78C7FFFF;
constexpr unsigned int kReflectionBlendCoreColor = 0x64D98AFF;
constexpr unsigned int kReflectionProjectionColor = 0xD493FFFF;
constexpr unsigned int kProbeCenterColor = 0xFFB45CFF;
constexpr unsigned int kIrradianceVolumeColor = 0x9B8AFFFF;
constexpr unsigned int kIrradianceBlendCoreColor = 0x73D7C2FF;
constexpr unsigned int kLightColor = 0xFFD36AFF;
constexpr uint64_t kMaximumDisplayedProbePoints = 1024u;

MATH::Mat4 BuildTranslationOnlyWorld(const GameObject& object) {
    return MATH::Mat4::Translate(object.GetTransform().position);
}

MATH::Vec3 WorldForward(const MATH::Mat4& world) {
    return MATH::Normalize({world.m[2][0], world.m[2][1], world.m[2][2]});
}

void SubmitProbeCenter(const MATH::Vec3& position, bool selected, unsigned int color);
void SubmitSphereBoundary(const MATH::Mat4& world, float radius, unsigned int color, bool selected);

void DrawLight(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const LightComponent* light = object.GetComponent<LightComponent>();
    if (light == nullptr || !light->IsEnabled()) {
        return;
    }
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    const MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
    const MATH::Vec3 position = DRAW::TransformPoint(world, {});
    const MATH::Vec3 direction = WorldForward(world);
    SubmitProbeCenter(position, selected, kLightColor);

    if (light->GetLightType() == LightComponentType::Point) {
        SubmitSphereBoundary(MATH::Mat4::Translate(position), light->GetRange(), kLightColor, selected);
        return;
    }

    if (light->GetLightType() == LightComponentType::Spot) {
        const auto submitCone = [&](float angle, unsigned int color) {
            RENDERER3D::DEBUG::WireCone3D cone{};
            cone.origin = position;
            cone.direction = direction;
            cone.length = light->GetRange();
            cone.angleDegrees = angle;
            cone.rgba = color;
            cone.segments = 32;
            cone.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
            cone.thicknessPixels = selected ? DRAW::kSelectedStrokePixels : DRAW::kNormalStrokePixels;
            RENDERER3D::DEBUG::SubmitWireCone3D(cone);
        };
        submitCone(light->GetOuterConeAngleDegrees(), selected ? DRAW::kSelectionColor : kLightColor);
        submitCone(light->GetInnerConeAngleDegrees(), 0xFFF0A0B8u);
        return;
    }

    RENDERER3D::DEBUG::Stroke3D stroke{};
    stroke.from = position;
    stroke.to = position + direction * (selected ? 4.0f : 3.0f);
    stroke.rgba = selected ? DRAW::kSelectionColor : kLightColor;
    stroke.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
    stroke.thicknessPixels = selected ? 4.5f : 3.0f;
    RENDERER3D::DEBUG::SubmitStroke3D(stroke);

    MATH::Vec3 side = MATH::Cross({0.0f, 1.0f, 0.0f}, direction);
    if (MATH::Dot(side, side) < 1.0e-4f) {
        side = MATH::Cross({1.0f, 0.0f, 0.0f}, direction);
    }
    side = MATH::Normalize(side);
    const float headLength = selected ? 0.75f : 0.60f;
    const float headWidth = selected ? 0.42f : 0.32f;
    const MATH::Vec3 headBase = stroke.to - direction * headLength;
    for (const float sign : {-1.0f, 1.0f}) {
        RENDERER3D::DEBUG::Stroke3D wing = stroke;
        wing.from = stroke.to;
        wing.to = headBase + side * (headWidth * sign);
        wing.thicknessPixels = selected ? 4.0f : 2.6f;
        RENDERER3D::DEBUG::SubmitStroke3D(wing);
    }
}

MATH::Vec3 BuildInnerBoxSize(const MATH::Vec3& outerSize, float blendDistance) noexcept {
    const float inset = (std::max)(0.0f, blendDistance) * 2.0f;
    return {(std::max)(0.0f, outerSize.x - inset), (std::max)(0.0f, outerSize.y - inset),
            (std::max)(0.0f, outerSize.z - inset)};
}

bool HasDrawableBoxSize(const MATH::Vec3& size) noexcept {
    return size.x > 0.001f && size.y > 0.001f && size.z > 0.001f;
}

void SubmitProbeCenter(const MATH::Vec3& position, bool selected, unsigned int color = kProbeCenterColor) {
    RENDERER3D::DEBUG::PointMarker3D marker{};
    marker.position = position;
    marker.radius = selected ? 0.16f : 0.10f;
    marker.rgba = selected ? DRAW::kSelectionColor : color;
    marker.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
    marker.thicknessPixels = selected ? 2.8f : 2.0f;
    RENDERER3D::DEBUG::SubmitPointMarker3D(marker);
}

void SubmitSphereBoundary(const MATH::Mat4& world, float radius, unsigned int color, bool selected) {
    if (radius <= 0.001f) {
        return;
    }
    const MATH::Vec3 center = DRAW::TransformPoint(world, {});
    if (selected) {
        RENDERER3D::DEBUG::WireSphere3D halo{};
        halo.center = center;
        halo.radius = radius;
        halo.rgba = DRAW::kSelectionHaloColor;
        halo.segments = 32;
        halo.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
        halo.thicknessPixels = DRAW::kSelectionHaloPixels;
        RENDERER3D::DEBUG::SubmitWireSphere3D(halo);
    }
    RENDERER3D::DEBUG::WireSphere3D sphere{};
    sphere.center = center;
    sphere.radius = radius;
    sphere.rgba = selected ? DRAW::kSelectionColor : color;
    sphere.segments = 32;
    sphere.thicknessPixels = selected ? DRAW::kSelectedStrokePixels : DRAW::kNormalStrokePixels;
    RENDERER3D::DEBUG::SubmitWireSphere3D(sphere);
}

void DrawReflectionProbe(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const ReflectionProbeComponent* probe = object.GetComponent<ReflectionProbeComponent>();
    if (probe == nullptr || !probe->IsEnabled()) {
        return;
    }
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    const bool projectionSelected =
        selected && (probe->GetProjectionFollowsInfluence() ||
                     probe->GetViewportBoundsTarget() == ReflectionProbeViewportBoundsTarget::Projection);
    const bool influenceSelected = selected && (!projectionSelected || probe->GetProjectionFollowsInfluence());
    const MATH::Mat4 world = BuildTranslationOnlyWorld(object);
    SubmitProbeCenter(DRAW::TransformPoint(world, {}), selected);
    if (probe->GetInfluenceShape() == ReflectionProbeInfluenceShape::Box) {
        DRAW::SubmitStyledBox(world, probe->GetInfluenceBoxOffset(), probe->GetInfluenceBoxSize(),
                              kReflectionProbeColor, influenceSelected);

        const MATH::Vec3 innerSize = BuildInnerBoxSize(probe->GetInfluenceBoxSize(), probe->GetBlendDistance());
        if (probe->GetBlendDistance() > 0.001f && HasDrawableBoxSize(innerSize)) {
            DRAW::SubmitStyledBox(world, probe->GetInfluenceBoxOffset(), innerSize, kReflectionBlendCoreColor, false);
        }
    } else {
        SubmitSphereBoundary(world, probe->GetRadius(), kReflectionProbeColor, influenceSelected);
        const float innerRadius = probe->GetRadius() - probe->GetBlendDistance();
        if (probe->GetBlendDistance() > 0.001f) {
            SubmitSphereBoundary(world, innerRadius, kReflectionBlendCoreColor, false);
        }
    }

    if (probe->GetProjectionShape() == ReflectionProbeProjectionShape::Box) {
        DRAW::SubmitStyledBox(world, probe->GetProjectionBoxOffset(), probe->GetProjectionBoxSize(),
                              kReflectionProjectionColor, projectionSelected);
    }
}

float ProbeCoordinate(uint32_t index, uint32_t count, float size) noexcept {
    if (count <= 1u) {
        return 0.0f;
    }
    return -size * 0.5f + size * static_cast<float>(index) / static_cast<float>(count - 1u);
}

float ProbeMarkerRadius(const IrradianceVolumeComponent& volume) noexcept {
    const auto spacing = [](float size, uint32_t count) {
        return count > 1u ? size / static_cast<float>(count - 1u) : size;
    };
    const MATH::Vec3 size = volume.GetSize();
    const float minimumSpacing = (std::min)({spacing(size.x, volume.GetCountX()), spacing(size.y, volume.GetCountY()),
                                             spacing(size.z, volume.GetCountZ())});
    return std::clamp(minimumSpacing * 0.12f, 0.035f, 0.22f);
}

void DrawIrradianceVolume(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const IrradianceVolumeComponent* volume = object.GetComponent<IrradianceVolumeComponent>();
    if (volume == nullptr || !volume->IsEnabled()) {
        return;
    }
    const bool selected = object.GetDocumentId() == context.selectedObjectId;
    const MATH::Mat4 world = BuildTranslationOnlyWorld(object);
    SubmitProbeCenter(DRAW::TransformPoint(world, {}), selected, kIrradianceVolumeColor);
    DRAW::SubmitStyledBox(world, {}, volume->GetSize(), kIrradianceVolumeColor, selected);
    const MATH::Vec3 innerSize = BuildInnerBoxSize(volume->GetSize(), volume->GetBlendDistance());
    if (volume->GetBlendDistance() > 0.001f && HasDrawableBoxSize(innerSize)) {
        DRAW::SubmitStyledBox(world, {}, innerSize, kIrradianceBlendCoreColor, false);
    }

    const uint64_t totalPointCount = static_cast<uint64_t>(volume->GetCountX()) *
                                     static_cast<uint64_t>(volume->GetCountY()) *
                                     static_cast<uint64_t>(volume->GetCountZ());
    uint64_t drawnPointCount = 0u;
    const uint64_t stride = selected ? (std::max)(uint64_t{1u}, (totalPointCount + kMaximumDisplayedProbePoints - 1u) /
                                                                    kMaximumDisplayedProbePoints)
                                     : totalPointCount + 1u;
    if (selected && totalPointCount > 0u) {
        const float markerRadius = ProbeMarkerRadius(*volume);
        for (uint64_t linearIndex = 0u; linearIndex < totalPointCount; linearIndex += stride) {
            const uint32_t x = static_cast<uint32_t>(linearIndex % volume->GetCountX());
            const uint64_t yz = linearIndex / volume->GetCountX();
            const uint32_t y = static_cast<uint32_t>(yz % volume->GetCountY());
            const uint32_t z = static_cast<uint32_t>(yz / volume->GetCountY());
            const MATH::Vec3 localPosition{ProbeCoordinate(x, volume->GetCountX(), volume->GetSize().x),
                                           ProbeCoordinate(y, volume->GetCountY(), volume->GetSize().y),
                                           ProbeCoordinate(z, volume->GetCountZ(), volume->GetSize().z)};
            RENDERER3D::DEBUG::PointMarker3D marker{};
            marker.position = DRAW::TransformPoint(world, localPosition);
            marker.radius = markerRadius;
            marker.rgba = DRAW::WithAlpha(kIrradianceVolumeColor, 0xE0u);
            marker.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
            marker.thicknessPixels = 1.8f;
            RENDERER3D::DEBUG::SubmitPointMarker3D(marker);
            ++drawnPointCount;
        }
    }
    RENDERER3D::DEBUG::AccumulateLightProbeVolumeGizmoStats(
        static_cast<uint32_t>(
            (std::min)(totalPointCount, static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))),
        static_cast<uint32_t>(drawnPointCount), selected ? 2u : 1u, drawnPointCount < totalPointCount && selected);
}
} // namespace

void RegisterLightingGizmoProviders(ComponentGizmoRegistry& registry) {
    (void)registry.Register(ComponentGizmoProvider{"scene.light", "Lights", true, DrawLight});
    (void)registry.Register(ComponentGizmoProvider{std::string(kReflectionProbeGizmoProviderId), "Reflection Probes",
                                                   true, DrawReflectionProbe});
    (void)registry.Register(ComponentGizmoProvider{std::string(kIrradianceVolumeGizmoProviderId), "Irradiance Volumes",
                                                   true, DrawIrradianceVolume});
}

} // namespace HIKARI
