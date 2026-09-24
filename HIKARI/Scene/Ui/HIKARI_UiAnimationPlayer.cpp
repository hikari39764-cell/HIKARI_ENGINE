#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include <algorithm>
#include <cmath>

namespace HIKARI {
namespace {
using Slots = std::unordered_map<std::string, RuntimeObjectHandle>;
Slots ResolveSlots(GameObject& root) {
    Slots slots{{"self", root.GetRuntimeHandle()}};
    std::vector<GameObject*> pending(root.GetChildren().begin(), root.GetChildren().end());
    while (!pending.empty()) {
        auto* object = pending.back();
        pending.pop_back();
        if (auto* target = object->GetComponent<UiAnimationTargetComponent>(); target && !target->GetSlot().empty()) {
            if (!slots.emplace(target->GetSlot(), object->GetRuntimeHandle()).second)
                slots[target->GetSlot()] = {}; // 重複は先勝ちにせず明示的に無効化する。
        }
        if (!object->GetComponent<UiAnimatorComponent>())
            pending.insert(pending.end(), object->GetChildren().begin(), object->GetChildren().end());
    }
    return slots;
}
} // namespace
bool UiAnimationPlayer::Play(World& world, GameObject& root, std::shared_ptr<const UiAnimationAsset> asset,
                             std::string_view name, bool continuous) {
    if (!asset || (!root.GetComponent<UiRectComponent>() && !root.GetComponent<UiCanvasComponent>()))
        return false;
    auto clip =
        std::find_if(asset->clips.begin(), asset->clips.end(), [&](const auto& value) { return value.name == name; });
    if (clip == asset->clips.end())
        return false;
    const auto slots = ResolveSlots(root);
    Instance next;
    next.root = root.GetRuntimeHandle();
    next.clip = static_cast<size_t>(clip - asset->clips.begin());
    next.asset = std::move(asset);
    const auto previous = instances_.find(next.root.ToValue());
    for (const auto& track : clip->tracks) {
        const auto found = slots.find(track.slot);
        if (found == slots.end() || !found->second.IsValid()) {
            // 失敗した Play は実行中のインスタンスを置き換えない。
            return false;
        }
        if (continuous) {
            float value = UiVisualPose{}.Get(track.property);
            // 親 Animator の寄与を再び取り込まない。切り替えるインスタンスだけを評価する。
            if (previous != instances_.end()) {
                const auto& old = previous->second;
                for (const auto& oldTrack : old.asset->clips[old.clip].tracks) {
                    const auto oldSlot = slots.find(oldTrack.slot);
                    if (oldTrack.property == track.property && oldSlot != slots.end() &&
                        oldSlot->second == found->second) {
                        value = Evaluate(old, oldTrack);
                        break;
                    }
                }
            }
            next.starts[track.id] = value;
        }
    }
    instances_[next.root.ToValue()] = std::move(next);
    Refresh(world);
    return true;
}
bool UiAnimationPlayer::PlayTrigger(World& world, GameObject& root, UiMotionTrigger trigger) {
    const auto* animator = root.GetComponent<UiAnimatorComponent>();
    auto* store = world.Services().Find<UiAnimationAssetStore>();
    if (!animator || !animator->IsEnabled() || !store)
        return false;
    std::string error;
    auto asset = store->Load(animator->GetAnimation(), error);
    if (!asset)
        return false;
    for (const auto& clip : asset->clips) {
        if (clip.trigger == trigger)
            return Play(world, root, asset, clip.name,
                        trigger != UiMotionTrigger::Open && trigger != UiMotionTrigger::Manual);
    }
    return false;
}
bool UiAnimationPlayer::SetVisible(World& world, GameObject& root, bool visible) {
    auto* group = root.GetComponent<UiGroupComponent>();
    if (!group)
        return false;
    const auto handle = root.GetRuntimeHandle();
    if (visible) {
        if (!group->IsVisible() || IsClosing(handle)) {
            group->SetVisible(true);
            Stop(handle);
            PlayTrigger(world, root, UiMotionTrigger::Open);
        }
    } else if (group->IsVisible() && !IsClosing(handle)) {
        if (PlayTrigger(world, root, UiMotionTrigger::Close))
            instances_.at(handle.ToValue()).closing = true;
        else {
            group->SetVisible(false);
            Stop(handle);
        }
    }
    if (auto state = automatic_.find(handle.ToValue()); state != automatic_.end())
        state->second.visible = group->IsVisible();
    Refresh(world);
    return true;
}
bool UiAnimationPlayer::Seek(World& world, RuntimeObjectHandle root, float seconds) {
    auto found = instances_.find(root.ToValue());
    if (found == instances_.end() || !std::isfinite(seconds))
        return false;
    auto& instance = found->second;
    instance.seconds = std::clamp(seconds, 0.f, instance.asset->clips[instance.clip].duration);
    instance.starts.clear(); // スクラブは履歴に依存しない。
    Refresh(world);
    return true;
}
bool UiAnimationPlayer::Pause(RuntimeObjectHandle root, bool paused) {
    auto found = instances_.find(root.ToValue());
    if (found == instances_.end())
        return false;
    found->second.playing = !paused;
    return true;
}
bool UiAnimationPlayer::Stop(RuntimeObjectHandle root) {
    const bool removed = instances_.erase(root.ToValue()) != 0;
    // Pose は複数ルートから合成されるため次の Refresh で更新する。
    return removed;
}
void UiAnimationPlayer::Reset() {
    instances_.clear();
    automatic_.clear();
    poses_.clear();
}
bool UiAnimationPlayer::IsClosing(RuntimeObjectHandle root) const {
    const auto found = instances_.find(root.ToValue());
    return found != instances_.end() && found->second.closing;
}
UiAnimationPlayer::Status UiAnimationPlayer::GetStatus(RuntimeObjectHandle root) const {
    const auto found = instances_.find(root.ToValue());
    if (found == instances_.end())
        return {};
    const auto& i = found->second;
    const auto& clip = i.asset->clips[i.clip];
    return {clip.name, i.error, i.seconds, clip.duration, i.playing, i.closing};
}
float UiAnimationPlayer::Evaluate(const Instance& instance, const UiAnimationTrack& track) {
    const auto& property = UiMotionProperties()[static_cast<size_t>(track.property)];
    float value = track.curve.Evaluate(instance.seconds);
    if (auto start = instance.starts.find(track.id); start != instance.starts.end() && !track.curve.keys.empty()) {
        const auto& keys = track.curve.keys;
        const float end = keys.size() > 1 ? keys[1].time : instance.asset->clips[instance.clip].duration;
        if (instance.seconds < end && end > 0) {
            const float t = std::clamp(instance.seconds / end, 0.f, 1.f);
            value += (start->second - track.curve.Evaluate(0)) * (1 - t * t * (3 - 2 * t));
        }
    }
    return std::clamp(std::isfinite(value) ? value : property.initial, property.minimum, property.maximum);
}
void UiAnimationPlayer::Refresh(World& world) {
    poses_.clear();
    std::erase_if(instances_, [&](auto& entry) { return world.FindObject(entry.second.root) == nullptr; });
    for (auto& [id, instance] : instances_) {
        auto* object = world.FindObject(instance.root);
        const auto slots = ResolveSlots(*object);
        const auto& clip = instance.asset->clips[instance.clip];
        instance.error.clear();
        for (const auto& track : clip.tracks) {
            const auto found = slots.find(track.slot);
            if (found == slots.end() || !found->second.IsValid()) {
                instance.error = "Missing or duplicate animation slot: " + track.slot;
                continue;
            }
            const auto index = static_cast<size_t>(track.property);
            if (index >= UiMotionProperties().size())
                continue;
            const auto& property = UiMotionProperties()[index];
            const float value = Evaluate(instance, track);
            auto& target = poses_[found->second.ToValue()].values[index];
            if (property.initial == 0)
                target += value;
            else
                target *= value;
        }
    }
}
} // namespace HIKARI
