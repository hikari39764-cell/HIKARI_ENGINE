#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"
#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace HIKARI::EDITOR {
namespace {
float EndpointTime(const CinematicSequence& sequence, const SequenceCameraEndpoint& endpoint) {
    return endpoint.end ? sequence.durationSeconds : 0.f;
}
bool SamePose(const BoundCameraPose& a, const BoundCameraPose& b) {
    constexpr float radians = 3.14159265358979323846f / 180.f;
    const auto rotation = [=](const MATH::Vec3& e) {
        return MATH::NormalizeQ(MATH::Quat::FromEulerXYZ(e.x * radians, e.y * radians, e.z * radians));
    };
    const auto qa = rotation(a.rotationEulerDeg), qb = rotation(b.rotationEulerDeg);
    const float sign = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0.f ? -1.f : 1.f;
    const auto square = [](float v) { return v * v; };
    // Euler の等価表現（例：-180 と 180）を機位の不一致と扱わない。
    const float delta = square(qa.x - sign * qb.x) + square(qa.y - sign * qb.y) + square(qa.z - sign * qb.z) +
                        square(qa.w - sign * qb.w);
    return MATH::Length(a.position - b.position) <= .001f && delta <= 1.e-8f && std::abs(a.fov - b.fov) <= .001f;
}
bool SameSequence(const CinematicSequence& a, const CinematicSequence& b) {
    nlohmann::json x, y;
    SerializeCinematicSequenceJson(a, x);
    SerializeCinematicSequenceJson(b, y);
    return x == y;
}
} // namespace
void SequenceEditorDocument::ClearCameraEndpoints() {
    endpointGroups_.clear();
    linkedSources_.clear();
    state_.linked.clear();
    endpointLabel_.clear();
    endpointAtEnd_ = false;
}
float SequenceEditorDocument::GetCameraEndpointTime() const {
    const auto* sequence = GetAssetSequence();
    return endpointAtEnd_ && sequence ? sequence->durationSeconds : 0.f;
}
const CinematicSequence* SequenceEditorDocument::EndpointSequence(const SequenceCameraEndpoint& endpoint) const {
    if (endpoint.assetGuid == assetGuid_)
        return GetAssetSequence();
    for (size_t i = 0; i < linkedSources_.size(); ++i)
        if (linkedSources_[i].guid == endpoint.assetGuid)
            return &state_.linked[i];
    return nullptr;
}
bool SequenceEditorDocument::SetCameraEndpointGroups(const AssetDatabase& database,
                                                     const std::vector<SequenceCameraEndpointGroup>& groups,
                                                     std::string label, bool atEnd, std::string& message) {
    if (IsDirty()) {
        message = "请先保存或撤销当前演出，再切换关联机位。";
        return false;
    }
    if (groups == endpointGroups_ && std::all_of(linkedSources_.begin(), linkedSources_.end(),
                                                 [&](const auto& source) { return source.version.CanSave(message); })) {
        endpointLabel_ = std::move(label);
        endpointAtEnd_ = atEnd;
        return true;
    }
    std::vector<LinkedSource> sources;
    std::set<std::tuple<std::string, uint64_t, bool>> seen;
    for (const auto& group : groups) {
        if (group.endpoints.empty() || group.endpoints.front().assetGuid != assetGuid_) {
            message = "机位组必须以当前演出的端点开始。";
            return false;
        }
        for (size_t i = 0; i < group.endpoints.size(); ++i) {
            const auto& endpoint = group.endpoints[i];
            if (i > 0 && endpoint.assetGuid == assetGuid_) {
                message = "同一演出的两个端点不能代表同一区域。";
                return false;
            }
            if (!seen.emplace(endpoint.assetGuid.value, endpoint.bindingId.value, endpoint.end).second) {
                message = "同一端点不能属于多个区域机位。";
                return false;
            }
            const CinematicSequence* sequence = GetAssetSequence();
            if (endpoint.assetGuid != assetGuid_) {
                auto found = std::find_if(sources.begin(), sources.end(),
                                          [&](const auto& source) { return source.guid == endpoint.assetGuid; });
                if (found == sources.end()) {
                    LinkedSource source;
                    source.guid = endpoint.assetGuid;
                    if (!LoadSequenceForAuthoring(database, source.guid, source.saved, message))
                        return false;
                    source.version.Accept(database, source.guid);
                    sources.push_back(std::move(source));
                    found = sources.end() - 1;
                }
                sequence = &found->saved;
            }
            BoundCameraPose pose;
            if (!sequence ||
                !ReadBoundCameraPose(*sequence, endpoint.bindingId, EndpointTime(*sequence, endpoint), pose)) {
                message = "关联演出缺少可编辑的相机轨道。";
                return false;
            }
        }
    }
    // 不一致を開けなくする旧制約は廃止。開くだけでは同期せず、作者が参照元を選べる。
    endpointGroups_ = groups;
    linkedSources_ = std::move(sources);
    state_.linked.clear();
    for (const auto& source : linkedSources_)
        state_.linked.push_back(source.saved);
    endpointLabel_ = std::move(label);
    endpointAtEnd_ = atEnd;
    ResetHistory(true);
    return true;
}
const SequenceCameraEndpointGroup* SequenceEditorDocument::FindCameraEndpointGroup(
    SEQUENCER::SequenceBindingId bindingId, float time) const {
    const auto* sequence = GetAssetSequence();
    if (sequence)
        for (const auto& group : endpointGroups_) {
            const auto& endpoint = group.endpoints.front();
            if (endpoint.bindingId == bindingId && std::abs(EndpointTime(*sequence, endpoint) - time) < 1.e-5f)
                return &group;
        }
    return nullptr;
}
bool SequenceEditorDocument::CameraEndpointsMatch(const SequenceCameraEndpointGroup& group) const {
    BoundCameraPose first;
    for (size_t i = 0; i < group.endpoints.size(); ++i) {
        const auto& endpoint = group.endpoints[i];
        const auto* sequence = EndpointSequence(endpoint);
        BoundCameraPose pose;
        if (!sequence || !ReadBoundCameraPose(*sequence, endpoint.bindingId, EndpointTime(*sequence, endpoint), pose))
            return false;
        if (i == 0)
            first = pose;
        else if (!SamePose(first, pose))
            return false;
    }
    return true;
}
bool SequenceEditorDocument::ApplyCameraEndpoint(const SequenceCameraEndpointGroup& group) {
    const auto& origin = group.endpoints.front();
    const auto* sequence = GetAssetSequence();
    BoundCameraPose pose;
    if (!sequence || !ReadBoundCameraPose(*sequence, origin.bindingId, EndpointTime(*sequence, origin), pose))
        return false;
    for (size_t i = 1; i < group.endpoints.size(); ++i) {
        const auto& target = group.endpoints[i];
        auto* peer = const_cast<CinematicSequence*>(EndpointSequence(target));
        if (!peer || !WriteBoundCameraPose(*peer, target.bindingId, EndpointTime(*peer, target), pose, BoundAll))
            return false;
    }
    return true;
}
void SequenceEditorDocument::SyncChangedCameraEndpoints(const SceneCinematicsSettings& before) {
    const auto* after = GetAssetSequence();
    if (!after)
        return;
    const auto old = std::find_if(before.sequences.begin(), before.sequences.end(),
                                  [&](const auto& s) { return s.id == after->id; });
    if (old == before.sequences.end())
        return;
    for (const auto& group : endpointGroups_) {
        const auto& endpoint = group.endpoints.front();
        BoundCameraPose a, b;
        if (ReadBoundCameraPose(*old, endpoint.bindingId, EndpointTime(*old, endpoint), a) &&
            ReadBoundCameraPose(*after, endpoint.bindingId, EndpointTime(*after, endpoint), b) &&
            (MATH::Length(a.position - b.position) > 0 || MATH::Length(a.rotationEulerDeg - b.rotationEulerDeg) > 0 ||
             a.fov != b.fov))
            ApplyCameraEndpoint(group);
    }
}
bool SequenceEditorDocument::AlignCameraEndpoint(SEQUENCER::SequenceBindingId bindingId, bool end,
                                                 std::string& message) {
    const auto* sequence = GetAssetSequence();
    const auto* group = sequence ? FindCameraEndpointGroup(bindingId, end ? sequence->durationSeconds : 0.f) : nullptr;
    if (!group) {
        message = "当前端点未关联区域，请从世界地图打开机位或切换演出。";
        return false;
    }
    State before = state_;
    if (!ApplyCameraEndpoint(*group)) {
        state_ = std::move(before);
        message = "关联端点更新失败；没有修改。";
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < state_.linked.size(); ++i)
        changed |= !SameSequence(before.linked[i], state_.linked[i]);
    if (changed)
        history_.Record(std::move(before), state_);
    message = changed ? "已统一此区域的关联机位；保存后生效，可撤销。" : "该区域的关联机位已经一致。";
    return true;
}
bool SequenceEditorDocument::SaveCameraEndpoints(AssetDatabase& database, std::string& message) {
    const auto* sequence = GetAssetSequence();
    for (const auto& group : endpointGroups_)
        for (const auto& endpoint : group.endpoints) {
            const auto* source = EndpointSequence(endpoint);
            BoundCameraPose pose;
            if (!source || !ReadBoundCameraPose(*source, endpoint.bindingId, EndpointTime(*source, endpoint), pose)) {
                message = "关卡机位轨道已移除，无法同步保存。";
                return false;
            }
        }
    // 全資産の世代を先に確認。共有する同一ファイルの両端も、一つの草稿へ合成して一度だけ保存する。
    std::vector<std::pair<AssetGuid, CinematicSequence>> updates{{assetGuid_, *sequence}};
    for (size_t i = 0; i < linkedSources_.size(); ++i) {
        const auto& source = linkedSources_[i];
        if (!source.version.CanSave(message))
            return false;
        if (!SameSequence(source.saved, state_.linked[i]))
            updates.emplace_back(source.guid, state_.linked[i]);
    }
    if (!UpdateSequenceAssets(database, updates, message))
        return false;
    for (size_t i = 0; i < linkedSources_.size(); ++i) {
        linkedSources_[i].saved = state_.linked[i];
        linkedSources_[i].version.Accept(database, linkedSources_[i].guid);
    }
    sourceVersion_.Accept(database, assetGuid_);
    history_.MarkSaved();
    displayName_ = sequence->name;
    return true;
}
} // namespace HIKARI::EDITOR
