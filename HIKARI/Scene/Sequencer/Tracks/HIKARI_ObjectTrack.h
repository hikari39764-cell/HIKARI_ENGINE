#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::SEQUENCER {
// 物体轨：結んだ物体の位置・向き・大きさ（と、物体に灯があればその明るさ）を時間轴の键で動かす。
// 键は物体の親に対する局所の姿勢で、演出の時刻に置く。最初の键より前と最後の键より後はその键の姿勢のまま。
// 演出が止まると物体を元の姿勢へ戻す（restoreOnStop が偽なら Play では最後の姿勢のまま残す。編集器の
// プレビューは常に戻す）。
enum class ObjectKeyInterpolation : uint8_t {
    Smooth, // 前後の键を通る滑らかな曲線（速さが键で途切れない）
    Linear, // 一定の速さ
    Ease,   // 键ごとに止まり、ゆっくり動き出してゆっくり止まる
    Step,   // 次の键まで動かず、键の時刻で跳ぶ
};

struct ObjectTrackKey {
    float time = 0.0f;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    // 灯の明るさの倍率（演出が始まった時の明るさに掛ける）。track.animateLight の時だけ使う。
    float light = 1.0f;
    // この键から次の键までの動き方。
    ObjectKeyInterpolation interpolation = ObjectKeyInterpolation::Smooth;
};

struct ObjectTrack {
    uint64_t id = 1;
    std::string name = "Object";
    bool enabled = true;
    SequenceBindingId target{};
    bool restoreOnStop = true;
    bool animateLight = false;
    std::vector<ObjectTrackKey> keys;
};

struct ObjectTrackSample {
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    float light = 1.0f;
};

inline constexpr size_t kMaximumObjectTracks = 128u;
inline constexpr size_t kMaximumObjectTrackKeys = 512u;

// 键を時刻順に並べ、壊れた値（非有限・負の大きさ）を直す。
void NormalizeObjectTracks(std::vector<ObjectTrack>& tracks);
bool ValidateObjectTracks(const std::vector<ObjectTrack>& tracks, std::string& error);
float GetObjectTracksContentEnd(const std::vector<ObjectTrack>& tracks) noexcept;
// 時刻の姿勢。键が無ければ偽。
bool SampleObjectTrack(const ObjectTrack& track, float timeSeconds, ObjectTrackSample& out) noexcept;
const char* ToString(ObjectKeyInterpolation interpolation) noexcept;
bool ParseObjectKeyInterpolation(const std::string& text, ObjectKeyInterpolation& out) noexcept;
} // namespace HIKARI::SEQUENCER
