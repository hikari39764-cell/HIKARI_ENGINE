#include "Editor/Authoring/HIKARI_GroundFollow.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "Editor/Authoring/HIKARI_GroundPlacement.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_WorldGround.h"

namespace HIKARI::EDITOR {
namespace {
constexpr double kApplyIntervalSeconds = 0.2;
constexpr uint64_t kFollowGroundMergeGroup = 0x47524F554E44ull; // "GROUND"
constexpr std::string_view kAnchorType = "GroundAnchorComponent";

bool Moved(const TransformData& a, const TransformData& b) noexcept {
    const auto far = [](const MATH::Vec3& x, const MATH::Vec3& y, float tolerance) {
        return std::abs(x.x - y.x) > tolerance || std::abs(x.y - y.y) > tolerance || std::abs(x.z - y.z) > tolerance;
    };
    return far(a.position, b.position, 1.0e-3f) || far(a.rotationEulerDeg, b.rotationEulerDeg, 1.0e-2f);
}

// 子は親に従い、固定した物は動かさない。
bool FollowsGround(const SceneObjectData& object) noexcept {
    return !object.parent.has_value() && !object.editorLocked && object.FindComponent(kAnchorType) != nullptr;
}

enum class Placement : uint8_t { Placed, Unchanged, NeedsModel };

Placement Place(const DocumentSceneBase& scene, const std::unordered_map<uint64_t, ModelFootprint>& footprints,
                SceneObjectData& object) {
    const auto footprint = footprints.find(object.id.value);
    // 模型がまだ描画の写しに無い物は、底が分かるまで置かない（基点で置くと後で跳ねる）。
    if (footprint == footprints.end() && object.FindComponent("ModelComponent") != nullptr) {
        return Placement::NeedsModel;
    }
    TransformData placed{};
    if (!PlaceOnGround(scene.GetWorld(), object.transform, footprint != footprints.end() ? &footprint->second : nullptr,
                       ResolveGroundPlacement(object), placed) ||
        !Moved(placed, object.transform)) {
        return Placement::Unchanged;
    }
    object.transform = placed;
    return Placement::Placed;
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
    if ((!pending_ && waiting_.empty()) || nowSeconds - lastApplySeconds_ < kApplyIntervalSeconds) {
        return std::nullopt;
    }
    // 地面が変わったら全部、そうでなければ模型を待っていた物だけ。
    const bool everything = std::exchange(pending_, false);
    lastApplySeconds_ = nowSeconds;
    SceneDocument& document = scene.GetSceneDocument();
    const std::unordered_map<uint64_t, ModelFootprint> footprints = CollectModelFootprints();
    std::vector<uint64_t> stillWaiting{};
    std::vector<SceneObjectData> before{};
    for (SceneObjectData& object : document.objects) {
        if (!FollowsGround(object) ||
            (!everything && std::find(waiting_.begin(), waiting_.end(), object.id.value) == waiting_.end())) {
            continue;
        }
        SceneObjectData placed = object;
        switch (Place(scene, footprints, placed)) {
        case Placement::NeedsModel: stillWaiting.push_back(object.id.value); break;
        case Placement::Unchanged: break;
        case Placement::Placed:
            if (before.empty()) {
                before = document.objects;
            }
            object.transform = placed.transform;
            break;
        }
    }
    waiting_ = std::move(stillWaiting);
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

void GroundFollowService::ApplyToEdit(DocumentSceneBase& scene, SceneObjectAuthoringHistoryRequest& history) {
    if (scene.IsRuntimePlayActive() ||
        std::none_of(history.afterObjects.begin(), history.afterObjects.end(),
                     [](const SceneObjectData& object) { return FollowsGround(object); })) {
        return;
    }
    std::unordered_map<uint64_t, const SceneObjectData*> before{};
    for (const SceneObjectData& object : history.beforeObjects) {
        before.emplace(object.id.value, &object);
    }
    std::optional<std::unordered_map<uint64_t, ModelFootprint>> footprints{};
    for (SceneObjectData& object : history.afterObjects) {
        if (!FollowsGround(object)) {
            continue;
        }
        // この編集で動いた・足された・約束が付いたか変わった物だけ。
        const auto previous = before.find(object.id.value);
        if (previous != before.end() && previous->second->transform == object.transform &&
            FollowsGround(*previous->second) &&
            *previous->second->FindComponent(kAnchorType) == *object.FindComponent(kAnchorType)) {
            continue;
        }
        if (!footprints.has_value()) {
            footprints = CollectModelFootprints();
        }
        switch (Place(scene, *footprints, object)) {
        case Placement::NeedsModel: waiting_.push_back(object.id.value); break;
        case Placement::Unchanged: break;
        case Placement::Placed:
            if (SceneObjectData* live = FindSceneObjectData(scene.GetSceneDocument(), object.id)) {
                live->transform = object.transform;
            }
            break;
        }
    }
}

} // namespace HIKARI::EDITOR
