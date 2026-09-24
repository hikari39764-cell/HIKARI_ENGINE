#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
namespace HIKARI {
UiAnimationAsset MakeUiAnimationPreset(std::string_view preset) {
    UiAnimationAsset asset;
    const auto add = [&](const char* name, UiMotionTrigger trigger, float duration) -> UiAnimationClip& {
        asset.clips.push_back({asset.clips.size() + 1, name, trigger, duration});
        return asset.clips.back();
    };
    const auto track = [](UiAnimationClip& clip, UiMotionProperty p, float from, float to) {
        UiAnimationTrack t;
        t.id = clip.tracks.size() + 1;
        t.property = p;
        t.curve.value = UiMotionProperties()[static_cast<size_t>(p)].initial;
        t.curve.keys = {{0, from}, {clip.duration, to}};
        clip.tracks.push_back(std::move(t));
    };
    if (preset == "button") {
        asset.name = "Button Feedback";
        for (const auto trigger :
             {UiMotionTrigger::Normal, UiMotionTrigger::Hover, UiMotionTrigger::Press, UiMotionTrigger::Disabled}) {
            auto& clip = add(UiMotionTriggerNames()[static_cast<size_t>(trigger)], trigger, .12f);
            const float scale = trigger == UiMotionTrigger::Hover   ? 1.05f
                                : trigger == UiMotionTrigger::Press ? .96f
                                                                    : 1;
            track(clip, UiMotionProperty::ScaleX, 1, scale);
            track(clip, UiMotionProperty::ScaleY, 1, scale);
            track(clip, UiMotionProperty::Opacity, 1, trigger == UiMotionTrigger::Disabled ? .45f : 1);
        }
    } else {
        asset.name = "Panel Motion";
        auto& open = add("Open", UiMotionTrigger::Open, .3f);
        track(open, UiMotionProperty::Opacity, 0, 1);
        track(open, UiMotionProperty::OffsetY, 24, 0);
        track(open, UiMotionProperty::ScaleX, .95f, 1);
        track(open, UiMotionProperty::ScaleY, .95f, 1);
        auto& close = add("Close", UiMotionTrigger::Close, .2f);
        track(close, UiMotionProperty::Opacity, 1, 0);
        track(close, UiMotionProperty::OffsetY, 0, 16);
    }
    return asset;
}
} // namespace HIKARI
