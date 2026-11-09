#include "Editor/Authoring/HIKARI_GroundFollow.h"

#include <cmath>
#include <utility>

#include "Editor/Authoring/HIKARI_GroundPlacement.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_WorldGround.h"

namespace HIKARI::EDITOR {
namespace {
constexpr double kApplyIntervalSeconds = 0.2;
constexpr uint64_t kFollowGroundMergeGroup = 0x47524F554E44ull; // "GROUND"

bool Moved(const TransformData& a, const TransformData& b) noexcept {
    const auto far = [](const MATH::Vec3& x, const MATH::Vec3& y, float tolerance) {
        return std::abs(x.x - y.x) > tolerance || std::abs(x.y - y.y) > tolerance || std::abs(x.z - y.z) > tolerance;
    };
    return far(a.position, b.position, 1.0e-3f) || far(a.rotationEulerDeg, b.rotationEulerDeg, 1.0e-2f);
}
} // namespace

std::optional<SceneObjectAuthoringHistoryRequest> GroundFollowService::Update(DocumentSceneBase& scene, bool sceneDirty,
                                                                              double nowSeconds) {
    if (scene.IsRuntimePlayActive()) {
        return std::nullopt;
    }
    const uint64_t revision = WorldGroundRevision(scene.GetWorld());
    if (revision != seenRevision_) {
        seenRevision_ = revision;
        pending_ = true;
    }
    if (!pending_ || nowSeconds - lastApplySeconds_ < kApplyIntervalSeconds) {
        return std::nullopt;
    }
    pending_ = false;
    lastApplySeconds_ = nowSeconds;
    SceneDocument& document = scene.GetSceneDocument();
    const std::unordered_map<uint64_t, float> bottoms = CollectModelBottoms();
    std::vector<SceneObjectData> before{};
    for (SceneObjectData& object : document.objects) {
        // 子は親に従い、固定した物は動かさない。
        if (object.parent.has_value() || object.editorLocked ||
            object.FindComponent("GroundAnchorComponent") == nullptr) {
            continue;
        }
        // 模型がまだ描画の写しに載っていない物は、底が分かるまで次へ回す（基点で置くと後で跳ねる）。
        const auto bottom = bottoms.find(object.id.value);
        if (bottom == bottoms.end() && object.FindComponent("ModelComponent") != nullptr) {
            pending_ = true;
            continue;
        }
        TransformData placed{};
        if (!PlaceOnGround(scene.GetWorld(), object.transform, bottom != bottoms.end() ? bottom->second : 0.0f,
                           ResolveGroundPlacement(object), placed) ||
            !Moved(placed, object.transform)) {
            continue;
        }
        if (before.empty()) {
            before = document.objects;
        }
        object.transform = placed;
    }
    if (before.empty()) {
        return std::nullopt;
    }
    return SceneObjectAuthoringHistoryRequest{"Follow Ground",
                                              std::move(before),
                                              document.objects,
                                              document.camera,
                                              document.camera,
                                              sceneDirty || scene.HasUnsavedSceneChanges(),
                                              true,
                                              true,
                                              kFollowGroundMergeGroup};
}

} // namespace HIKARI::EDITOR
