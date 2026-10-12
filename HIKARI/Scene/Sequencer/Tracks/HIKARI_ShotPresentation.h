#pragma once

#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <string>
#include <array>
#include <vector>
#include <span>
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::SEQUENCER {

// 镜头の姿勢（一つの時刻の位置・向き・画角）。向きは場面の摄像机物体と同じ約束（局所 +Z が前、+Y が上）の四元数で、
// 傾き（roll）も向きに含む。位置と向きは anchor の局所空間（anchor が無ければ世界空間）。
struct ShotPose {
    float time = 0.0f;
    MATH::Vec3 position{0.0f, 2.0f, -8.0f};
    MATH::Quat rotation = MATH::Quat::Identity();
    float fov = 55.0f;
};

// 镜头の軌道（anchor の局所空間の通過点を滑らかに結ぶ）。点が 2 つ以上あると位置は軌道上の道のりの割合 railT（0..1）
// で決まり、姿勢键の位置は使わない（向きと画角は姿勢键のまま）。
struct ShotRail {
    std::vector<MATH::Vec3> points;
    bool loop = false;
};

enum class CameraMotionKind {
    Dolly,
    Truck,
    Pedestal,
    Orbit,
    Pan,
    Tilt,
    Roll,
    Shake,
    Handheld,
    Fov,
    SpeedFov,
    SpeedDolly
};

struct CameraMotionDescriptor {
    CameraMotionKind kind;
    const char* key;
    const char* group;
    float defaultAmount;
    bool oscillating = false;
    bool speedDriven = false;
};
inline constexpr std::array kCameraMotionDescriptors{
    CameraMotionDescriptor{CameraMotionKind::Dolly, "cine.dolly", "cm.position", 0.3f},
    CameraMotionDescriptor{CameraMotionKind::Truck, "cine.truck", "cm.position", 0.3f},
    CameraMotionDescriptor{CameraMotionKind::Pedestal, "cine.pedestal", "cm.position", 0.2f},
    CameraMotionDescriptor{CameraMotionKind::Orbit, "cine.orbit", "cm.rotation", 10},
    CameraMotionDescriptor{CameraMotionKind::Pan, "cine.pan", "cm.rotation", 5},
    CameraMotionDescriptor{CameraMotionKind::Tilt, "cine.tilt", "cm.rotation", 5},
    CameraMotionDescriptor{CameraMotionKind::Roll, "cine.roll", "cm.rotation", 3},
    CameraMotionDescriptor{CameraMotionKind::Shake, "cine.shake", "cm.movement", 0.06f, true},
    CameraMotionDescriptor{CameraMotionKind::Handheld, "cine.handheld", "cm.movement", 0.025f, true},
    CameraMotionDescriptor{CameraMotionKind::Fov, "cm.fov", "cm.lens", 5},
    CameraMotionDescriptor{CameraMotionKind::SpeedFov, "gc.speedFov", "cm.speed", 12, false, true},
    CameraMotionDescriptor{CameraMotionKind::SpeedDolly, "cm.speedDolly", "cm.speed", -0.6f, false, true}};

struct CameraMotionEffect {
    uint64_t id = 1;
    CameraMotionKind kind = CameraMotionKind::Dolly;
    bool enabled = true;
    ScalarCurve amount{1.0f};
    ScalarCurve weight{1.0f};
    float frequency = 3.0f;
    uint32_t seed = 1;
    // duration == 0 は親の再生期間全体。キーは開始位置からの秒数。
    float start = 0, duration = 0, fadeIn = 0, fadeOut = 0;
    float maximumSpeed = 8;
};

// 演出内の镜头。層で組む：身体（姿勢键か軌道、anchor の局所空間）→ 瞄准（lookAt の相手へ向きを寄せる）→
// 運動効果（手持ち・衝撃・相対的な推し引き）→ レンズ（near/far）。
struct ShotCamera {
    bool owned = false;
    std::string name = "Shot";
    // 姿勢键（時刻順、镜头の局所時刻）。空なら base の姿勢のまま。
    ShotPose base{};
    std::vector<ShotPose> keys;
    // 键の間を滑らかに通す：途中の键は前後の键から求めた速さで止まらずに通り、最初と最後の键だけ緩やかに加減速する。
    // 切ると键の間は一定の速さ（位置は直線、向きは最短の回転）。
    bool smoothPath = true;
    ShotRail rail{};
    ScalarCurve railT{0.0f};
    SequenceBindingId anchor{};
    SequenceBindingId lookAt{};
    MATH::Vec3 targetOffset{};
    // lookAt がある時、向きをどれだけ相手へ寄せるか（0 = 姿勢键の向きのまま、1 = 常に相手を見る）。
    ScalarCurve aimWeight{1.0f};
    // 相手に結んでいない時の注視点までの距離 [m]。環繞などの運動効果はこの点を中心に回る。
    float pivotDistance = 5.0f;
    float nearClip = 0.1f;
    float farClip = 2000.0f;
    std::vector<CameraMotionEffect> motion;
};

enum class ScreenEffectKind {
    BloomIntensity,
    BloomThreshold,
    Exposure,
    Saturation,
    Contrast,
    Temperature,
    Vignette,
    Monochrome,
    Tint,
    Fade,
    Pixelate,
    Chromatic
};

struct SequenceScreenEffect {
    uint64_t id = 1;
    std::string name;
    ScreenEffectKind kind = ScreenEffectKind::BloomIntensity;
    bool enabled = true;
    float start = 0.0f;
    float duration = 3.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
    ScalarCurve amount{0.5f};
    ScalarCurve weight{1.0f};
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
};

struct ScreenEffectSample {
    ScreenEffectKind kind{};
    float amount = 0.0f;
    float weight = 0.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
};

// 姿勢键を同じ時刻とみなす幅 [s]。時間轴の 1/60 秒刻みより十分小さい。
inline constexpr float kShotPoseKeyEpsilon = 0.0005f;

void NormalizeShotCamera(ShotCamera& shot);
void NormalizeCameraMotionEffects(std::vector<CameraMotionEffect>& effects);
void ApplyCameraMotionEffects(std::span<const CameraMotionEffect> effects, float seconds, MATH::Vec3& eye,
                              MATH::Vec3& target, MATH::Vec3& up, float& fovDegrees, float speed = 0,
                              float strength = 1, bool keepTarget = false);
// 姿勢键（と軌道）だけから求めた anchor の局所空間の姿勢。瞄准と運動効果は含まない。
ShotPose EvaluateShotPose(const ShotCamera& shot, float seconds);
// pose.time に姿勢键を置く（同じ時刻の键は置き換える）。keyed が偽で键が一つも無ければ base を変える。
void SetShotPose(ShotCamera& shot, const ShotPose& pose, bool keyed);
const ShotPose* FindShotPoseKey(const ShotCamera& shot, float time) noexcept;
ShotPose* FindShotPoseKey(ShotCamera& shot, float time) noexcept;
// time の姿勢键を newTime へ動かす。動かす键が無いか newTime に別の键があれば false。
bool MoveShotPoseKey(ShotCamera& shot, float time, float newTime);
// time の姿勢键を消す。最後の键を消した時は消す直前の姿勢を base に残す。
bool RemoveShotPoseKey(ShotCamera& shot, float time);
// 軌道上の位置。t は始点から終点までの道のりの割合（0..1）。点が 1 つならその点、無ければ原点。
MATH::Vec3 EvaluateShotRail(const ShotRail& rail, float t);
void NormalizeScreenEffects(std::vector<SequenceScreenEffect>& effects);
void SampleScreenEffects(const std::vector<SequenceScreenEffect>& effects, float seconds,
                         std::vector<ScreenEffectSample>& output);
// 姿勢を世界空間の摄像机にする。anchor は局所→世界、lookAt は瞄准の相手の世界位置（targetOffset を足す）。
Camera3D EvaluateShotCamera(const ShotCamera& shot, float seconds, float aspect, const MATH::Mat4* anchor = nullptr,
                            const MATH::Vec3* lookAt = nullptr);
// 摄像机の姿勢を（anchor があればその局所空間の）键の形にする。
ShotPose CaptureShotPose(const Camera3D& camera, float seconds = 0.0f, const MATH::Mat4* anchor = nullptr);
// 姿勢の向きの前（局所 +Z）と上（局所 +Y）。
MATH::Vec3 ShotPoseForward(const ShotPose& pose);
MATH::Vec3 ShotPoseUp(const ShotPose& pose);
// 前を forward へ向け直した向き。水平に構えた時の上向きからの傾き（roll）は previous のまま保つ。
MATH::Quat ShotRotationLookingAlong(const MATH::Vec3& forward, const MATH::Quat& previous);
} // namespace HIKARI::SEQUENCER
