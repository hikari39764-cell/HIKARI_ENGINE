#pragma once

#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <string>
#include <array>
#include <vector>
#include <span>
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::SEQUENCER {

struct ShotPose {
    float time = 0.0f;
    MATH::Vec3 eye{0.0f, 2.0f, -8.0f};
    MATH::Vec3 target{0.0f, 1.0f, 0.0f};
    float fov = 55.0f;
    float roll = 0.0f;
};

enum class ShotChannel : size_t { EyeX, EyeY, EyeZ, TargetX, TargetY, TargetZ, Fov, Roll, Count };
inline constexpr std::array<const char*, 8> kShotChannelNames{"eyeX",    "eyeY",    "eyeZ", "targetX",
                                                              "targetY", "targetZ", "fov",  "roll"};

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

struct ShotCamera {
    bool owned = false;
    std::string name = "Shot";
    std::array<ScalarCurve, 8> channels{{{0}, {2}, {-8}, {0}, {1}, {0}, {55}, {0}}};
    SequenceBindingId anchor{};
    SequenceBindingId lookAt{};
    MATH::Vec3 targetOffset{};
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

void NormalizeShotCamera(ShotCamera& shot);
void NormalizeCameraMotionEffects(std::vector<CameraMotionEffect>& effects);
void ApplyCameraMotionEffects(std::span<const CameraMotionEffect> effects, float seconds, MATH::Vec3& eye,
                              MATH::Vec3& target, MATH::Vec3& up, float& fovDegrees, float speed = 0,
                              float strength = 1, bool keepTarget = false);
ShotPose EvaluateShotPose(const ShotCamera& shot, float seconds);
void SetShotPose(ShotCamera& shot, const ShotPose& pose, bool keyed);
void NormalizeScreenEffects(std::vector<SequenceScreenEffect>& effects);
void SampleScreenEffects(const std::vector<SequenceScreenEffect>& effects, float seconds,
                         std::vector<ScreenEffectSample>& output);
Camera3D EvaluateShotCamera(const ShotCamera& shot, float seconds, float aspect, const MATH::Mat4* anchor = nullptr,
                            const MATH::Vec3* lookAt = nullptr);
ShotPose CaptureShotPose(const Camera3D& camera, float seconds = 0.0f, const MATH::Mat4* anchor = nullptr);
} // namespace HIKARI::SEQUENCER
