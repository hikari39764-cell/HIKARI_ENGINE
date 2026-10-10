#pragma once

#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Camera/HIKARI_CameraFollowSettings.h"
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"
#include <string>
#include <vector>

namespace HIKARI::CAMERA {
// 機位の型。Orbit は追う物の周りを回る、FirstPerson は目の位置、Fixed は機位の物体の位置から追う物を見る。
// Rail は railStart〜railEnd の線上で追う物に最も近い点から見る、Arena は場の中心から追う物を通る向きの先に立つ。
enum class CameraMount { Orbit, FirstPerson, Fixed, Rail, Arena };

inline constexpr TEXT::EnumName<CameraMount> kCameraMountNames[] = {
    {CameraMount::Orbit, "orbit"}, {CameraMount::FirstPerson, "first_person"},
    {CameraMount::Fixed, "fixed"}, {CameraMount::Rail, "rail"},
    {CameraMount::Arena, "arena"},
};

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
    // Rail の線（ワールド座標）。
    MATH::Vec3 railStart{-6.0f, 3.0f, -4.0f};
    MATH::Vec3 railEnd{6.0f, 3.0f, -4.0f};
    // Arena の場の中心（ワールド座標）。距離と仰角は follow の initialDistance と initialPitchDegrees。
    MATH::Vec3 arenaCenter{};
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
    const GameplayCameraMode* FindMode(std::string_view id) const noexcept;
};

bool ReadGameplayCameraProfile(const nlohmann::json& json, GameplayCameraProfile& result, std::string& error);
nlohmann::json WriteGameplayCameraProfile(const GameplayCameraProfile& profile);
bool ValidateGameplayCameraProfile(const GameplayCameraProfile& profile, std::string& error);
GameplayCameraProfile MakeGameplayCameraPreset(std::string_view preset);
} // namespace HIKARI::CAMERA
