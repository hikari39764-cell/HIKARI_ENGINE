#pragma once
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include <array>
#include <unordered_map>

namespace HIKARI {
class World;
class GameObject;
struct FrameContext;
struct UiVisualPose {
    std::array<float, static_cast<size_t>(UiMotionProperty::Count)> values{0, 0, 1, 1, 0, 1, 1, 1, 1, 1};
    float Get(UiMotionProperty property) const { return values[static_cast<size_t>(property)]; }
};
using UiVisualPoses = std::unordered_map<uint64_t, UiVisualPose>;

// 描画値だけを所有し、編集された Rect や TextStyle に書き戻さない。
class UiAnimationPlayer {
public:
    bool Play(World&, GameObject&, std::shared_ptr<const UiAnimationAsset>, std::string_view clip,
              bool continuous = false);
    bool PlayTrigger(World&, GameObject&, UiMotionTrigger);
    bool SetVisible(World&, GameObject&, bool);
    bool Seek(World&, RuntimeObjectHandle, float seconds);
    bool Pause(RuntimeObjectHandle, bool paused);
    bool Stop(RuntimeObjectHandle);
    void Reset();
    void Tick(World&, const FrameContext&, bool automatic = true);
    void Refresh(World&);
    const UiVisualPoses& Poses() const { return poses_; }
    bool HasInstance(RuntimeObjectHandle root) const { return instances_.contains(root.ToValue()); }
    bool IsClosing(RuntimeObjectHandle) const;
    struct Status {
        std::string clip, error;
        float seconds = 0, duration = 0;
        bool playing = false, closing = false;
    };
    Status GetStatus(RuntimeObjectHandle) const;

private:
    struct Instance {
        RuntimeObjectHandle root;
        std::shared_ptr<const UiAnimationAsset> asset;
        size_t clip = 0;
        float seconds = 0;
        bool playing = true, closing = false;
        std::string error;
        std::unordered_map<uint64_t, float> starts;
    };
    struct AutomaticState {
        std::shared_ptr<const UiAnimationAsset> asset;
        bool visible = false;
        int button = -1;
    };
    static float Evaluate(const Instance&, const UiAnimationTrack&);
    std::unordered_map<uint64_t, Instance> instances_;
    std::unordered_map<uint64_t, AutomaticState> automatic_;
    UiVisualPoses poses_;
};
} // namespace HIKARI
