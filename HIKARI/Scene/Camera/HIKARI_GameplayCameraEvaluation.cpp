#include "Scene/Camera/HIKARI_GameplayCameraEvaluation.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

namespace HIKARI::CAMERA {
uint64_t PlayGameplayCameraEffect(GameObject& camera, CameraFollowComponent& component, std::string_view id,
                                  float strength, std::string& error) {
    error.clear();
    if (!std::isfinite(strength) || strength < 0 || id.empty()) {
        error = "Invalid camera effect request.";
        return 0;
    }
    auto& state = component.GameplayState();
    auto* world = camera.GetWorld();
    auto* store = world ? world->Services().Find<GameplayCameraAssetStore>() : nullptr;
    auto source = state.previewSource ? state.previewSource
                  : store             ? store->Load(component.GameplayBinding().profile, error)
                                      : nullptr;
    const auto* effect = source ? source->FindEffect(id) : nullptr;
    if (!effect) {
        error = "Camera effect not found: " + std::string(id);
        return 0;
    }
    static std::atomic<uint64_t> next{1};
    const auto token = next.fetch_add(1, std::memory_order_relaxed);
    state.effects.push_back({token, *effect, 0, strength});
    return token;
}
bool StopGameplayCameraEffect(CameraFollowComponent& component, uint64_t token) {
    return token &&
           std::erase_if(component.GameplayState().effects, [&](const auto& e) { return e.token == token; }) > 0;
}

bool PrepareGameplayCamera(GameObject& camera, CameraFollowComponent& component, const INPUT::InputSnapshot* input,
                           float seconds) {
    auto& state = component.GameplayState();
    const auto& binding = component.GameplayBinding();
    if (!binding.profile.IsValid() && !state.previewSource) {
        component.ClearEvaluatedSettings();
        state.Reset();
        return false;
    }
    state.error.clear();
    auto* store = camera.GetWorld()->Services().Find<GameplayCameraAssetStore>();
    auto source = state.previewSource ? state.previewSource
                  : store             ? store->Load(binding.profile, state.error)
                                      : nullptr;
    if (!source) {
        if (!store)
            state.error = "Gameplay camera asset service is unavailable.";
        state.source.reset();
        state.hasPose = false;
        state.activeMode.clear();
        component.ClearEvaluatedSettings();
        return false;
    }
    const auto* selected =
        source->FindMode(state.requestedMode.empty() ? (state.baseMode.empty() ? binding.initialMode : state.baseMode)
                                                     : state.requestedMode);
    state.reason = state.requestedMode.empty() ? (state.baseMode.empty() ? "default" : "flow") : "requested";
    if (!selected)
        selected = source->FindMode(source->defaultMode);
    const std::string baseId = selected ? selected->id : source->defaultMode;
    if (state.requestedMode.empty() && input) {
        const GameplayCameraMode* triggered = nullptr;
        for (const auto& mode : source->modes)
            if (!mode.activationAction.empty() && input->IsDown(mode.activationAction) &&
                (mode.activationFromModes.empty() ||
                 std::find(mode.activationFromModes.begin(), mode.activationFromModes.end(), baseId) !=
                     mode.activationFromModes.end()) &&
                (!triggered || mode.priority > triggered->priority))
                triggered = &mode;
        if (triggered) {
            selected = triggered;
            state.reason = triggered->activationAction;
        }
    }
    if (state.requestedMode.empty()) {
        if (const auto* rigs = camera.GetWorld()->Services().Find<CameraRigService>()) {
            if (const auto* request = rigs->ResolveMode(camera.GetDocumentId())) {
                if (const auto* mode = source->FindMode(request->mode)) {
                    selected = mode;
                    state.reason = "gameplay request";
                } else
                    state.error = "Requested camera mode does not exist: " + request->mode;
            }
        }
    }
    if (!selected)
        return false;
    state.changed = state.source != source || state.activeMode != selected->id;
    if (state.changed) {
        if (state.source && state.mode.follow.orbitInputEnabled && state.mode.mount != CameraMount::Fixed &&
            component.Runtime().initialized) {
            state.userYaw = component.Runtime().yawDegrees;
            state.userPitch = component.Runtime().pitchDegrees;
            state.hasUserOrbit = true;
        }
        state.transitionPose = state.previousPose;
        state.transitionElapsed = 0;
        if (state.activeMode != selected->id)
            state.elapsed = 0;
        state.source = std::move(source);
        state.activeMode = selected->id;
        state.mode = *selected;
        if (binding.overrides.contains(selected->id)) {
            auto json = WriteGameplayCameraProfile(*state.source);
            for (auto& mode : json["modes"])
                if (mode["id"] == selected->id)
                    mode.merge_patch(binding.overrides.at(selected->id));
            GameplayCameraProfile resolved;
            if (ReadGameplayCameraProfile(json, resolved, state.error)) {
                if (const auto* effective = resolved.FindMode(selected->id))
                    state.mode = *effective;
            }
        }
    }
    if (input && !state.source->shoulderSwapAction.empty() && input->IsPressed(state.source->shoulderSwapAction))
        state.shoulderSign *= -1;
    state.elapsed += (std::max)(seconds, 0.0f);
    auto settings = state.mode.follow;
    // 肩越しと一人称の最終位置は後段で解決する。入力軌道は同じソルバを使う。
    if (state.mode.mount == CameraMount::Fixed)
        settings.orbitInputEnabled = false;
    component.SetEvaluatedSettings(settings);
    return true;
}

void ApplyGameplayCameraPose(const GameObject& camera, CameraFollowComponent& component, const MATH::Vec3& pivot,
                             float targetSpeed, float seconds, CameraRigPose& pose) {
    auto& state = component.GameplayState();
    if (!state.source)
        return;
    const auto& mode = state.mode;
    if (state.previewSource && state.previewSpeed >= 0)
        targetSpeed = state.previewSpeed;
    auto forward = MATH::Normalize(pose.target - pose.eye);
    if (MATH::Length(forward) < 0.001f)
        forward = {0, 0, 1};
    auto basis = MATH::BuildViewBasisRH(forward, pose.up);
    if (mode.mount == CameraMount::FirstPerson) {
        pose.eye = pivot;
        pose.target = pivot + forward;
    } else if (mode.mount == CameraMount::Fixed) {
        const auto matrix = camera.GetTransform().GetWorldMatrix();
        pose.eye = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
        pose.target = pivot;
    }
    const auto shoulder = basis.right * (mode.shoulderOffset * state.shoulderSign);
    pose.eye = pose.eye + shoulder;
    pose.target = pose.target + shoulder;
    float fov =
        pose.overrideVerticalFov ? pose.verticalFovRadians * 180 / std::numbers::pi_v<float> : mode.verticalFovDegrees;
    SEQUENCER::ApplyCameraMotionEffects(mode.effects, state.elapsed, pose.eye, pose.target, pose.up, fov, targetSpeed);
    pose.overrideVerticalFov = true;
    pose.verticalFovRadians = fov * std::numbers::pi_v<float> / 180;
    pose.overrideClipPlanes = true;
    pose.nearClip = mode.nearClip;
    pose.farClip = mode.farClip;
    if (state.hasPose && state.transitionElapsed < mode.blendSeconds) {
        state.transitionElapsed += (std::max)(seconds, 0.0f);
        const float t = std::clamp(state.transitionElapsed / (std::max)(mode.blendSeconds, 0.001f), 0.0f, 1.0f);
        const float weight = t * t * (3 - 2 * t);
        pose.eye = state.transitionPose.eye + (pose.eye - state.transitionPose.eye) * weight;
        pose.target = state.transitionPose.target + (pose.target - state.transitionPose.target) * weight;
        pose.up = MATH::Normalize(state.transitionPose.up + (pose.up - state.transitionPose.up) * weight);
        pose.verticalFovRadians = state.transitionPose.verticalFovRadians +
                                  (pose.verticalFovRadians - state.transitionPose.verticalFovRadians) * weight;
    }
    // 過渡の基底には一時効果を焼き込まない。
    state.previousPose = pose;
    state.hasPose = true;
    fov = pose.verticalFovRadians * 180 / std::numbers::pi_v<float>;
    for (auto& playback : state.effects) {
        if (playback.elapsed < playback.effect.duration) {
            SEQUENCER::ApplyCameraMotionEffects(playback.effect.effects, playback.elapsed, pose.eye, pose.target,
                                                pose.up, fov, targetSpeed, playback.strength);
            playback.elapsed += std::max(seconds, 0.0f);
        }
    }
    std::erase_if(state.effects, [](const auto& e) { return e.elapsed >= e.effect.duration; });
    pose.verticalFovRadians = fov * std::numbers::pi_v<float> / 180;
    state.outputPose = pose;
}
} // namespace HIKARI::CAMERA
