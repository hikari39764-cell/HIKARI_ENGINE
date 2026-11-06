#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"

#include <string>
#include <string_view>

#include <json.hpp>

#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

// 場面が持つ開発コマンド。相机・文書・場面の入れ替えは場面が持つので、実行時コマンド表へここから登録する。
// どれも Play 中だけ働き、場面の文書は変えない。自由視点は Play の終わりと World の作り直しで解ける。
namespace HIKARI {
namespace {

using Json = nlohmann::json;

// 開発コマンドが出す要求の送り手（"HIKDEBUG"）。実行時コマンドの time と同じ値。
constexpr uint64_t kDebugCommandSource = 0x48494b4445425547ull;
// 玩家の移動はゲームの要求より優先する。
constexpr int kTeleportPriority = 1 << 20;

RUNTIME::DebugReply Ok(const Json& output) { return {true, output.dump(2)}; }
RUNTIME::DebugReply Error(std::string reason) { return {false, Json{{"error", std::move(reason)}}.dump(2)}; }
RUNTIME::DebugReply NeedsPlay(std::string_view command) {
    return Error(std::string(command) + " works only while Play is running. Start Play first.");
}

Json ToJson(const MATH::Vec3& value) { return Json::array({value.x, value.y, value.z}); }

} // namespace

void DocumentSceneBase::RegisterSceneDebugCommands(INPUT::InputService& input) {
    RUNTIME::RuntimeDebugService& commands = state_->runtime.debugService;
    commands.RegisterCommand(
        "freecam", "freecam [on|off]",
        "Fly a free camera: WASD move, QE down/up, mouse looks, wheel sets speed, Shift is faster (Play only).",
        [this, &input](World&, std::string_view argument) {
            DocumentSceneCameraRuntimeState& camera = state_->camera;
            if (!state_->runtime.playActive)
                return NeedsPlay("freecam");
            if (!argument.empty() && argument != "on" && argument != "off")
                return Error("freecam expects on, off or nothing (toggle).");
            const bool enable = argument.empty() ? !camera.freeCameraActive : argument == "on";
            if (enable && !camera.freeCameraActive) {
                // 今描いている相机の位置と向きから飛び始める。速度は同じ Play の間は前回のまま。
                camera.freeCamera.ResetFromCamera(camera.editorCamera);
                camera.freeCamera.SetEnabled(true);
            }
            camera.freeCameraActive = enable;
            input.SetDeveloperInputCaptured(INPUT::DeveloperInputOwner::FreeCamera, enable);
            if (!enable)
                return Ok({{"freeCamera", false}, {"note", "The game camera is back."}});
            return Ok({{"freeCamera", true},
                       {"position", ToJson(camera.freeCamera.GetPosition())},
                       {"speed", camera.freeCamera.Settings().moveSpeed},
                       {"note", "Game input is paused while flying. teleport moves the player here; freecam "
                                "returns to the game camera."}});
        });
    commands.RegisterCommand(
        "teleport", "teleport", "Move the player to the current camera position (Play only).",
        [this](World& world, std::string_view argument) {
            if (!state_->runtime.playActive)
                return NeedsPlay("teleport");
            if (!argument.empty())
                return Error("teleport takes no argument; it uses the current camera position.");
            const SceneObjectId playerId = FindScenePlayer(state_->identity.document);
            GameObject* player = playerId.value != 0u ? world.FindObject(playerId) : nullptr;
            if (player == nullptr)
                return Error("No player: no object has CharacterInputComponent or CharacterLocomotionComponent.");
            const MATH::Vec3 target = state_->camera.editorCamera.GetPosition();
            // 動きを物理が持つ玩家は、次の固定歩長で物理に移させる（速度も消す）。
            auto* motion = world.Services().Find<PHYSICS::KinematicMotionService>();
            const PHYSICS::KinematicMotionState* kinematic =
                motion != nullptr ? motion->FindState(player->GetRuntimeHandle()) : nullptr;
            std::string via = "kinematic";
            if (kinematic != nullptr) {
                PHYSICS::KinematicTeleportRequest request{};
                request.pose.position = target;
                request.pose.rotation = kinematic->pose.rotation;
                motion->RequestTeleport(player->GetRuntimeHandle(), kDebugCommandSource, kTeleportPriority, request);
            } else if (player->GetParent() == nullptr && player->SetLocalPosition(target)) {
                via = "transform";
            } else {
                return Error("The player is parented to another object and has no physics motion to teleport.");
            }
            return Ok(
                {{"object", playerId.value}, {"name", player->GetName()}, {"position", ToJson(target)}, {"via", via}});
        });
    commands.RegisterCommand(
        "reload", "reload", "Reload the current scene from its saved asset, without a transition (Play only).",
        [this](World&, std::string_view argument) {
            if (!state_->runtime.playActive)
                return NeedsPlay("reload");
            if (!argument.empty())
                return Error("reload takes no argument.");
            SceneTransitionBus* bus = RuntimeSceneContext::GetTransitionBus();
            const AssetGuid& scene = state_->identity.currentSceneAssetGuid;
            if (bus == nullptr || !scene.IsValid())
                return Error("Scene transitions are unavailable for this scene.");
            SceneTransitionRequest request{};
            request.targetSceneAssetGuid = scene.value;
            request.useTransition = false;
            if (!bus->RequestTransition(request))
                return Error("The scene transition was rejected (another transition may be running).");
            return Ok({{"reloading", scene.value},
                       {"note", "Unsaved edits made before Play are not part of the "
                                "saved asset and are not reloaded."}});
        });
}

void DocumentSceneBase::ApplyDebugFreeCamera(float unscaledDt, const INPUT::InputService& input, bool editorViewport) {
    DocumentSceneCameraRuntimeState& camera = state_->camera;
    if (!camera.freeCameraActive)
        return;
    // コンソールに打っている間は飛ばない（同じキーが入力欄に入る）。
    DebugCameraControls controls{};
    if (!input.IsDeveloperInputCapturedBy(INPUT::DeveloperInputOwner::Console)) {
        // 窓がマウスを持っている（相対入力）ならマウスだけで向きを変え、そうでなければ右クリック中だけ回す。
        controls = DebugCameraController3D::ReadDeviceControls(
            input.GetSnapshot(), input.GetMouseCaptureMode() == INPUT::MouseCaptureMode::Relative);
    }
    // 投影はゲームの相机のまま、位置と向きだけ自由視点にする。
    camera.freeCamera.Fly(unscaledDt, camera.resolvedFrame.camera, controls,
                          editorViewport ? CameraControlInputContext::EditorViewport
                                         : CameraControlInputContext::RuntimeWindow);
}

} // namespace HIKARI
