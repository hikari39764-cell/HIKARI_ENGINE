#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "HIKARI/Project/FrameContext.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace HIKARI {
namespace {
bool Visible(GameObject& root) {
    for (auto* object = &root; object; object = object->GetParent()) {
        if (auto* group = object->GetComponent<UiGroupComponent>(); group && !group->IsVisible())
            return false;
        if (auto* canvas = object->GetComponent<UiCanvasComponent>(); canvas && !canvas->IsEnabled())
            return false;
    }
    return true;
}
UiMotionTrigger ButtonTrigger(UiButtonVisualState state) {
    switch (state) {
    case UiButtonVisualState::Hovered: return UiMotionTrigger::Hover;
    case UiButtonVisualState::Pressed: return UiMotionTrigger::Press;
    case UiButtonVisualState::Disabled: return UiMotionTrigger::Disabled;
    default: return UiMotionTrigger::Normal;
    }
}
std::shared_ptr<const UiAnimationAsset> ButtonFeedback(const UiButtonComponent& button) {
    auto asset = std::make_shared<UiAnimationAsset>();
    UiAnimationClip clip;
    clip.name = "Button feedback";
    clip.duration = std::max(.001f, button.GetTransitionSeconds());
    const auto tint = button.GetStateTint();
    const UiMotionProperty properties[]{UiMotionProperty::Red, UiMotionProperty::Green, UiMotionProperty::Blue,
                                        UiMotionProperty::Opacity};
    const float values[]{tint.r, tint.g, tint.b, tint.a};
    for (size_t i = 0; i < 4; ++i) {
        UiAnimationTrack track;
        track.id = i + 1;
        track.property = properties[i];
        track.curve.keys = {{0, 1, 0, 0}, {clip.duration, values[i], 0, 0}};
        clip.tracks.push_back(std::move(track));
    }
    asset->clips.push_back(std::move(clip));
    return asset;
}
} // namespace
void UiAnimationPlayer::Tick(World& world, const FrameContext& frame, bool automatic) {
    if (automatic) {
        std::unordered_set<uint64_t> live;
        auto update = [&](GameObject& root, UiAnimatorComponent* animator, UiButtonComponent* button) {
            const auto id = root.GetRuntimeHandle().ToValue();
            live.insert(id);
            auto& state = automatic_[id];
            const bool visible = Visible(root);
            std::string error;
            auto* store = world.Services().Find<UiAnimationAssetStore>();
            const auto asset =
                animator && animator->IsEnabled() && store ? store->Load(animator->GetAnimation(), error) : nullptr;
            const bool changedAsset = state.asset != asset;
            if (changedAsset) {
                const auto running = instances_.find(id);
                if (running != instances_.end() && running->second.asset != asset)
                    Stop(root.GetRuntimeHandle());
                state.button = -1;
            }
            if (!visible)
                Stop(root.GetRuntimeHandle());
            else if (asset && (!state.visible || changedAsset) && !instances_.contains(id))
                PlayTrigger(world, root, UiMotionTrigger::Open);
            const int visual = button ? static_cast<int>(button->GetVisualState()) : -1;
            if (button && visible && !IsClosing(root.GetRuntimeHandle()) && visual != state.button &&
                (state.button >= 0 || !instances_.contains(id))) {
                if (!PlayTrigger(world, root, ButtonTrigger(button->GetVisualState())) && !asset) {
                    auto fallback = ButtonFeedback(*button);
                    Play(world, root, fallback, fallback->clips.front().name, true);
                }
            }
            state.asset = asset;
            state.visible = visible;
            state.button = visible ? visual : -1;
        };
        (void)world.ForEachObjectWith<UiAnimatorComponent>([&](GameObject& root, UiAnimatorComponent& animator) {
            update(root, &animator, root.GetComponent<UiButtonComponent>());
        });
        (void)world.ForEachObjectWith<UiButtonComponent>([&](GameObject& root, UiButtonComponent& button) {
            if (!root.GetComponent<UiAnimatorComponent>())
                update(root, nullptr, &button);
        });
        std::erase_if(automatic_, [&](const auto& entry) { return !live.contains(entry.first); });
        std::erase_if(instances_, [&](const auto& entry) { return !live.contains(entry.first); });
    }
    for (auto& [id, instance] : instances_) {
        auto* root = world.FindObject(instance.root);
        if (!root || !instance.playing)
            continue;
        const auto* animator = root->GetComponent<UiAnimatorComponent>();
        const float dt = animator && !animator->IsUnscaled() ? frame.gameDt : frame.unscaledDt;
        if (!std::isfinite(dt) || dt <= 0)
            continue;
        const auto& clip = instance.asset->clips[instance.clip];
        const double elapsed =
            static_cast<double>(instance.seconds) + dt * static_cast<double>(animator ? animator->GetSpeed() : 1);
        instance.seconds = static_cast<float>(clip.loop ? std::fmod(elapsed, clip.duration)
                                                        : std::min(elapsed, static_cast<double>(clip.duration)));
        if (!clip.loop && instance.seconds >= clip.duration) {
            instance.playing = false;
            if (instance.closing) {
                if (auto* group = root->GetComponent<UiGroupComponent>())
                    group->SetVisible(false);
                instance.closing = false;
            }
        }
    }
    Refresh(world);
}
} // namespace HIKARI
