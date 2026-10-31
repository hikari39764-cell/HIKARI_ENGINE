#pragma once
#include "Core/HIKARI_AssetGuid.h"
#include "Scene/Camera/HIKARI_CameraRigTypes.h"
#include "Scene/Camera/HIKARI_GameplayCameraProfile.h"
#include <memory>

namespace HIKARI {
class World;
class GameObject;
class CameraFollowComponent;
namespace INPUT {
class InputSnapshot;
}
namespace CAMERA {
struct GameplayCameraBinding {
    AssetGuid profile;
    std::string initialMode;
    SceneObjectId lookTarget;
    // モード ID ごとの差分。未指定項目はアセットから継承する。
    nlohmann::json overrides = nlohmann::json::object();
};
struct GameplayCameraEvaluation {
    struct EffectPlayback {
        uint64_t token = 0;
        GameplayCameraEffect effect;
        float elapsed = 0, strength = 1;
    };
    std::vector<EffectPlayback> effects;
    std::shared_ptr<const GameplayCameraProfile> source, previewSource;
    GameplayCameraMode mode;
    // ランタイムの基底モード。演出フローが切り替えるが、アセットには保存しない。
    std::string baseMode;
    std::string requestedMode, activeMode, reason, error;
    CameraRigPose previousPose, transitionPose, outputPose;
    float elapsed = 0, transitionElapsed = 0, shoulderSign = 1;
    bool hasPose = false, changed = false;
    float previewSpeed = -1;
    float userYaw = 0, userPitch = 0;
    bool hasUserOrbit = false;
    void Reset() { *this = {}; }
};

// 一回の呼出しで有限長の効果を開始する。カメラ所有状態とともに破棄される。
uint64_t PlayGameplayCameraEffect(GameObject& camera, CameraFollowComponent& component, std::string_view id,
                                  float strength, std::string& error);
bool StopGameplayCameraEffect(CameraFollowComponent& component, uint64_t token);

// 既存の追従・構図ソルバを設定し、最後にマウントと演出補助を合成する。
bool PrepareGameplayCamera(GameObject& camera, CameraFollowComponent& component, const INPUT::InputSnapshot* input,
                           float seconds);
void ApplyGameplayCameraPose(const GameObject& camera, CameraFollowComponent& component, const MATH::Vec3& pivot,
                             float targetSpeed, float seconds, CameraRigPose& pose);
} // namespace CAMERA
} // namespace HIKARI
