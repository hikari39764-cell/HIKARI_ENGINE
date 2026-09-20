#pragma once

#include <string>
#include <vector>
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

namespace HIKARI::SEQUENCER {

    struct PresentationKey {
        float time = 0.0f;
        float value = 0.0f;
        float inSlope = 0.0f;
        float outSlope = 0.0f;
    };

    struct PresentationParameter {
        float value = 0.0f;
        std::vector<PresentationKey> keys;
        float Evaluate(float seconds) const noexcept;
    };

    struct ShotPose {
        float time = 0.0f;
        MATH::Vec3 eye{0.0f, 2.0f, -8.0f};
        MATH::Vec3 target{0.0f, 1.0f, 0.0f};
        float fov = 55.0f;
        float roll = 0.0f;
        // 正規化した区間進行度の接線。0/0 はイーズ、1/1 は等速。
        float outgoing = 0.0f;
        float incoming = 0.0f;
    };

    enum class CameraMotionKind { Dolly, Truck, Pedestal, Orbit, Pan, Tilt, Roll, Shake, Handheld };

    struct CameraMotionEffect {
        uint64_t id = 1;
        CameraMotionKind kind = CameraMotionKind::Dolly;
        bool enabled = true;
        PresentationParameter amount{1.0f};
        PresentationParameter weight{1.0f};
        float frequency = 3.0f;
        uint32_t seed = 1;
    };

    struct ShotCamera {
        bool owned = false;
        std::string name = "Shot";
        std::vector<ShotPose> poses{ShotPose{}};
        SequenceBindingId anchor{};
        SequenceBindingId lookAt{};
        MATH::Vec3 targetOffset{};
        float nearClip = 0.1f;
        float farClip = 2000.0f;
        std::vector<CameraMotionEffect> motion;
    };

    enum class ScreenEffectKind {
        BloomIntensity, BloomThreshold, Exposure, Saturation, Contrast,
        Temperature, Vignette, Monochrome, Tint, Fade, Pixelate, Chromatic
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
        PresentationParameter amount{0.5f};
        PresentationParameter weight{1.0f};
        MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    };

    struct ScreenEffectSample {
        ScreenEffectKind kind{};
        float amount = 0.0f;
        float weight = 0.0f;
        MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    };

    void NormalizeShotCamera(ShotCamera& shot);
    void NormalizeScreenEffects(std::vector<SequenceScreenEffect>& effects);
    void SampleScreenEffects(const std::vector<SequenceScreenEffect>& effects,
        float seconds, std::vector<ScreenEffectSample>& output);
    Camera3D EvaluateShotCamera(const ShotCamera& shot, float seconds, float aspect,
        const MATH::Mat4* anchor = nullptr, const MATH::Vec3* lookAt = nullptr);
    ShotPose CaptureShotPose(const Camera3D& camera, float seconds = 0.0f,
        const MATH::Mat4* anchor = nullptr);
}
