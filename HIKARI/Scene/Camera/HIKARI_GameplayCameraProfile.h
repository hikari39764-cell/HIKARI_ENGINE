#pragma once

#include "Scene/Camera/HIKARI_CameraFollowSettings.h"
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"
#include <string>
#include <vector>

namespace HIKARI::CAMERA {
enum class CameraMount { Orbit, FirstPerson, Fixed };

struct GameplayCameraMode {
    std::string id = "explore";
    std::string name = "Explore";
    CameraMount mount = CameraMount::Orbit;
    CameraFollowSettings follow;
    float verticalFovDegrees = 60;
    float nearClip = 0.05f;
    float farClip = 2000;
    float blendSeconds = 0.3f;
    float shoulderOffset = 0;
    std::vector<SEQUENCER::CameraMotionEffect> effects;
    bool trackLookTarget = false;
    bool keepUserOrbit = true;
    // 空の場合は手動・ゲームコードから選択する。入力条件は押下中だけ有効。
    std::string activationAction;
    std::vector<std::string> activationFromModes;
    int priority = 0;
};

struct GameplayCameraEffect {
    std::string id = "impact", name = "Impact";
    float duration = 0.6f;
    std::vector<SEQUENCER::CameraMotionEffect> effects;
};

struct GameplayCameraProfile {
    static constexpr uint32_t kFormatVersion = 2;
    std::string name = "Gameplay Camera";
    std::string defaultMode = "explore";
    float cinematicEnterSeconds = 0.35f, cinematicReturnSeconds = 0.45f;
    std::string shoulderSwapAction = "Gameplay.CameraShoulder";
    std::vector<GameplayCameraMode> modes{GameplayCameraMode{}};
    std::vector<GameplayCameraEffect> effects;
    const GameplayCameraEffect* FindEffect(std::string_view id) const noexcept;
    const GameplayCameraMode *FindMode(std::string_view id) const noexcept;
};

bool ReadGameplayCameraProfile(const nlohmann::json &json, GameplayCameraProfile &result, std::string &error);
nlohmann::json WriteGameplayCameraProfile(const GameplayCameraProfile &profile);
bool ValidateGameplayCameraProfile(const GameplayCameraProfile &profile, std::string &error);
GameplayCameraProfile MakeGameplayCameraPreset(std::string_view preset);
} // namespace HIKARI::CAMERA
