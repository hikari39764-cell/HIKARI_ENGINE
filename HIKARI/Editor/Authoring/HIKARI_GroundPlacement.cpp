#include "Editor/Authoring/HIKARI_GroundPlacement.h"

#include <algorithm>
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
} // namespace

std::unordered_map<uint64_t, float> CollectModelBottoms() {
    std::unordered_map<uint64_t, float> bottoms{};
    for (const RENDER3D::RUNTIME::SceneSurfaceInstance& surface :
         RenderSubmissionSystem::GetSceneRenderCache().GetSurfaceInstances()) {
        if (surface.valid && surface.model != nullptr && BOUNDS::IsUsable(surface.model->bounds)) {
            bottoms.try_emplace(surface.objectId.value, -surface.model->bounds.min.y);
        }
    }
    return bottoms;
}

bool PlaceOnGround(const World& world, const TransformData& current, float bottomMeters,
                   const GroundPlacementSettings& settings, TransformData& out) noexcept {
    const MATH::Vec3& degrees = current.rotationEulerDeg;
    const MATH::Mat4 rotation = MATH::Mat4::Rotate(
        MATH::Quat::FromEulerXYZ(degrees.x * kDegToRad, degrees.y * kDegToRad, degrees.z * kDegToRad));
    const float bottom = bottomMeters * current.scale.y;
    const MATH::Vec3 foot = current.position - MATH::TransformVector(rotation, {0.0f, 1.0f, 0.0f}) * bottom;
    WorldGroundHit hit{};
    if (!RaycastWorldGround(world, foot + MATH::Vec3{0.0f, kProbeHeightMeters, 0.0f}, {0.0f, -1.0f, 0.0f},
                            kProbeHeightMeters * 2.0f, hit)) {
        return false;
    }
    // 上：真上から地面の法線へ alignToGround だけ寄せ、maxTiltDegrees で止める。
    const MATH::Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const float slope = std::acos(std::clamp(MATH::Dot(hit.normal, worldUp), -1.0f, 1.0f));
    const float tilt = (std::min)(slope * std::clamp(settings.alignToGround, 0.0f, 1.0f),
                                  std::clamp(settings.maxTiltDegrees, 0.0f, 90.0f) * kDegToRad);
    MATH::Vec3 up = worldUp;
    if (slope > 1.0e-4f) {
        const MATH::Vec3 lean = MATH::Normalize(hit.normal - worldUp * MATH::Dot(hit.normal, worldUp));
        up = worldUp * std::cos(tilt) + lean * std::sin(tilt);
    }
    // 前：今の前を新しい上に直交させる。真上や真下を向いていた物は今の右から作る。
    const MATH::Vec3 facing = MATH::TransformVector(rotation, {0.0f, 0.0f, 1.0f});
    MATH::Vec3 forward = facing - up * MATH::Dot(facing, up);
    if (MATH::Length(forward) < 1.0e-4f) {
        forward = MATH::Cross(MATH::TransformVector(rotation, {1.0f, 0.0f, 0.0f}), up);
    }
    out = current;
    out.position = MATH::Vec3{hit.position.x, hit.position.y - settings.sinkMeters, hit.position.z} + up * bottom;
    out.rotationEulerDeg =
        MATH::EulerXYZDegreesFromQuatNearest(MATH::QuatFromForwardUp(MATH::Normalize(forward), up), degrees);
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
