#include "Editor/Authoring/HIKARI_GroundPlacement.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_WorldGround.h"

namespace HIKARI::EDITOR {
namespace {
constexpr float kDegToRad = 0.01745329252f;
// 光線を物体の上のここから下ろす（崖の下の物体も、上に盛られた物体も拾える高さ）。
constexpr float kProbeHeightMeters = 2000.0f;
// 真横まで倒すと前の向きが決まらないので、傾きはここで止める。
constexpr float kMaxTiltDegrees = 85.0f;
const MATH::Vec3 kWorldUp{0.0f, 1.0f, 0.0f};

bool GroundBelow(const World& world, const MATH::Vec3& point, WorldGroundHit& hit) noexcept {
    return RaycastWorldGround(world, point + MATH::Vec3{0.0f, kProbeHeightMeters, 0.0f}, {0.0f, -1.0f, 0.0f},
                              kProbeHeightMeters * 2.0f, hit);
}
} // namespace

std::unordered_map<uint64_t, ModelFootprint> CollectModelFootprints() {
    std::unordered_map<uint64_t, ModelFootprint> footprints{};
    for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
         RenderSubmissionSystem::GetSceneRenderCache().GetSurfaceInstances()) {
        if (surface.valid && surface.model != nullptr && BOUNDS::IsUsable(surface.model->bounds)) {
            footprints.try_emplace(surface.objectId.value,
                                   ModelFootprint{surface.model->bounds.min, surface.model->bounds.max});
        }
    }
    return footprints;
}

bool PlaceOnGround(const World& world, const TransformData& current, const ModelFootprint* footprint,
                   const GroundPlacementSettings& settings, TransformData& out) noexcept {
    const MATH::Vec3& degrees = current.rotationEulerDeg;
    const MATH::Mat4 rotation = MATH::Mat4::Rotate(
        MATH::Quat::FromEulerXYZ(degrees.x * kDegToRad, degrees.y * kDegToRad, degrees.z * kDegToRad));
    // 水平の前と右。前が真上か真下を向いていた物は右から作る。
    const MATH::Vec3 facing = MATH::TransformVector(rotation, {0.0f, 0.0f, 1.0f});
    MATH::Vec3 ahead{facing.x, 0.0f, facing.z};
    if (MATH::Length(ahead) < 1.0e-3f) {
        const MATH::Vec3 side = MATH::TransformVector(rotation, {1.0f, 0.0f, 0.0f});
        ahead = MATH::Cross({side.x, 0.0f, side.z}, kWorldUp);
    }
    ahead = MATH::Normalize(ahead);
    const MATH::Vec3 right = MATH::Cross(kWorldUp, ahead);

    // 底の中心（今の姿勢）の真下。
    const float bottom = footprint != nullptr ? -footprint->min.y * current.scale.y : 0.0f;
    const MATH::Vec3 foot = current.position - MATH::TransformVector(rotation, {0.0f, 1.0f, 0.0f}) * bottom;
    WorldGroundHit center{};
    if (!GroundBelow(world, foot, center)) {
        return false;
    }
    // 底の四隅（周りの順）の真下。中心より底の広さ以上低い角は崖の外として支えにしない。
    MATH::Vec3 normal = center.normal;
    std::array<MATH::Vec3, 4> corners{};
    uint32_t supported = 0u;
    if (footprint != nullptr) {
        const float xs[4] = {footprint->min.x, footprint->max.x, footprint->max.x, footprint->min.x};
        const float zs[4] = {footprint->min.z, footprint->min.z, footprint->max.z, footprint->max.z};
        const float reach = (std::max)((footprint->max.x - footprint->min.x) * std::abs(current.scale.x),
                                       (footprint->max.z - footprint->min.z) * std::abs(current.scale.z));
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            WorldGroundHit hit{};
            const MATH::Vec3 at =
                foot + right * (xs[corner] * current.scale.x) + ahead * (zs[corner] * current.scale.z);
            if (GroundBelow(world, at, hit) && hit.position.y > center.position.y - reach) {
                corners[supported++] = hit.position;
            }
        }
        if (supported >= 3u) {
            // 角を結んだ面の向き（Newell の方法）。
            MATH::Vec3 fitted{};
            for (uint32_t index = 0u; index < supported; ++index) {
                const MATH::Vec3& a = corners[index];
                const MATH::Vec3& b = corners[(index + 1u) % supported];
                fitted.x += (a.y - b.y) * (a.z + b.z);
                fitted.y += (a.z - b.z) * (a.x + b.x);
                fitted.z += (a.x - b.x) * (a.y + b.y);
            }
            if (fitted.y < 0.0f) {
                fitted = fitted * -1.0f;
            }
            if (MATH::Length(fitted) > 1.0e-6f) {
                normal = MATH::Normalize(fitted);
            }
        }
    }
    // 上：真上から地面の向きへ alignToGround だけ寄せ、maxTiltDegrees で止める。
    const float slope = std::acos(std::clamp(MATH::Dot(normal, kWorldUp), -1.0f, 1.0f));
    const float tilt = (std::min)(slope * std::clamp(settings.alignToGround, 0.0f, 1.0f),
                                  std::clamp(settings.maxTiltDegrees, 0.0f, kMaxTiltDegrees) * kDegToRad);
    MATH::Vec3 up = kWorldUp;
    if (slope > 1.0e-4f) {
        const MATH::Vec3 lean = MATH::Normalize(normal - kWorldUp * MATH::Dot(normal, kWorldUp));
        up = kWorldUp * std::cos(tilt) + lean * std::sin(tilt);
    }
    // 底の面を中心の地面に通し、支えの角がその面より上にあれば（出っ張り）面ごと上げる。
    float lift = 0.0f;
    for (uint32_t index = 0u; index < supported; ++index) {
        const MATH::Vec3 offset = corners[index] - center.position;
        const float plane = -(up.x * offset.x + up.z * offset.z) / up.y;
        lift = (std::max)(lift, offset.y - plane);
    }
    // 前：水平の向きが ahead のまま坂の面に沿う向き（置き直しても水平の向きが変わらない）。
    const MATH::Vec3 forward = MATH::Normalize(ahead - kWorldUp * (MATH::Dot(ahead, up) / up.y));
    out = current;
    out.position =
        MATH::Vec3{center.position.x, center.position.y + lift - settings.sinkMeters, center.position.z} + up * bottom;
    out.rotationEulerDeg = MATH::EulerXYZDegreesFromQuatNearest(MATH::QuatFromForwardUp(forward, up), degrees);
    return true;
}

bool RestOnGround(const World& world, const TransformData& current, const ModelFootprint* footprint,
                  TransformData& out) noexcept {
    const MATH::Vec3& degrees = current.rotationEulerDeg;
    const MATH::Mat4 rotation = MATH::Mat4::Rotate(
        MATH::Quat::FromEulerXYZ(degrees.x * kDegToRad, degrees.y * kDegToRad, degrees.z * kDegToRad));
    const float bottom = footprint != nullptr ? -footprint->min.y * current.scale.y : 0.0f;
    const MATH::Vec3 foot = current.position - MATH::TransformVector(rotation, {0.0f, 1.0f, 0.0f}) * bottom;
    WorldGroundHit hit{};
    if (!GroundBelow(world, foot, hit)) {
        return false;
    }
    out = current;
    out.position.y += hit.position.y - foot.y;
    return true;
}

GroundPlacementSettings ResolveGroundPlacement(const SceneObjectData& object) {
    GroundPlacementSettings settings{};
    if (const SceneComponentData* anchor = object.FindComponent("GroundAnchorComponent")) {
        JSONFIELDS::ReadLenient(anchor->properties, settings, kGroundPlacementFields);
    }
    return settings;
}

} // namespace HIKARI::EDITOR
