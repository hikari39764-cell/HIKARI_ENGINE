#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace HIKARI::SEQUENCER {
namespace {
constexpr float radians = std::numbers::pi_v<float> / 180;
float Safe(float x, float fallback = 0) { return std::isfinite(x) ? x : fallback; }
MATH::Vec3 Rotate(MATH::Vec3 v, MATH::Vec3 axis, float angle) {
    axis = MATH::Normalize(axis);
    return v * std::cos(angle) + MATH::Cross(axis, v) * std::sin(angle) +
        axis * (MATH::Dot(axis, v) * (1 - std::cos(angle)));
}
}

void NormalizeCameraMotionEffects(std::vector<CameraMotionEffect>& effects) {
    std::unordered_set<uint64_t> ids;
    uint64_t next = 1;
    for (auto& effect : effects) {
        if (!effect.id || !ids.insert(effect.id).second) {
            while (ids.contains(next)) ++next;
            effect.id = next; ids.insert(next++);
        }
        NormalizePresentationParameter(effect.amount);
        NormalizePresentationParameter(effect.weight);
        effect.start = std::max(0.0f, Safe(effect.start));
        effect.duration = std::max(0.0f, Safe(effect.duration));
        effect.fadeIn = std::max(0.0f, Safe(effect.fadeIn));
        effect.fadeOut = std::max(0.0f, Safe(effect.fadeOut));
        if (effect.duration > 0) {
            effect.fadeIn = std::min(effect.fadeIn, effect.duration);
            effect.fadeOut = std::min(effect.fadeOut, effect.duration);
        }
        effect.maximumSpeed = std::max(0.01f, Safe(effect.maximumSpeed, 8));
        effect.frequency = std::clamp(Safe(effect.frequency, 3), 0.0f, 100.0f);
    }
}

void ApplyCameraMotionEffects(std::span<const CameraMotionEffect> effects, float seconds,
    MATH::Vec3& eye, MATH::Vec3& target, MATH::Vec3& up, float& fov,
    float speed, float strength, bool keepTarget) {
    if (!std::isfinite(seconds) || !std::isfinite(strength)) return;
    for (const auto& effect : effects) {
        const float local = seconds - effect.start;
        if (!effect.enabled || local < 0 || (effect.duration > 0 && local >= effect.duration)) continue;
        float weight = std::clamp(effect.weight.Evaluate(local), 0.0f, 1.0f) * strength;
        if (effect.fadeIn > 0) weight *= std::clamp(local / effect.fadeIn, 0.0f, 1.0f);
        if (effect.fadeOut > 0 && effect.duration > 0)
            weight *= std::clamp((effect.duration - local) / effect.fadeOut, 0.0f, 1.0f);
        float amount = effect.amount.Evaluate(local) * weight;
        if (!std::isfinite(amount) || amount == 0) continue;
        const auto basis = MATH::BuildViewBasisRH(target - eye, up);
        auto forward = basis.forward, right = basis.right, vertical = basis.up;
        MATH::Vec3 offset{};
        switch (effect.kind) {
        case CameraMotionKind::SpeedDolly:
            amount *= std::clamp(Safe(speed) / std::max(effect.maximumSpeed, .01f), 0.0f, 1.0f);
            [[fallthrough]];
        case CameraMotionKind::Dolly: offset = forward * amount; break;
        case CameraMotionKind::Truck: offset = right * amount; break;
        case CameraMotionKind::Pedestal: offset = vertical * amount; break;
        case CameraMotionKind::Orbit:
            eye = target + Rotate(eye - target, up, amount * radians); break;
        case CameraMotionKind::Pan:
            target = eye + Rotate(target - eye, vertical, amount * radians); break;
        case CameraMotionKind::Tilt:
            target = eye + Rotate(target - eye, right, amount * radians); break;
        case CameraMotionKind::Roll:
            up = Rotate(vertical, forward, amount * radians); break;
        case CameraMotionKind::SpeedFov:
            amount *= std::clamp(Safe(speed) / std::max(effect.maximumSpeed, .01f), 0.0f, 1.0f);
            [[fallthrough]];
        case CameraMotionKind::Fov: fov += amount; break;
        case CameraMotionKind::Shake:
        case CameraMotionKind::Handheld: {
            const float t = local * effect.frequency * 2 * std::numbers::pi_v<float>;
            const float phase = static_cast<float>(effect.seed % 997) * .17f;
            offset = (right * std::sin(t + phase) + vertical * std::sin(t * 1.37f + phase * 2)) * amount;
            if (effect.kind == CameraMotionKind::Handheld) {
                offset = offset * .3f + (right * std::sin(t * .19f + phase) +
                    vertical * std::sin(t * .27f + phase)) * amount * .7f;
                up = Rotate(vertical, forward, std::sin(t * .23f + phase) * amount * 8 * radians);
            }
            break;
        }
        }
        eye = eye + offset;
        if (!keepTarget || effect.kind == CameraMotionKind::Shake || effect.kind == CameraMotionKind::Handheld)
            target = target + offset;
    }
    fov = std::clamp(Safe(fov, 60), 1.0f, 179.0f);
    const auto basis = MATH::BuildViewBasisRH(target - eye, up);
    up = basis.up;
}
} // namespace HIKARI::SEQUENCER
