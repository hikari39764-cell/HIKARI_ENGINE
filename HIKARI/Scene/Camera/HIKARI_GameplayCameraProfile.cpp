#include "Scene/Camera/HIKARI_GameplayCameraProfile.h"
#include "Scene/Sequencer/Serialization/HIKARI_ShotPresentationJson.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace HIKARI::CAMERA {
const GameplayCameraMode* GameplayCameraProfile::FindMode(std::string_view id) const noexcept {
    const auto it = std::find_if(modes.begin(), modes.end(), [id](const auto& mode) { return mode.id == id; });
    return it == modes.end() ? nullptr : &*it;
}

const GameplayCameraEffect* GameplayCameraProfile::FindEffect(std::string_view id) const noexcept {
    const auto it = std::find_if(effects.begin(), effects.end(), [&](const auto& e) { return e.id == id; });
    return it == effects.end() ? nullptr : &*it;
}

bool ValidateGameplayCameraProfile(const GameplayCameraProfile& profile, std::string& error) {
    auto fail = [&](const char* message) {
        error = message;
        return false;
    };
    if (profile.modes.empty() || profile.modes.size() > 128)
        return fail("A camera profile needs 1–128 modes.");
    if (profile.name.empty())
        return fail("Camera profile name is empty.");
    if (!std::isfinite(profile.cinematicEnterSeconds) || !std::isfinite(profile.cinematicReturnSeconds) ||
        profile.cinematicEnterSeconds < 0 || profile.cinematicReturnSeconds < 0)
        return fail("Invalid cinematic handoff duration.");
    const auto validStack = [](const std::vector<SEQUENCER::CameraMotionEffect>& stack) {
        std::unordered_set<uint64_t> seen;
        for (const auto& e : stack) {
            if (!e.id || !seen.insert(e.id).second ||
                static_cast<size_t>(e.kind) >= SEQUENCER::kCameraMotionDescriptors.size())
                return false;
            for (float value : {e.start, e.duration, e.fadeIn, e.fadeOut, e.frequency, e.maximumSpeed})
                if (!std::isfinite(value) || value < 0)
                    return false;
            if (e.maximumSpeed <= 0)
                return false;
            for (const auto* parameter : {&e.amount, &e.weight}) {
                if (!std::isfinite(parameter->value))
                    return false;
                float previous = -1;
                for (const auto& key : parameter->keys) {
                    if (!std::isfinite(key.time) || key.time < 0 || key.time <= previous || !std::isfinite(key.value) ||
                        !std::isfinite(key.inSlope) || !std::isfinite(key.outSlope))
                        return false;
                    previous = key.time;
                }
            }
        }
        return true;
    };
    std::unordered_set<std::string> ids;
    for (const auto& mode : profile.modes) {
        if (mode.id.empty() || mode.id.size() > 128 || !ids.insert(mode.id).second)
            return fail("Mode IDs must be nonempty and unique.");
        if (!validStack(mode.effects))
            return fail("Invalid camera effect stack.");
        if (mode.mount != CameraMount::Orbit && mode.mount != CameraMount::FirstPerson &&
            mode.mount != CameraMount::Fixed)
            return fail("Unknown camera mount.");
        if (!std::isfinite(mode.verticalFovDegrees) || mode.verticalFovDegrees < 5 || mode.verticalFovDegrees > 170)
            return fail("Vertical FOV must be between 5 and 170 degrees.");
        if (!std::isfinite(mode.nearClip) || !std::isfinite(mode.farClip) || mode.nearClip < 0.001f ||
            mode.farClip <= mode.nearClip)
            return fail("The camera clipping range is invalid.");
        for (float value : {mode.blendSeconds})
            if (!std::isfinite(value) || value < 0)
                return fail("Camera timing and effect scales must be finite and nonnegative.");
        for (float value : {mode.shoulderOffset})
            if (!std::isfinite(value))
                return fail("Camera offsets must be finite.");
    }
    ids.clear();
    for (const auto& effect : profile.effects) {
        if (effect.id.empty() || effect.id.size() > 128 || !ids.insert(effect.id).second)
            return fail("Effect IDs must be nonempty and unique.");
        if (!validStack(effect.effects))
            return fail("Invalid camera effect clip.");
        if (!std::isfinite(effect.duration) || effect.duration <= 0)
            return fail("Effect duration must be positive and finite.");
    }
    for (const auto& mode : profile.modes)
        for (const auto& base : mode.activationFromModes)
            if (!profile.FindMode(base))
                return fail("Activation base mode does not exist.");
    if (!profile.FindMode(profile.defaultMode))
        return fail("Default mode does not exist.");
    error.clear();
    return true;
}

nlohmann::json WriteGameplayCameraProfile(const GameplayCameraProfile& profile) {
    nlohmann::json root{{"assetType", "GameplayCamera"},
                        {"formatVersion", GameplayCameraProfile::kFormatVersion},
                        {"cinematicEnterSeconds", profile.cinematicEnterSeconds},
                        {"cinematicReturnSeconds", profile.cinematicReturnSeconds},
                        {"name", profile.name},
                        {"defaultMode", profile.defaultMode},
                        {"shoulderSwapAction", profile.shoulderSwapAction},
                        {"effects", nlohmann::json::array()},
                        {"modes", nlohmann::json::array()}};
    for (const auto& mode : profile.modes) {
        nlohmann::json follow;
        mode.follow.Serialize(follow);
        root["modes"].push_back({{"id", mode.id},
                                 {"name", mode.name},
                                 {"mount", mode.mount == CameraMount::FirstPerson ? "first_person"
                                           : mode.mount == CameraMount::Fixed     ? "fixed"
                                                                                  : "orbit"},
                                 {"follow", std::move(follow)},
                                 {"verticalFovDegrees", mode.verticalFovDegrees},
                                 {"nearClip", mode.nearClip},
                                 {"farClip", mode.farClip},
                                 {"blendSeconds", mode.blendSeconds},
                                 {"shoulderOffset", mode.shoulderOffset},
                                 {"effects", SEQUENCER::WriteCameraMotionEffects(mode.effects)},
                                 {"trackLookTarget", mode.trackLookTarget},
                                 {"keepUserOrbit", mode.keepUserOrbit},
                                 {"activationAction", mode.activationAction},
                                 {"activationFromModes", mode.activationFromModes},
                                 {"priority", mode.priority}});
    }
    for (const auto& effect : profile.effects)
        root["effects"].push_back({{"id", effect.id},
                                   {"name", effect.name},
                                   {"duration", effect.duration},
                                   {"effects", SEQUENCER::WriteCameraMotionEffects(effect.effects)}});
    return root;
}

bool ReadGameplayCameraProfile(const nlohmann::json& root, GameplayCameraProfile& result, std::string& error) {
    try {
        if (!root.is_object() || root.value("assetType", "") != "GameplayCamera" ||
            root.value("formatVersion", 0u) != GameplayCameraProfile::kFormatVersion) {
            error = "Unsupported gameplay camera asset format.";
            return false;
        }
        GameplayCameraProfile profile;
        profile.name = root.value("name", profile.name);
        profile.defaultMode = root.value("defaultMode", profile.defaultMode);
        profile.shoulderSwapAction = root.value("shoulderSwapAction", profile.shoulderSwapAction);
        profile.cinematicEnterSeconds = root.value("cinematicEnterSeconds", profile.cinematicEnterSeconds);
        profile.cinematicReturnSeconds = root.value("cinematicReturnSeconds", profile.cinematicReturnSeconds);
        if (!root.at("modes").is_array()) {
            error = "Camera modes must be an array.";
            return false;
        }
        profile.modes.clear();
        for (const auto& item : root.at("modes")) {
            GameplayCameraMode mode;
            mode.id = item.at("id").get<std::string>();
            mode.name = item.value("name", mode.id);
            const std::string mount = item.value("mount", "orbit");
            if (mount != "orbit" && mount != "first_person" && mount != "fixed") {
                error = "Unknown camera mount: " + mount;
                return false;
            }
            mode.mount = mount == "first_person" ? CameraMount::FirstPerson
                         : mount == "fixed"      ? CameraMount::Fixed
                                                 : CameraMount::Orbit;
            if (item.contains("follow"))
                mode.follow.Deserialize(item.at("follow"));
#define HIKARI_READ_MODE(member) mode.member = item.value(#member, mode.member)
            HIKARI_READ_MODE(verticalFovDegrees);
            HIKARI_READ_MODE(nearClip);
            HIKARI_READ_MODE(farClip);
            HIKARI_READ_MODE(blendSeconds);
            HIKARI_READ_MODE(shoulderOffset);
            HIKARI_READ_MODE(trackLookTarget);
            HIKARI_READ_MODE(keepUserOrbit);
            HIKARI_READ_MODE(activationAction);
            HIKARI_READ_MODE(activationFromModes);
            HIKARI_READ_MODE(priority);
#undef HIKARI_READ_MODE
            mode.effects = SEQUENCER::ReadCameraMotionEffects(item.value("effects", nlohmann::json::array()));
            profile.modes.push_back(std::move(mode));
        }
        for (const auto& item : root.value("effects", nlohmann::json::array())) {
            GameplayCameraEffect effect;
            effect.id = item.at("id").get<std::string>();
            effect.name = item.value("name", effect.id);
            effect.duration = item.value("duration", 0.6f);
            effect.effects = SEQUENCER::ReadCameraMotionEffects(item.value("effects", nlohmann::json::array()));
            profile.effects.push_back(std::move(effect));
        }
        if (!ValidateGameplayCameraProfile(profile, error))
            return false;
        result = std::move(profile);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

GameplayCameraProfile MakeGameplayCameraPreset(std::string_view preset) {
    GameplayCameraProfile profile;
    auto& base = profile.modes.front();
    base.follow.initialDistance = 4.5f;
    base.follow.minimumDistance = 0.1f;
    base.follow.maximumDistance = 10;
    base.follow.initialPitchDegrees = 15;
    base.follow.pivotOffset = {0, 1.6f, 0};
    if (preset == "first_person") {
        profile.name = "First Person";
        base.name = "First Person";
        base.mount = CameraMount::FirstPerson;
        base.follow.initialPitchDegrees = 0;
        base.verticalFovDegrees = 75;
    } else if (preset == "top_down" || preset == "side_scroller" || preset == "fixed") {
        profile.name = preset == "top_down" ? "Top Down" : preset == "side_scroller" ? "Side Scroller" : "Fixed Camera";
        base.name = profile.name;
        base.follow.orbitInputEnabled = false;
        base.keepUserOrbit = false;
        base.follow.initialDistance = preset == "top_down" ? 12.0f : 8.0f;
        base.follow.maximumDistance = 30;
        base.follow.initialPitchDegrees = preset == "top_down" ? 65.0f : 0.0f;
        base.follow.initialYawDegrees = preset == "side_scroller" ? 90.0f : 0.0f;
        if (preset == "fixed")
            base.mount = CameraMount::Fixed;
    } else {
        profile.name = "Third Person";
        base.name = "Explore";
        auto aim = base;
        aim.id = "aim";
        aim.name = "Aim";
        aim.priority = 20;
        aim.activationFromModes = {base.id};
        aim.activationAction = "Gameplay.Aim";
        aim.follow.initialDistance = 2.2f;
        aim.shoulderOffset = 0.55f;
        aim.verticalFovDegrees = 45;
        auto lock = base;
        lock.id = "lock_on";
        lock.name = "Lock On";
        lock.priority = 30;
        lock.trackLookTarget = true;
        lock.activationFromModes = {base.id};
        lock.activationAction = "Gameplay.LockOn";
        auto sprint = base;
        sprint.id = "sprint";
        sprint.name = "Sprint";
        sprint.priority = 10;
        sprint.activationFromModes = {base.id};
        sprint.activationAction = "Gameplay.Sprint";
        SEQUENCER::CameraMotionEffect speed;
        speed.kind = SEQUENCER::CameraMotionKind::SpeedFov;
        speed.amount.value = 12;
        sprint.effects.push_back(speed);
        auto fps = base;
        fps.id = "first_person";
        fps.name = "First Person";
        fps.mount = CameraMount::FirstPerson;
        fps.verticalFovDegrees = 75;
        profile.modes.insert(profile.modes.end(), {aim, lock, sprint, fps});
    }
    GameplayCameraEffect impact;
    SEQUENCER::CameraMotionEffect shake;
    shake.kind = SEQUENCER::CameraMotionKind::Shake;
    shake.amount.value = .08f;
    shake.frequency = 12;
    shake.duration = .6f;
    shake.fadeIn = .025f;
    shake.fadeOut = .5f;
    impact.effects.push_back(shake);
    profile.effects.push_back(impact);
    GameplayCameraEffect recoil;
    recoil.id = "recoil";
    recoil.name = "Recoil";
    recoil.duration = .3f;
    SEQUENCER::CameraMotionEffect tilt;
    tilt.kind = SEQUENCER::CameraMotionKind::Tilt;
    tilt.amount.keys = {{0, 0, 0, 0}, {.035f, -3, 0, 0}, {.3f, 0, 0, 0}};
    recoil.effects.push_back(tilt);
    profile.effects.push_back(recoil);
    GameplayCameraEffect pulse;
    pulse.id = "focus_pulse";
    pulse.name = "Focus Pulse";
    pulse.duration = .8f;
    SEQUENCER::CameraMotionEffect lens;
    lens.kind = SEQUENCER::CameraMotionKind::Fov;
    lens.amount.keys = {{0, 0, 0, 0}, {.15f, -8, 0, 0}, {.8f, 0, 0, 0}};
    pulse.effects.push_back(lens);
    profile.effects.push_back(pulse);
    return profile;
}
} // namespace HIKARI::CAMERA
