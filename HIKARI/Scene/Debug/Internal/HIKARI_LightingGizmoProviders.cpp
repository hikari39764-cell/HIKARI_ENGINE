#include "Scene/Debug/Internal/HIKARI_LightingGizmoProviders.h"

#include <algorithm>
#include <array>
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

// 灯の形（点・管の範囲の球、長方形の枠、スポットの円錐、平行光の矢印）。選んだ灯は選択色で壁越しにも描き、
// 選んでいない灯は種類の色を薄めて奥行きどおりに描く（多数の灯が重なっても画面が線で埋まらない）。
void SubmitLightShape(const GameObject& object, const LightComponent& light, bool selected) {
    const MATH::Mat4 world = object.GetTransform().GetWorldMatrix();
    const MATH::Vec3 position = MATH::TransformPoint(world, {});
    const MATH::Vec3 direction = WorldForward(world);
    const unsigned int color = selected ? DRAW::kSelectionColor : DRAW::WithAlpha(kLightColor, 0x70u);
    const auto depthMode = selected ? RENDERER3D::DEBUG::DebugDepthMode::XRay : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
    const float thickness = selected ? DRAW::kSelectedStrokePixels : DRAW::kNormalStrokePixels;
    if (selected) {
        SubmitProbeCenter(position, true, kLightColor);
    }

    using RENDER3D::LIGHTING::SceneLightType;
    if (light.GetLightType() == SceneLightType::Point) {
        SubmitSphereBoundary(MATH::Mat4::Translate(position), light.GetRange(), color, selected);
        return;
    }

    // 管は軸の線分、長方形は発光面の枠を描き、長方形はこの後に向きの矢印も描く。
    const LightSettings& settings = light.GetSettings();
    const MATH::Vec3 right = MATH::Normalize({world.m[0][0], world.m[0][1], world.m[0][2]});
    const MATH::Vec3 up = MATH::Normalize({world.m[1][0], world.m[1][1], world.m[1][2]});
    const auto submitEdge = [&](const MATH::Vec3& from, const MATH::Vec3& to) {
        RENDERER3D::DEBUG::Stroke3D edge{};
        edge.from = from;
        edge.to = to;
        edge.rgba = color;
        edge.depthMode = depthMode;
        edge.thicknessPixels = thickness;
        RENDERER3D::DEBUG::SubmitStroke3D(edge);
    };
    if (light.GetLightType() == SceneLightType::Tube) {
        const MATH::Vec3 half = right * (0.5f * settings.length);
        submitEdge(position - half, position + half);
        SubmitSphereBoundary(MATH::Mat4::Translate(position), light.GetRange(), color, selected);
        return;
    }
    if (light.GetLightType() == SceneLightType::Rect) {
        const MATH::Vec3 halfWidth = right * (0.5f * settings.length);
        const MATH::Vec3 halfHeight = up * (0.5f * settings.height);
        const std::array<MATH::Vec3, 4> corners{position - halfWidth - halfHeight, position + halfWidth - halfHeight,
                                                position + halfWidth + halfHeight, position - halfWidth + halfHeight};
        for (size_t index = 0; index < corners.size(); ++index) {
            submitEdge(corners[index], corners[(index + 1u) % corners.size()]);
        }
    }

    if (light.GetLightType() == SceneLightType::Spot) {
        const auto submitCone = [&](float angle, unsigned int coneColor) {
            RENDERER3D::DEBUG::WireCone3D cone{};
            cone.origin = position;
            cone.direction = direction;
            cone.length = light.GetRange();
            cone.angleDegrees = angle;
            cone.rgba = coneColor;
            cone.segments = selected ? 32 : 16;
            cone.depthMode = depthMode;
            cone.thicknessPixels = thickness;
            RENDERER3D::DEBUG::SubmitWireCone3D(cone);
        };
        submitCone(light.GetOuterConeAngleDegrees(), color);
        // 内側の円錐（減衰の始まり）は選んだ灯だけ。
        if (selected) {
            submitCone(light.GetInnerConeAngleDegrees(), 0xFFF0A0B8u);
        }
        return;
    }

    RENDERER3D::DEBUG::Stroke3D stroke{};
    stroke.from = position;
    stroke.to = position + direction * (selected ? 4.0f : 3.0f);
    stroke.rgba = color;
    stroke.depthMode = depthMode;
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

const LightComponent* DrawableLight(const GameObject& object) {
    const LightComponent* light = object.GetComponent<LightComponent>();
    return light != nullptr && light->IsEnabled() ? light : nullptr;
}

// 「灯」：選んだ灯だけ形を描く。選んでいない灯は視口の画面上の印（エディタ側）で示す。
void DrawSelectedLight(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const LightComponent* light = DrawableLight(object);
    if (light != nullptr && object.GetDocumentId() == context.selectedObjectId) {
        SubmitLightShape(object, *light, true);
    }
}

// 「すべての灯の範囲」（既定で切）：選んでいない灯の範囲も薄く描き、場全体の当たり方を確かめる時に使う。
void DrawOtherLightRanges(const GameObject& object, const ComponentGizmoDrawContext& context) {
    const LightComponent* light = DrawableLight(object);
    if (light != nullptr && object.GetDocumentId() != context.selectedObjectId) {
        SubmitLightShape(object, *light, false);
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
    const MATH::Vec3 center = MATH::TransformPoint(world, {});
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
    const bool projectionSelected = selected && probe->GetProjectionFollowsInfluence();
    const MATH::Mat4 world = BuildTranslationOnlyWorld(object);
    SubmitProbeCenter(MATH::TransformPoint(world, {}), selected);
    if (probe->GetInfluenceShape() == ReflectionProbeInfluenceShape::Box) {
        DRAW::SubmitStyledBox(world, probe->GetInfluenceBoxOffset(), probe->GetInfluenceBoxSize(),
                              kReflectionProbeColor, selected);

        const MATH::Vec3 innerSize = BuildInnerBoxSize(probe->GetInfluenceBoxSize(), probe->GetBlendDistance());
        if (probe->GetBlendDistance() > 0.001f && HasDrawableBoxSize(innerSize)) {
            DRAW::SubmitStyledBox(world, probe->GetInfluenceBoxOffset(), innerSize, kReflectionBlendCoreColor, false);
        }
    } else {
        SubmitSphereBoundary(world, probe->GetRadius(), kReflectionProbeColor, selected);
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
    SubmitProbeCenter(MATH::TransformPoint(world, {}), selected, kIrradianceVolumeColor);
    DRAW::SubmitStyledBox(world, {}, volume->GetSize(), kIrradianceVolumeColor, selected);
    if (volume->GetMode() == LightProbeVolumeMode::Baked) {
        // 焼き込みは探針の格子（上の箱）の外へ半間隔まで全面を覆い、そこから blendDistance で消える
        // （HikariEvaluateBakedVolumeInfluence）。壁際の探針を部屋の中に置いても壁の面が覆われるかをここで見る。
        const auto spacing = [](float size, uint32_t count) {
            return count > 1u ? size / static_cast<float>(count - 1u) : 0.0f;
        };
        const MATH::Vec3 size = volume->GetSize();
        const MATH::Vec3 covered{size.x + spacing(size.x, volume->GetCountX()),
                                 size.y + spacing(size.y, volume->GetCountY()),
                                 size.z + spacing(size.z, volume->GetCountZ())};
        DRAW::SubmitStyledBox(world, {}, covered, kIrradianceBlendCoreColor, false);
        const float blend = 2.0f * volume->GetBlendDistance();
        if (blend > 0.001f)
            DRAW::SubmitStyledBox(world, {}, {covered.x + blend, covered.y + blend, covered.z + blend},
                                  DRAW::WithAlpha(kIrradianceVolumeColor, 0x50u), false);
    } else {
        // 実時間（動的 GI）の区域は箱の内側で blendDistance をかけて消える。
        const MATH::Vec3 innerSize = BuildInnerBoxSize(volume->GetSize(), volume->GetBlendDistance());
        if (volume->GetBlendDistance() > 0.001f && HasDrawableBoxSize(innerSize)) {
            DRAW::SubmitStyledBox(world, {}, innerSize, kIrradianceBlendCoreColor, false);
        }
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
            marker.position = MATH::TransformPoint(world, localPosition);
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
    (void)registry.Register(ComponentGizmoProvider{"scene.light", "Lights", true, DrawSelectedLight});
    (void)registry.Register(ComponentGizmoProvider{"scene.light.ranges", "All Light Ranges", false, DrawOtherLightRanges});
    (void)registry.Register(ComponentGizmoProvider{std::string(kReflectionProbeGizmoProviderId), "Reflection Probes",
                                                   true, DrawReflectionProbe});
    (void)registry.Register(ComponentGizmoProvider{std::string(kIrradianceVolumeGizmoProviderId), "Irradiance Volumes",
                                                   true, DrawIrradianceVolume});
}

} // namespace HIKARI
