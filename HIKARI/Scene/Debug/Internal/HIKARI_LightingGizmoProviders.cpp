#include "Scene/Debug/Internal/HIKARI_LightingGizmoProviders.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/Debug/HIKARI_BuiltInComponentGizmoProviders.h"
#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"
#include "Scene/Debug/Internal/HIKARI_ComponentGizmoDrawing.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
    namespace {
        namespace DRAW = COMPONENT_GIZMO_DRAW;

        constexpr unsigned int kReflectionProbeColor = 0x78C7FFFF;
        constexpr unsigned int kIrradianceVolumeColor = 0x9B8AFFFF;
        constexpr uint64_t kMaximumDisplayedProbePoints = 1024u;

        void DrawReflectionProbe(
            const GameObject& object,
            const ComponentGizmoDrawContext& context) {
            const ReflectionProbeComponent* probe =
                object.GetComponent<ReflectionProbeComponent>();
            if (probe == nullptr || !probe->IsEnabled()) {
                return;
            }
            const bool selected =
                object.GetDocumentId() == context.selectedObjectId;
            const MATH::Mat4 world =
                object.GetTransform().GetWorldMatrix();
            if (probe->GetInfluenceShape() ==
                ReflectionProbeInfluenceShape::Box) {
                DRAW::SubmitStyledBox(
                    world,
                    probe->GetInfluenceBoxOffset(),
                    probe->GetInfluenceBoxSize(),
                    kReflectionProbeColor,
                    selected);
            } else {
                constexpr uint32_t segments = 32u;
                constexpr float twoPi = 6.2831853071795864769f;
                for (uint32_t axis = 0; axis < 3u; ++axis) {
                    for (uint32_t segment = 0;
                        segment < segments;
                        ++segment) {
                        const float angle0 =
                            static_cast<float>(segment) *
                            twoPi / static_cast<float>(segments);
                        const float angle1 =
                            static_cast<float>(segment + 1u) *
                            twoPi / static_cast<float>(segments);
                        MATH::Vec3 point0{};
                        MATH::Vec3 point1{};
                        const float radius = probe->GetRadius();
                        if (axis == 0u) {
                            point0 = {
                                0.0f,
                                std::cos(angle0) * radius,
                                std::sin(angle0) * radius
                            };
                            point1 = {
                                0.0f,
                                std::cos(angle1) * radius,
                                std::sin(angle1) * radius
                            };
                        } else if (axis == 1u) {
                            point0 = {
                                std::cos(angle0) * radius,
                                0.0f,
                                std::sin(angle0) * radius
                            };
                            point1 = {
                                std::cos(angle1) * radius,
                                0.0f,
                                std::sin(angle1) * radius
                            };
                        } else {
                            point0 = {
                                std::cos(angle0) * radius,
                                std::sin(angle0) * radius,
                                0.0f
                            };
                            point1 = {
                                std::cos(angle1) * radius,
                                std::sin(angle1) * radius,
                                0.0f
                            };
                        }
                        DRAW::SubmitStyledStroke(
                            DRAW::TransformPoint(world, point0),
                            DRAW::TransformPoint(world, point1),
                            kReflectionProbeColor,
                            selected);
                    }
                }
            }
            if (selected) {
                RENDERER3D::DEBUG::PointMarker3D marker{};
                marker.position = DRAW::TransformPoint(world, {});
                marker.radius = 0.16f;
                marker.rgba = DRAW::kSelectionColor;
                marker.depthMode =
                    RENDERER3D::DEBUG::DebugDepthMode::XRay;
                marker.thicknessPixels = 2.4f;
                RENDERER3D::DEBUG::SubmitPointMarker3D(marker);
            }
        }

        float ProbeCoordinate(
            uint32_t index,
            uint32_t count,
            float size) noexcept {
            if (count <= 1u) {
                return 0.0f;
            }
            return -size * 0.5f +
                size * static_cast<float>(index) /
                static_cast<float>(count - 1u);
        }

        float ProbeMarkerRadius(
            const IrradianceVolumeComponent& volume) noexcept {
            const auto spacing = [](float size, uint32_t count) {
                return count > 1u
                    ? size / static_cast<float>(count - 1u)
                    : size;
            };
            const MATH::Vec3 size = volume.GetSize();
            const float minimumSpacing = (std::min)({
                spacing(size.x, volume.GetCountX()),
                spacing(size.y, volume.GetCountY()),
                spacing(size.z, volume.GetCountZ())
            });
            return std::clamp(
                minimumSpacing * 0.12f,
                0.035f,
                0.22f);
        }

        void DrawIrradianceVolume(
            const GameObject& object,
            const ComponentGizmoDrawContext& context) {
            const IrradianceVolumeComponent* volume =
                object.GetComponent<IrradianceVolumeComponent>();
            if (volume == nullptr || !volume->IsEnabled()) {
                return;
            }
            const bool selected =
                object.GetDocumentId() == context.selectedObjectId;
            const MATH::Mat4 world =
                object.GetTransform().GetWorldMatrix();
            DRAW::SubmitStyledBox(
                world,
                {},
                volume->GetSize(),
                kIrradianceVolumeColor,
                selected);

            const uint64_t totalPointCount =
                static_cast<uint64_t>(volume->GetCountX()) *
                static_cast<uint64_t>(volume->GetCountY()) *
                static_cast<uint64_t>(volume->GetCountZ());
            uint64_t drawnPointCount = 0u;
            const uint64_t stride = selected
                ? (std::max)(
                    uint64_t{ 1u },
                    (totalPointCount +
                        kMaximumDisplayedProbePoints - 1u) /
                        kMaximumDisplayedProbePoints)
                : totalPointCount + 1u;
            if (selected && totalPointCount > 0u) {
                const float markerRadius = ProbeMarkerRadius(*volume);
                for (uint64_t linearIndex = 0u;
                    linearIndex < totalPointCount;
                    linearIndex += stride) {
                    const uint32_t x = static_cast<uint32_t>(
                        linearIndex % volume->GetCountX());
                    const uint64_t yz =
                        linearIndex / volume->GetCountX();
                    const uint32_t y = static_cast<uint32_t>(
                        yz % volume->GetCountY());
                    const uint32_t z =
                        static_cast<uint32_t>(
                            yz / volume->GetCountY());
                    const MATH::Vec3 localPosition{
                        ProbeCoordinate(
                            x,
                            volume->GetCountX(),
                            volume->GetSize().x),
                        ProbeCoordinate(
                            y,
                            volume->GetCountY(),
                            volume->GetSize().y),
                        ProbeCoordinate(
                            z,
                            volume->GetCountZ(),
                            volume->GetSize().z)
                    };
                    RENDERER3D::DEBUG::PointMarker3D marker{};
                    marker.position =
                        DRAW::TransformPoint(world, localPosition);
                    marker.radius = markerRadius;
                    marker.rgba = DRAW::WithAlpha(
                        kIrradianceVolumeColor, 0xE0u);
                    marker.depthMode =
                        RENDERER3D::DEBUG::DebugDepthMode::XRay;
                    marker.thicknessPixels = 1.8f;
                    RENDERER3D::DEBUG::SubmitPointMarker3D(marker);
                    ++drawnPointCount;
                }
            }
            RENDERER3D::DEBUG::
                AccumulateLightProbeVolumeGizmoStats(
                    static_cast<uint32_t>((std::min)(
                        totalPointCount,
                        static_cast<uint64_t>(
                            (std::numeric_limits<uint32_t>::max)()))),
                    static_cast<uint32_t>(drawnPointCount),
                    selected ? 2u : 1u,
                    drawnPointCount < totalPointCount && selected);
        }
    }

    void RegisterLightingGizmoProviders(
        ComponentGizmoRegistry& registry) {
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kReflectionProbeGizmoProviderId),
            "Reflection Probes",
            true,
            DrawReflectionProbe
        });
        (void)registry.Register(ComponentGizmoProvider{
            std::string(kIrradianceVolumeGizmoProviderId),
            "Irradiance Volumes",
            true,
            DrawIrradianceVolume
        });
    }

} // namespace HIKARI
