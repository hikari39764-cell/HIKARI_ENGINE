#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::EditLayerTiming(std::string_view id, VfxLayerTimeEdit mode, float delta,
                                             std::string &message) {
    auto next = document_.Asset();
    if (!EditVfxLayerTime(next, id, mode, delta, message))
        return false;
    return ApplyAsset(std::move(next), message);
}
bool VfxWorkspaceController::MoveLayerKeys(std::span<const std::string> ids, float delta,
                                           std::string &message) {
    auto next = document_.Asset();
    if (!MoveVfxTimelineKeys(next, ids, delta, message))
        return false;
    return ApplyAsset(std::move(next), message);
}

void VfxWorkspaceController::BeginTimelineEdit() {
    if (timelineEdit_.baseline)
        return;
    SealPropertyEdit();
    timelineEdit_.baseline = document_.Asset();
    timelineEdit_.baselineSelection = timelineEdit_.selectedKeys;
    PausePreview(true);
}
void VfxWorkspaceController::EndTimelineEdit(bool cancel) {
    if (cancel && timelineEdit_.baseline) {
        (void)ApplyAsset(*timelineEdit_.baseline, statusMessage_, VfxTimelineEditState::mergeGroup);
        timelineEdit_.selectedKeys = timelineEdit_.baselineSelection;
    }
    timelineEdit_.baseline.reset();
    timelineEdit_.baselineSelection.clear();
    timelineEdit_.draggedLayer.clear();
    timelineEdit_.keyDragging = false;
    timelineEdit_.curveDragging = false;
    timelineEdit_.duplicate = false;
    timelineEdit_.marquee = false;
    viewportHandle_ = {};
    layerGizmoEditing_ = false;
    SealPropertyEdit();
}
void VfxWorkspaceController::CopyTimelineSelection() {
    timelineEdit_.clipboard = CopyVfxTimelineKeys(document_.Asset(), timelineEdit_.selectedKeys);
}
bool VfxWorkspaceController::MoveTimelineSelection(float seconds, bool fromBaseline) {
    auto next = fromBaseline && timelineEdit_.baseline ? *timelineEdit_.baseline : document_.Asset();
    if (!MoveVfxTimelineKeys(next, timelineEdit_.selectedKeys, seconds, statusMessage_))
        return false;
    return ApplyAsset(std::move(next), statusMessage_, fromBaseline ? VfxTimelineEditState::mergeGroup : 0);
}
bool VfxWorkspaceController::DeleteTimelineSelection() {
    if (timelineEdit_.selectedKeys.empty()) return false;
    auto next = document_.Asset();
    for (auto &layer : next.emitters)
        for (const auto &entry : CollectVfxTimelineCurves(layer, true)) {
            auto *curve = ResolveVfxCurve(layer, entry.address);
            const auto removed = std::erase_if(curve->keys, [this](const auto &key) {
                return std::find(timelineEdit_.selectedKeys.begin(), timelineEdit_.selectedKeys.end(),
                                 key.id) != timelineEdit_.selectedKeys.end();
            });
            if (removed && curve->keys.empty()) {
                statusMessage_ = "Keep at least one key in each animated channel";
                return false;
            }
        }
    if (!ApplyAsset(std::move(next), statusMessage_))
        return false;
    timelineEdit_.selectedKeys.clear();
    return true;
}
bool VfxWorkspaceController::PasteTimelineSelection(float seconds, bool merge) {
    if (timelineEdit_.clipboard.empty() || !std::isfinite(seconds))
        return false;
    auto next = document_.Asset();
    std::vector<std::string> pasted;
    for (const auto &source : timelineEdit_.clipboard) {
        auto *layer = FindEmitterMutable(next, source.address.emitterId);
        if (!layer) {
            statusMessage_ = "The copied layer no longer exists";
            return false;
        }
        auto *curve = ResolveVfxCurve(*layer, source.address);
        if (!curve)
            return false;
        float t = 0;
        if (!ResolveVfxLayerKeyTime(next, *layer, seconds + source.relativeSeconds, t, statusMessage_)) {
            return false;
        }
        const auto existing = std::find_if(curve->keys.begin(), curve->keys.end(), [t](const auto &key) {
            return std::abs(key.time - t) < 0.00001f;
        });
        auto key = source.key;
        key.time = t;
        if (existing != curve->keys.end()) {
            key.id = existing->id;
            *existing = key;
        } else {
            key.id = AllocateStableId("timeline.key");
            // 同じApply内でも異なるIDを割り当てる。
            while (std::find(pasted.begin(), pasted.end(), key.id) != pasted.end())
                key.id += "x";
            curve->keys.push_back(key);
        }
        pasted.push_back(key.id);
        std::sort(curve->keys.begin(), curve->keys.end(),
                  [](const auto &a, const auto &b) { return a.time < b.time; });
    }
    if (!ApplyAsset(std::move(next), statusMessage_, merge ? VfxTimelineEditState::mergeGroup : 0))
        return false;
    timelineEdit_.selectedKeys = std::move(pasted);
    return true;
}
} // namespace HIKARI::EDITOR
