#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneDocument.h"
#include <array>
#include "HIKARI/Project/SequenceView.h"
#include "HIKARI/Project/SceneCaptureView.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include <thread>
#include "Scene/Camera/HIKARI_CameraCollision.h"
#include "HIKARI/Project/ActionViews.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Sequencer/Serialization/HIKARI_ShotPresentationJson.h"
#include <limits>
#include <fstream>
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "HIKARI/Project/CameraSpace.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Camera/HIKARI_CameraFlowSystem.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include "HIKARI/Project/FrameContext.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include <filesystem>
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Editor/Documents/HIKARI_GameplayCameraEditorDocument.h"
#include "Physics/Backends/Jolt/HIKARI_JoltPhysicsBackend.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_CameraFollowSystem.h"
#include "Scene/HIKARI_World.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWalk.h"
#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace HIKARI;
using namespace HIKARI::CAMERA;
using HIKARI::SCENE::SERIALIZATION::WriteSceneObjectReference;
bool RunKinematicMotionContracts();
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));                        \
    } while (false)
static bool Near(float a, float b, float epsilon = 0.002f) { return std::isfinite(a) && std::abs(a - b) < epsilon; }
static void FollowStateReset() {
    CameraFollowComponent component;
    component.Deserialize({{"initialYawDegrees", 25.f}, {"initialPitchDegrees", 20.f}, {"initialDistance", 4.f}});
    component.InitializeRuntimeState({1, 2, 3}, {4, 5, 6});
    CHECK(component.Runtime().initialized && Near(component.Runtime().eye.x, 1));
    CHECK(Near(component.Runtime().targetPivot.y, 5));
    component.SetRuntimeOrbit(std::numeric_limits<float>::quiet_NaN(), 999, 999, -1, -1);
    CHECK(Near(component.Runtime().yawDegrees, component.Settings().initialYawDegrees));
    CHECK(Near(component.Runtime().pitchDegrees, component.Settings().maximumPitchDegrees));
    CHECK(component.Runtime().distance > 0 && Near(component.Runtime().lookIdleSeconds, 0));
    component.SetRuntimeScreenCompositionState(true, -1, .2f, .3f, .7f, .8f, true, true, 1, 2);
    component.ResetRuntimeState();
    CHECK(!component.Runtime().initialized && !component.Runtime().screenCompositionActive);
    CHECK(Near(component.Runtime().screenCompositionSide, 1) &&
          Near(component.Runtime().screenCompositionPrimaryX, .5f));
    CHECK(!component.Runtime().framingInitialized && Near(component.Runtime().framingTargetWeight, .45f));
    component.InitializeRuntimeState({0, 2, 4}, {0, 1, 0});
    CHECK(component.Runtime().initialized && Near(component.Runtime().eye.z, 4));
    CHECK(Near(component.Runtime().screenCompositionCollisionBlendVelocity, 0));
    World detachedWorld;
    Camera3D camera;
    GameplayCameraSafetyResult safety;
    CHECK(!ResolveGameplayCameraSafety(detachedWorld, {}, camera, &safety, &component));
    CHECK(!safety.evaluated);
}
static void Documents() {
    std::string error;
    for (const auto* preset : {"third_person", "shoulder", "first_person", "lock_on", "top_down", "side_scroller",
                               "fixed", "rail", "arena"}) {
        const auto profile = MakeGameplayCameraPreset(preset);
        GameplayCameraProfile copy;
        CHECK(ReadGameplayCameraProfile(WriteGameplayCameraProfile(profile), copy, error));
        CHECK(WriteGameplayCameraProfile(copy) == WriteGameplayCameraProfile(profile));
    }
    auto json = WriteGameplayCameraProfile(MakeGameplayCameraPreset("third_person"));
    GameplayCameraProfile invalid;
    json["modes"][1]["id"] = "explore";
    CHECK(!ReadGameplayCameraProfile(json, invalid, error));
    json["modes"] = nlohmann::json::object();
    CHECK(!ReadGameplayCameraProfile(json, invalid, error));
    EDITOR::GameplayCameraEditorDocument doc;
    CHECK(doc.New("third_person", error));
    CHECK(doc.IsDirty());
    auto original = doc.Profile();
    auto edit = original;
    edit.name = "A";
    CHECK(doc.Apply(edit, error, 42));
    edit.name = "B";
    CHECK(doc.Apply(edit, error, 42));
    CHECK(doc.Undo());
    CHECK(doc.Profile().name == original.name);
    CHECK(!doc.CanUndo());
    CHECK(doc.Redo());
    CHECK(doc.Profile().name == "B");
    CHECK(!doc.New("first_person", error));
    CHECK(doc.Profile().name == "B");
    CHECK(doc.New("first_person", error, true));
    CHECK(!doc.CanUndo());
    CameraFollowComponent follow;
    follow.Deserialize({{"initialDistance", 6.0f}, {"targetObjectId", WriteSceneObjectReference(2)}});
    nlohmann::json before, after;
    follow.Serialize(before);
    auto settings = MakeGameplayCameraPreset("first_person").modes[0].follow;
    settings.initialDistance = 1;
    follow.SetEvaluatedSettings(settings);
    follow.Serialize(after);
    CHECK(before == after);
    CHECK(Near(follow.GetInitialDistance(), 1));
    follow.ClearEvaluatedSettings();
    CHECK(Near(follow.GetInitialDistance(), 6));
}
static void DocumentSourceIdentity(const std::filesystem::path& root) {
    const auto first = root / "first";
    const auto second = root / "second";
    const auto source = std::filesystem::path("Assets/Cameras/Identity.hcamera");
    std::string error;
    auto profile = MakeGameplayCameraPreset("third_person");
    profile.name = "First";
    CHECK(SaveGameplayCameraAsset(first / source, profile, error));
    AssetDatabase database;
    CHECK(database.Initialize(first));
    CHECK(database.ScanAssets(true));
    const auto* record = database.FindByPath(source);
    CHECK(record);
    const auto guid = record->guid;
    const auto meta = record->metaPath;
    EDITOR::GameplayCameraEditorDocument document;
    CHECK(document.Open(database, guid, error));
    CHECK(document.Profile().name == "First");
    profile.name = "External edit";
    CHECK(SaveGameplayCameraAsset(first / source, profile, error));
    CHECK(document.Open(database, guid, error));
    CHECK(document.Profile().name == "External edit");
    auto edit = document.Profile();
    edit.name = "Local edit";
    CHECK(document.Apply(edit, error));
    profile.name = "Newer external edit";
    CHECK(SaveGameplayCameraAsset(first / source, profile, error));
    CHECK(!document.Save(database, error));
    CHECK(document.Profile().name == "Local edit");
    CHECK(document.Open(database, guid, error, true));
    CHECK(document.Profile().name == profile.name);
    edit = document.Profile();
    edit.modes.front().verticalFovDegrees = std::numeric_limits<float>::quiet_NaN();
    CHECK(!document.Apply(edit, error));
    CHECK(!document.IsDirty());

    profile.name = "Same GUID in another project";
    CHECK(SaveGameplayCameraAsset(second / source, profile, error));
    const auto copiedMeta = second / std::filesystem::relative(meta, first);
    std::filesystem::create_directories(copiedMeta.parent_path());
    std::filesystem::copy_file(meta, copiedMeta, std::filesystem::copy_options::overwrite_existing);
    AssetDatabase other;
    CHECK(other.Initialize(second));
    CHECK(other.ScanAssets(true));
    CHECK(other.FindByGuid(guid));
    CHECK(document.Open(other, guid, error));
    CHECK(document.Profile().name == profile.name);
    CHECK(!document.Save(database, error));

    // シーン importer は欠落・不正ソースを成功としてキャッシュしない。
    const auto scenePath = first / "Assets/Scenes/Invalid.hscene";
    std::filesystem::create_directories(scenePath.parent_path());
    {
        std::ofstream scene(scenePath);
        scene << R"({"version":4294967295})";
    }
    CHECK(database.ScanAssets(true));
    const auto sceneGuid = database.FindByPath(scenePath)->guid;
    CHECK(!database.ImportAsset(sceneGuid));
    {
        std::ofstream scene(scenePath);
        scene << R"({"version":)" << kCurrentSceneDocumentVersion << R"(,"objects":[]})";
    }
    CHECK(database.ImportAsset(sceneGuid));
}

// 镜头の姿勢键：键の時刻で一致し、途中の键は止まらずに通り（前後の速さがつながる）、最初と最後の键で止まる。
// 向きも同じ。軌道は道のりで割り当て、瞄准の重み 1 は相手を正面に捉える。保存の形は読んで書き直しても変わらない。
static void ShotPoseKeys() {
    using namespace SEQUENCER;
    ShotCamera shot;
    shot.useScenePose = false;
    const auto yaw = [](float degrees) {
        const float radians = degrees * 3.14159265f / 180.0f;
        return MATH::QuatFromForwardUp({std::sin(radians), 0.0f, std::cos(radians)}, {0.0f, 1.0f, 0.0f});
    };
    SetShotPose(shot, {0.0f, {0, 1, 0}, yaw(0), 50}, true);
    SetShotPose(shot, {1.0f, {2, 1, 0}, yaw(45), 40}, true);
    SetShotPose(shot, {3.0f, {2, 1, 4}, yaw(90), 30}, true);
    CHECK(shot.keys.size() == 3);
    const auto position = [&](float t) { return EvaluateShotPose(shot, t).position; };
    const auto forward = [&](float t) { return ShotPoseForward(EvaluateShotPose(shot, t)); };
    CHECK(Near(position(1.0f).x, 2) && Near(position(3.0f).z, 4) && Near(EvaluateShotPose(shot, 1.0f).fov, 40));
    CHECK(Near(forward(3.0f).x, 1) && Near(forward(0.0f).z, 1));
    // 途中の键の前後で速さがつながる（間隔が 1 秒と 2 秒で不揃いでも）。
    constexpr float h = 0.001f;
    const MATH::Vec3 before = (position(1.0f) - position(1.0f - h)) * (1.0f / h);
    const MATH::Vec3 after = (position(1.0f + h) - position(1.0f)) * (1.0f / h);
    CHECK(MATH::Length(before - after) < 0.05f && MATH::Length(after) > 0.5f);
    const MATH::Vec3 turnBefore = (forward(1.0f) - forward(1.0f - h)) * (1.0f / h);
    const MATH::Vec3 turnAfter = (forward(1.0f + h) - forward(1.0f)) * (1.0f / h);
    CHECK(MATH::Length(turnBefore - turnAfter) < 0.05f && MATH::Length(turnAfter) > 0.1f);
    // 最初と最後の键では止まる。
    CHECK(MATH::Length(position(h) - position(0.0f)) < 0.01f);
    CHECK(MATH::Length(position(3.0f) - position(3.0f - h)) < 0.01f);
    // 一定の速さを選ぶと键の間は直線。
    for (auto& key : shot.keys)
        key.interpolation = SEQUENCER::ShotInterpolation::Linear;
    CHECK(Near(position(0.5f).x, 1) && Near(position(0.5f).z, 0));
    for (auto& key : shot.keys)
        key.interpolation = SEQUENCER::ShotInterpolation::Spline;
    // 键を消すと残りの键でつなぎ、键は時刻を動かせる。
    CHECK(RemoveShotPoseKey(shot, 1.0f) && shot.keys.size() == 2);
    CHECK(MoveShotPoseKey(shot, 3.0f, 2.0f) && Near(shot.keys.back().time, 2));
    // 軌道：道のりの半分は、点の間隔が不揃いでも距離の中点。
    ShotRail rail;
    rail.points = {{0, 0, 0}, {1, 0, 0}, {10, 0, 0}};
    CHECK(Near(EvaluateShotRail(rail, 0.0f).x, 0) && Near(EvaluateShotRail(rail, 1.0f).x, 10));
    CHECK(Near(EvaluateShotRail(rail, 0.5f).x, 5, 0.2f));
    // 瞄准の重み 1 は相手を正面に見る（姿勢键の向きに依らない）。
    ShotCamera aimed;
    aimed.useScenePose = false;
    aimed.base.position = {0, 0, 0};
    aimed.base.rotation = yaw(-90);
    const MATH::Vec3 subject{0, 0, 5};
    const Camera3D camera = EvaluateShotCamera(aimed, 0.0f, 1.777f, nullptr, &subject);
    CHECK(Near(camera.GetTarget().z, 5) && Near(camera.GetTarget().x, 0));
    // 保存の形の往復。
    shot.rail = rail;
    SetScalarCurveKey(shot.railT, 0.0f, 0.0f);
    SetScalarCurveKey(shot.railT, 2.0f, 1.0f);
    const auto encoded = WriteShotCamera(shot);
    CHECK(WriteShotCamera(ReadShotCamera(encoded)) == encoded);
    CHECK(Near(EvaluateShotPose(ReadShotCamera(encoded), 2.0f).position.x, 10));
}

static void EffectStacks() {
    using namespace SEQUENCER;
    CameraMotionEffect dolly;
    dolly.kind = CameraMotionKind::Dolly;
    dolly.amount.value = 2;
    dolly.start = .2f;
    dolly.duration = 1;
    dolly.fadeIn = .2f;
    dolly.fadeOut = .2f;
    CameraMotionEffect zoom;
    zoom.id = 2;
    zoom.kind = CameraMotionKind::Fov;
    zoom.amount.value = 10;
    zoom.amount.keys = {{0, 0, 0, 0}, {1, 10, 0, 0}};
    std::vector<CameraMotionEffect> stack{dolly, zoom};
    const auto encoded = WriteCameraMotionEffects(stack);
    CHECK(WriteCameraMotionEffects(ReadCameraMotionEffects(encoded)) == encoded);
    const auto sample = [&](float time, float speed = 0, float strength = 1) {
        ShotCamera shot;
        shot.useScenePose = false;
        shot.base.position = {0, 0, -5};
        shot.base.fov = 60;
        shot.pivotDistance = 5;
        shot.motion = stack;
        auto cinematic = EvaluateShotCamera(shot, time, 1.777f);
        MATH::Vec3 eye{0, 0, -5}, target{0, 0, 0}, up{0, 1, 0};
        float fov = 60;
        ApplyCameraMotionEffects(stack, time, eye, target, up, fov, speed, strength);
        if (speed == 0 && strength == 1) {
            CHECK(Near(eye.z, cinematic.GetPosition().z));
            CHECK(Near(fov, cinematic.GetFovYRad() * 180 / 3.14159265f));
            CHECK(MATH::Length(up - cinematic.GetUp()) < .002f);
        }
        return eye;
    };
    CHECK(Near(sample(.1f).z, -5));
    CHECK(Near(sample(.3f).z, -4));
    CHECK(Near(sample(.6f).z, -3));
    CHECK(Near(sample(1.1f).z, -4));
    CHECK(Near(sample(1.2f).z, -5));
    CHECK(Near(sample(.6f, 0, 0).z, -5));
    CHECK(Near(sample(.6f).z, sample(.6f).z)); // scrubbing is stateless
    for (const auto& descriptor : kCameraMotionDescriptors) {
        stack = {CameraMotionEffect{}};
        stack[0].kind = descriptor.kind;
        stack[0].amount.value = descriptor.defaultAmount;
        sample(.35f);
    }
    stack[0].kind = CameraMotionKind::SpeedDolly;
    stack[0].amount.value = -2;
    stack[0].maximumSpeed = 4;
    CHECK(Near(sample(.3f, 0).z, -5));
    CHECK(Near(sample(.3f, 2).z, -6));
    CHECK(Near(sample(.3f, 8).z, -7));
    std::string error;
    // 旧版の asset は変換せずに拒否する。
    auto outdated = WriteGameplayCameraProfile(MakeGameplayCameraPreset("first_person"));
    outdated["formatVersion"] = 1;
    GameplayCameraProfile rejected;
    CHECK(!ReadGameplayCameraProfile(outdated, rejected, error));
    World world;
    CameraRigService rigs;
    world.Services().Register(rigs);
    auto* camera = world.CreateObject("Effects");
    CHECK(camera->SetDocumentId({1}));
    CHECK(camera->AddComponent<CameraComponent>());
    auto* target = world.CreateObject("Target");
    CHECK(target->SetDocumentId({2}));
    auto* follow = camera->AddComponent<CameraFollowComponent>();
    follow->Deserialize({{"targetObjectId", WriteSceneObjectReference(2)}});
    auto profile = MakeGameplayCameraPreset("third_person");
    profile.modes[0].blendSeconds = 0;
    auto& clip = profile.effects.front();
    clip.duration = .2f;
    clip.effects = {CameraMotionEffect{}};
    clip.effects[0].kind = CameraMotionKind::Fov;
    clip.effects[0].amount.value = 10;
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(profile);
    CameraFollowSystem evaluator;
    CHECK(evaluator.EvaluatePreview(world, *camera, *follow, 0));
    const auto base = follow->GameplayState().outputPose;
    CHECK(!PlayGameplayCameraEffect(*camera, *follow, "missing", 1, error));
    CHECK(!PlayGameplayCameraEffect(*camera, *follow, "impact", std::numeric_limits<float>::quiet_NaN(), error));
    const auto sdk = GameProjectSdkAccess::MakeCameraView(world);
    const auto handle = camera->GetRuntimeHandle().ToValue();
    CameraDirector director;
    world.Services().Register(director);
    CHECK(!sdk.SetGameplayCamera(0));
    CHECK(!sdk.SetGameplayCamera(target->GetRuntimeHandle().ToValue()));
    CHECK(sdk.SetGameplayCamera(handle));
    CHECK(director.GetBaseCamera() == SceneObjectId{1});

    CHECK(!sdk.PlayGameplayEffect(target->GetRuntimeHandle().ToValue(), "impact"));
    CHECK(!sdk.PlayGameplayEffect(0, "impact"));
    const auto token = sdk.PlayGameplayEffect(handle, "impact", .5f);
    CHECK(token);
    CHECK(evaluator.EvaluatePreview(world, *camera, *follow, 0));
    CHECK(Near(follow->GameplayState().outputPose.verticalFovRadians, base.verticalFovRadians + 5 * 3.14159265f / 180));
    CHECK(Near(follow->GameplayState().previousPose.verticalFovRadians, base.verticalFovRadians));
    CHECK(sdk.StopGameplayEffect(handle, token));
    CHECK(!sdk.StopGameplayEffect(handle, token));
    CHECK(evaluator.EvaluatePreview(world, *camera, *follow, 0));
    CHECK(Near(follow->GameplayState().outputPose.verticalFovRadians, base.verticalFovRadians));
    CHECK(PlayGameplayCameraEffect(*camera, *follow, "impact", 1, error));
    CHECK(PlayGameplayCameraEffect(*camera, *follow, "impact", 1, error));
    CHECK(evaluator.EvaluatePreview(world, *camera, *follow, 0));
    CHECK(
        Near(follow->GameplayState().outputPose.verticalFovRadians, base.verticalFovRadians + 20 * 3.14159265f / 180));
    for (int i = 0; i < 5; ++i)
        CHECK(evaluator.EvaluatePreview(world, *camera, *follow, .1f));
    CHECK(follow->GameplayState().effects.empty());
    CHECK(Near(follow->GameplayState().outputPose.verticalFovRadians, base.verticalFovRadians));
    CHECK(PlayGameplayCameraEffect(*camera, *follow, "impact", 1, error));
    follow->GameplayState().Reset();
    CHECK(follow->GameplayState().effects.empty());
}

static void DepthOfFieldCameraChannels() {
    CAMERA::DepthOfFieldSettings authored{};
    authored.enabled = false;
    authored.focusDistance = 10.0f;
    authored.nearBlurRadius1080pPixels = 8.0f;
    authored.farBlurRadius1080pPixels = 4.0f;
    CAMERA::DepthOfFieldSettings enabled = authored;
    enabled.enabled = true;
    enabled.focusDistance = 20.0f;

    Camera3D from;
    Camera3D to;
    from.SetDepthOfFieldSettings(authored);
    to.SetDepthOfFieldSettings(enabled);
    const std::array<float, 4> blendAmounts{0.0f, 0.25f, 0.5f, 1.0f};
    for (const float amount : blendAmounts) {
        const CAMERA::DepthOfFieldSettings result =
            CameraDirector::BlendViews(from, to, amount).GetDepthOfFieldSettings();
        CHECK(result.enabled == (amount > 0.0f));
        CHECK(Near(result.nearBlurRadius1080pPixels, amount == 0.0f ? 8.0f : 8.0f * amount));
        CHECK(Near(result.farBlurRadius1080pPixels, amount == 0.0f ? 4.0f : 4.0f * amount));
    }

    World world;
    CameraRigService rigs;
    CameraDirector director;
    world.Services().Register(rigs);
    auto* cameraObject = world.CreateObject("DoF camera");
    CHECK(cameraObject->SetDocumentId({42}));
    auto* cameraComponent = cameraObject->AddComponent<CameraComponent>();
    CHECK(cameraComponent != nullptr);
    cameraComponent->Deserialize({{"depthOfField",
                                   {{"enabled", authored.enabled},
                                    {"focusDistance", authored.focusDistance},
                                    {"focusRange", authored.focusRange},
                                    {"nearTransition", authored.nearTransition},
                                    {"farTransition", authored.farTransition},
                                    {"nearBlurRadius1080pPixels", authored.nearBlurRadius1080pPixels},
                                    {"farBlurRadius1080pPixels", authored.farBlurRadius1080pPixels}}}});
    director.SetBaseCamera({42});
    Camera3D fallback;
    const float authoredFov = cameraComponent->GetFovYRad();

    CAMERA::CameraDepthOfFieldSubmission fullSettings{};
    fullSettings.cameraObjectId = {42};
    fullSettings.sourceId = 10u;
    fullSettings.priority = 5;
    fullSettings.weight = 0.5f;
    fullSettings.settings = enabled;
    rigs.BeginFrame(1u);
    CHECK(rigs.SubmitDepthOfField(fullSettings));
    CAMERA::CameraDepthOfFieldSubmission invalid = fullSettings;
    invalid.settings.focusDistance = std::numeric_limits<float>::quiet_NaN();
    CHECK(!rigs.SubmitDepthOfField(invalid));
    CAMERA::CameraDepthOfFieldSubmission zeroWeight = fullSettings;
    zeroWeight.sourceId = 11u;
    zeroWeight.priority = 100;
    zeroWeight.weight = 0.0f;
    zeroWeight.settings.focusDistance = 2.0f;
    CHECK(rigs.SubmitDepthOfField(zeroWeight));
    CAMERA::CameraDepthOfFieldSubmission winner{};
    CHECK(rigs.TryResolveDepthOfField({42}, winner));
    CHECK(winner.sourceId == 10u && Near(winner.settings.focusDistance, 20.0f));
    // 運動ブラーも同じ受付と調停。範囲外は拒否し、重みで作者値（無効は 0°）からシャッター角とブラー長を寄せる。
    CAMERA::CameraMotionBlurSubmission blur{};
    blur.cameraObjectId = {42};
    blur.sourceId = 12u;
    blur.weight = 0.5f;
    blur.settings = {true, 360.0f, 64.0f};
    CAMERA::CameraMotionBlurSubmission outOfRange = blur;
    outOfRange.settings.shutterAngleDegrees = 400.0f;
    CHECK(!rigs.SubmitMotionBlur(outOfRange));
    CHECK(rigs.SubmitMotionBlur(blur));
    const Camera3D& weighted = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(weighted.GetMotionBlurSettings().enabled &&
          Near(weighted.GetMotionBlurSettings().shutterAngleDegrees, 180.0f) &&
          Near(weighted.GetMotionBlurSettings().maxBlur1080pPixels, 48.0f));
    CHECK(weighted.GetDepthOfFieldSettings().enabled);
    CHECK(Near(weighted.GetDepthOfFieldSettings().nearBlurRadius1080pPixels, 4.0f));
    CHECK(Near(weighted.GetDepthOfFieldSettings().focusDistance, 15.0f));
    CHECK(Near(weighted.GetFovYRad(), authoredFov));

    rigs.BeginFrame(2u);
    fullSettings.weight = 1.0f;
    fullSettings.settings.focusDistance = 12.0f;
    CHECK(rigs.SubmitDepthOfField(fullSettings));
    CAMERA::CameraFocusSubmission sameSourceFocus{};
    sameSourceFocus.cameraObjectId = {42};
    sameSourceFocus.sourceId = fullSettings.sourceId;
    sameSourceFocus.priority = fullSettings.priority;
    sameSourceFocus.mode = CAMERA::CameraFocusMode::Distance;
    sameSourceFocus.distance = 6.0f;
    CHECK(rigs.SubmitFocus(sameSourceFocus));
    const Camera3D& sameSource = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(Near(sameSource.GetDepthOfFieldSettings().focusDistance, 6.0f));
    CHECK(Near(sameSource.GetFovYRad(), authoredFov));

    rigs.BeginFrame(3u);
    CAMERA::CameraFocusSubmission pointFocus{};
    pointFocus.cameraObjectId = {42};
    pointFocus.sourceId = 20u;
    pointFocus.mode = CAMERA::CameraFocusMode::WorldPoint;
    pointFocus.worldPoint = {3.0f, 2.0f, 7.0f};
    CHECK(rigs.SubmitFocus(pointFocus));
    const Camera3D& worldPoint = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(Near(worldPoint.GetDepthOfFieldSettings().focusDistance, 7.0f));

    rigs.BeginFrame(4u);
    CAMERA::CameraFocusSubmission planeFocus{};
    planeFocus.cameraObjectId = {42};
    planeFocus.sourceId = 20u;
    planeFocus.mode = CAMERA::CameraFocusMode::WorldPlane;
    planeFocus.planePoint = {4.0f, 0.0f, 9.0f};
    planeFocus.planeNormal = {0.0f, 0.0f, 1.0f};
    CHECK(rigs.SubmitFocus(planeFocus));
    const Camera3D& worldPlane = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(Near(worldPlane.GetDepthOfFieldSettings().focusDistance, 9.0f));

    rigs.BeginFrame(5u);
    fullSettings.priority = 20;
    fullSettings.settings.focusDistance = 13.0f;
    CHECK(rigs.SubmitDepthOfField(fullSettings));
    pointFocus.priority = 10;
    pointFocus.worldPoint = {0.0f, 0.0f, 6.0f};
    CHECK(rigs.SubmitFocus(pointFocus));
    const Camera3D& higherPrioritySettings = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(Near(higherPrioritySettings.GetDepthOfFieldSettings().focusDistance, 13.0f));
    CHECK(Near(higherPrioritySettings.GetFovYRad(), authoredFov));

    rigs.BeginFrame(6u);
    planeFocus.sourceId = 21u;
    planeFocus.priority = 30;
    planeFocus.planePoint = {8.0f, 0.0f, 0.0f};
    planeFocus.planeNormal = {1.0f, 0.0f, 0.0f};
    CHECK(rigs.SubmitDepthOfField(fullSettings));
    CHECK(rigs.SubmitFocus(planeFocus));
    const Camera3D& parallelPlane = director.Resolve(world, fallback, 16.0f / 9.0f, 0.0f).camera;
    CHECK(Near(parallelPlane.GetDepthOfFieldSettings().focusDistance, 13.0f));
}

static void CameraSnapshotProjection() {
    struct SnapshotWithCanary {
        GameProjectCameraViewSnapshot snapshot{};
        uint64_t canary = 0x8F2D4A6C13579BDFull;
    };
    struct ResultWithCanary {
        GameProjectWorldToScreenResult result{};
        uint64_t canary = 0xD0C0B0A090807060ull;
    };

    SnapshotWithCanary input{};
    GameProjectCameraViewSnapshot& snapshot = input.snapshot;
    snapshot.structSize = sizeof(input);
    snapshot.frameIndex = 17u;
    snapshot.worldIdentity = 9u;
    snapshot.primaryViewId = 1u;
    snapshot.source = GameProjectPrimaryViewSource::Game;
    snapshot.presentationTarget = GameProjectPresentationTarget::BackBuffer;
    snapshot.lens.stage = GameProjectCameraLensStage::Rendered;
    snapshot.lens.projection = GameProjectCameraProjectionKind::Perspective;
    snapshot.lens.verticalFovRadians = 1.57079632679f;
    snapshot.lens.nearClip = 0.1f;
    snapshot.lens.farClip = 100.0f;
    snapshot.pose.eye = {0.0f, 0.0f, 0.0f};
    snapshot.pose.target = {0.0f, 0.0f, 1.0f};
    snapshot.pose.up = {0.0f, 1.0f, 0.0f};
    snapshot.aspectRatio = 2.0f;
    snapshot.sceneRenderWidth = 800u;
    snapshot.sceneRenderHeight = 400u;
    snapshot.outputPhysicalWidth = 640u;
    snapshot.outputPhysicalHeight = 360u;
    snapshot.uiLogicalWidth = 320u;
    snapshot.uiLogicalHeight = 180u;
    snapshot.sceneContentPhysical = {100.0f, 50.0f, 500.0f, 250.0f};
    snapshot.sceneContentLogical = {50.0f, 25.0f, 250.0f, 125.0f};

    World world;
    const CameraView cameraView = GameProjectSdkAccess::MakeCameraView(world);
    SnapshotWithCanary unpublished{};
    unpublished.snapshot.frameIndex = 1234u;
    unpublished.snapshot.worldIdentity = 5678u;
    CHECK(!cameraView.TryGetViewSnapshot(unpublished.snapshot));
    CHECK(unpublished.snapshot.frameIndex == 1234u && unpublished.snapshot.worldIdentity == 5678u);
    CHECK(unpublished.canary == 0x8F2D4A6C13579BDFull);

    ResultWithCanary projected{};
    projected.result.structSize = sizeof(projected);
    CHECK(ProjectWorldToScreen(snapshot, {0.0f, 0.0f, 10.0f}, GameProjectScreenCoordinateSpace::PhysicalOutputPixels,
                               projected.result));
    CHECK(projected.result.frameIndex == snapshot.frameIndex);
    CHECK(Near(projected.result.x, 300.0f) && Near(projected.result.y, 150.0f));
    CHECK((projected.result.flags & GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::InFront)) != 0u);
    CHECK((projected.result.flags & GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::WithinDepthRange)) !=
          0u);
    CHECK((projected.result.flags & GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::InsideSceneContent)) !=
          0u);
    CHECK(projected.canary == 0xD0C0B0A090807060ull);

    // RH view 空間では +Z 前方・+Y 上方のとき、+X は画面左側へ投影される。
    CHECK(ProjectWorldToScreen(snapshot, {10.0f, 0.0f, 10.0f}, GameProjectScreenCoordinateSpace::UiLogicalPixels,
                               projected.result));
    CHECK(Near(projected.result.x, 100.0f) && Near(projected.result.y, 75.0f));
    CHECK(ProjectWorldToScreen(snapshot, {0.0f, 0.0f, -10.0f}, GameProjectScreenCoordinateSpace::PhysicalOutputPixels,
                               projected.result));
    CHECK((projected.result.flags & GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag::InFront)) == 0u);

    snapshot.lens.projection = GameProjectCameraProjectionKind::Orthographic;
    snapshot.lens.orthographicHeight = 20.0f;
    CHECK(ProjectWorldToScreen(snapshot, {10.0f, 0.0f, 10.0f}, GameProjectScreenCoordinateSpace::PhysicalOutputPixels,
                               projected.result));
    CHECK(Near(projected.result.x, 200.0f) && Near(projected.result.y, 150.0f));
    CHECK(projected.canary == 0xD0C0B0A090807060ull);
}

static void Runtime() {
    World world;
    CameraRigService rigs;
    world.Services().Register(rigs);
    auto* camera = world.CreateObject("Camera");
    CHECK(camera->SetDocumentId({1}));
    CHECK(camera->AddComponent<CameraComponent>());
    auto* target = world.CreateObject("Target");
    CHECK(target->SetDocumentId({2}));
    auto* follow = camera->AddComponent<CameraFollowComponent>();
    CHECK(follow);
    follow->Deserialize({{"targetObjectId", WriteSceneObjectReference(2)}});
    auto profile = MakeGameplayCameraPreset("third_person");
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(profile);
    CameraFollowSystem evaluator;
    const auto evaluate = [&] {
        const bool ok = evaluator.EvaluatePreview(world, *camera, *follow, 1.0f / 60);
        CameraRigSubmission output;
        output.cameraObjectId = {1};
        output.sourceId = 1;
        output.pose = follow->GameplayState().previousPose;
        if (ok)
            CHECK(rigs.SubmitPose(output));
        return ok;
    };
    for (int frame = 0; frame < 60; ++frame) {
        rigs.BeginFrame(frame);
        CHECK(evaluate());
    }
    CHECK(follow->GameplayState().activeMode == "explore");
    CameraRigSubmission pose;
    CHECK(rigs.TryResolvePose({1}, pose));
    CHECK(pose.pose.overrideClipPlanes);
    CHECK(Near(pose.pose.nearClip, 0.05f));
    CHECK(rigs.SubmitMode({{1}, 2, 20, "aim"}));
    CHECK(rigs.SubmitMode({{1}, 3, 5, "sprint"}));
    CHECK(rigs.ResolveMode({1})->mode == "aim");
    CHECK(evaluate());
    CHECK(follow->GameplayState().activeMode == "aim");
    rigs.BeginFrame(62);
    CHECK(!rigs.ResolveMode({1}));
    CHECK(evaluate());
    CHECK(follow->GameplayState().activeMode == "explore");
    auto expanded = profile;
    auto top = MakeGameplayCameraPreset("top_down").modes.front();
    top.id = "top_down";
    expanded.modes.push_back(top);
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(expanded);
    follow->SetRuntimeOrbit(25, 20, 4.5f, 4.5f, 0);
    follow->GameplayState().requestedMode = "top_down";
    rigs.BeginFrame(1000);
    CHECK(evaluate());
    CHECK(Near(follow->Runtime().pitchDegrees, 65));
    follow->GameplayState().requestedMode = "explore";
    rigs.BeginFrame(1001);
    CHECK(evaluate());
    CHECK(Near(follow->Runtime().yawDegrees, 25));
    CHECK(Near(follow->Runtime().pitchDegrees, 20));
    // 軌道は線のうち追う物（原点 + 高さ 1.6）に最も近い点、竞技场は中心から追う物を通る向きの先に立つ。
    auto rail = MakeGameplayCameraPreset("rail").modes.front();
    rail.id = "rail";
    rail.blendSeconds = 0;
    rail.railStart = {-4, 3, -5};
    rail.railEnd = {4, 3, -5};
    auto arena = MakeGameplayCameraPreset("arena").modes.front();
    arena.id = "arena";
    arena.blendSeconds = 0;
    arena.arenaCenter = {0, 0, 10};
    expanded.modes.push_back(rail);
    expanded.modes.push_back(arena);
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(expanded);
    follow->GameplayState().requestedMode = "rail";
    rigs.BeginFrame(1002);
    CHECK(evaluate());
    const auto railEye = follow->GameplayState().outputPose.eye;
    CHECK(Near(railEye.x, 0) && Near(railEye.y, 3) && Near(railEye.z, -5));
    follow->GameplayState().requestedMode = "arena";
    rigs.BeginFrame(1003);
    CHECK(evaluate());
    const auto arenaEye = follow->GameplayState().outputPose.eye;
    const float arenaPitch = arena.follow.initialPitchDegrees * 3.14159265f / 180;
    CHECK(Near(arenaEye.x, 0) && Near(arenaEye.z, -arena.follow.initialDistance * std::cos(arenaPitch)));
    CHECK(Near(arenaEye.y, 1.6f + arena.follow.initialDistance * std::sin(arenaPitch)));
    follow->GameplayState().requestedMode = "first_person";
    for (int frame = 63; frame < 123; ++frame) {
        rigs.BeginFrame(frame);
        CHECK(evaluate());
    }
    CHECK(rigs.TryResolvePose({1}, pose));
    CHECK(Near(pose.pose.eye.y, 1.6f));
    CHECK(Near(pose.pose.eye.z, 0));
    follow->GameplayState().requestedMode = "explore";
    CameraDirector director;
    director.SetBaseCamera({1});
    Camera3D fallback;
    director.Resolve(world, fallback, 1.777f, 1.0f / 60);
    CameraActivationRequest request;
    request.returnBlend = CameraBlendDesc{CameraBlendMode::EaseInOut, 0.5f};
    const auto token = director.PushOverride(request);
    CHECK(token.IsValid());
    Camera3D cut;
    cut.SetLookAt({10, 6, -10}, {0, 1, 0});
    CHECK(director.SetOverrideCamera(token, cut));
    director.Resolve(world, fallback, 1.777f, 1.0f / 60);
    CHECK(target->SetLocalPosition({5, 0, 0}));
    for (int frame = 123; frame < 183; ++frame) {
        rigs.BeginFrame(frame);
        CHECK(evaluate());
        director.Resolve(world, fallback, 1.777f, 1.0f / 60);
    }
    CHECK(director.ReleaseOverride(token));
    auto first = director.Resolve(world, fallback, 1.777f, 0).camera.GetPosition();
    CHECK(Near(first.x, 10));
    CHECK(director.GetStatus().blending);
    for (int i = 0; i < 60; ++i)
        director.Resolve(world, fallback, 1.777f, 1.0f / 60);
    CHECK(!director.GetStatus().blending);
    CHECK(rigs.TryResolvePose({1}, pose));
    CHECK(Near(director.GetResolvedFrame().camera.GetPosition().x, pose.pose.eye.x));
    CHECK(Near(follow->Runtime().targetPivot.x, 5));
    {
        SequenceAsset asset;
        auto& sequence = asset.sequence;
        sequence.durationSeconds = 4;
        SEQUENCER::CameraCutClip clip;
        clip.id = 1;
        clip.durationSeconds = 4;
        clip.camera.useScenePose = false;
        clip.camera.base.position.x = 20;
        sequence.cameraCutTrack.clips.push_back(clip);
        SEQUENCER::SequenceBindingContext bindings;
        SEQUENCER::CameraSequenceTrackDriver driver(director, world, fallback);
        SEQUENCER::SequencePlaybackInstanceView instance;
        instance.handle = {1, 1};
        instance.asset = &asset;
        instance.bindings = &bindings;
        auto entry = director.GetResolvedFrame().camera.GetPosition();
        driver.OnSequenceStarted(instance);
        SEQUENCER::SequenceEvaluationContext context;
        context.currentTimeSeconds = 0;
        driver.Evaluate(instance, context);
        CHECK(Near(director.Resolve(world, fallback, 1.777f, 0).camera.GetPosition().x, entry.x));
        context.currentTimeSeconds = 1;
        driver.Evaluate(instance, context);
        CHECK(Near(director.Resolve(world, fallback, 1.777f, 0).camera.GetPosition().x, 20));
        driver.OnSequenceStopped(instance.handle, SEQUENCER::SequenceStopReason::Stopped);
        director.Resolve(world, fallback, 1.777f, 0);
        CHECK(director.GetStatus().blending);
        for (int i = 0; i < 60; ++i)
            director.Resolve(world, fallback, 1.777f, 1.0f / 60);
        CHECK(!director.GetStatus().blending);
    }
    // Release must preserve the outgoing composition while its spring fades out.
    auto* secondary = world.CreateObject("Framing target");
    CHECK(secondary->SetLocalPosition({7, 0, 4}));
    CameraTargetFramingSubmission framing;
    framing.cameraObjectId = {1};
    framing.sourceId = 987;
    framing.targetObject = secondary->GetRuntimeHandle();
    framing.relationshipShot = true;
    framing.relationshipPreferredSide = 1.0f;
    framing.relationshipShoulderAngleDegrees = 20.0f;
    framing.relationshipRollRadians = 0.15f;
    for (int i = 0; i < 90; ++i) {
        rigs.BeginFrame(2000 + i);
        CHECK(rigs.SubmitTargetFraming(framing));
        CHECK(evaluate());
    }
    CHECK(follow->Runtime().framingWeight > 0.9f && follow->Runtime().relationshipWeight > 0.9f);
    const auto locked = follow->Runtime();
    framing.relationshipShot = false;
    framing.relationshipPreferredSide = -1.0f;
    framing.relationshipRollRadians = -0.3f;
    framing.distanceScale = 0.8f;
    rigs.BeginFrame(2100);
    CHECK(rigs.SubmitTargetFraming(framing));
    CHECK(evaluate());
    CHECK(Near(follow->Runtime().relationshipPreferredSide, locked.relationshipPreferredSide));
    CHECK(Near(follow->Runtime().relationshipRollRadians, locked.relationshipRollRadians));
    CHECK(Near(follow->Runtime().framingDistanceScale, 0.8f));
    CHECK(follow->Runtime().relationshipWeight < locked.relationshipWeight);
    for (int i = 0; i < 180; ++i) {
        rigs.BeginFrame(2200 + i);
        CHECK(evaluate());
    }
    CHECK(!follow->Runtime().framingHasSubmission);
    CHECK(follow->Runtime().framingWeight < 0.001f && follow->Runtime().relationshipWeight < 0.001f);
    CHECK(follow->Runtime().framingSourceId == framing.sourceId);
    CHECK(std::isfinite(follow->Runtime().eye.x) && std::isfinite(follow->Runtime().eye.y));
    follow->GameplayState().previewSource.reset();
    follow->GameplayBinding().profile = {"missing"};
    CHECK(!PrepareGameplayCamera(*camera, *follow, nullptr, 0));
    CHECK(!follow->GameplayState().source);
}
static void Physics() {
    using namespace PHYSICS;
    World world;
    PhysicsWorldService physics;
    CHECK(physics.InstallBackend(CreateJoltPhysicsBackend()));
    CHECK(physics.AttachWorld(world));
    auto* dynamic = world.CreateObject("Dynamic");
    auto* wall = world.CreateObject("Wall");
    PhysicsBodyCreateInfo body;
    body.body.object = dynamic->GetRuntimeHandle();
    body.body.motionType = PhysicsMotionType::Dynamic;
    body.initialPose.position = {0, 0, 2};
    body.shapes.push_back({});
    CHECK(physics.CreateBody(body).Succeeded());
    body.body.object = wall->GetRuntimeHandle();
    body.body.motionType = PhysicsMotionType::Static;
    body.initialPose.position.z = 2.6f;
    CHECK(physics.CreateBody(body).Succeeded());
    PhysicsShapeCastQuery query;
    query.shape.type = PhysicsShapeType::Sphere;
    query.shape.radius = 0.2f;
    query.direction = {0, 0, 1};
    query.maxDistance = 8;
    PhysicsHit hit;
    CHECK(physics.ShapeCast(query, hit));
    CHECK(hit.object == dynamic->GetRuntimeHandle());
    query.filter.staticOnly = true;
    CHECK(physics.ShapeCast(query, hit));
    CHECK(hit.object == wall->GetRuntimeHandle());
    CHECK(hit.distance < 2.1f);
    physics.DetachWorld();
}
class CameraInputBackend final : public HIKARI::INPUT::IInputBackend {
public:
    bool down = false;
    float lookX = 0, lookY = 0;
    void SetHostWindow(void*) override {}
    bool HasInputFocus() const noexcept override { return true; }
    void SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode) override {}
    void SetMouseCaptureRegion(const HIKARI::INPUT::MouseCaptureRegion&) override {}
    void ClearMouseCaptureRegion() override {}
    HIKARI::INPUT::MouseCaptureMode GetMouseCaptureMode() const noexcept override {
        return HIKARI::INPUT::MouseCaptureMode::Free;
    }
    void SetExternalMouseWheel(float) override {}
    void SetPointerCursorVisible(bool) override {}
    void Reset() override {}
    void Poll(HIKARI::INPUT::InputDeviceState& state) override {
        state = {};
        state.keyboard['A'] = down;
        state.mouseDeltaX = lookX;
        state.mouseDeltaY = lookY;
    }
    float ReadControl(const HIKARI::INPUT::InputDeviceState&, HIKARI::INPUT::InputBindingSource,
                      std::string_view control, uint32_t) const override {
        return control == "DeltaX" ? lookX : control == "DeltaY" ? lookY : control == "A" && down ? 1.f : 0.f;
    }
    bool IsAnyControlActive(const HIKARI::INPUT::InputDeviceState&, float) const override {
        return down || lookX != 0 || lookY != 0;
    }
    void EnumerateNewControls(const HIKARI::INPUT::InputDeviceState&, const HIKARI::INPUT::InputDeviceState&, float,
                              bool, std::vector<HIKARI::INPUT::InputControlActuation>& out) const override {
        out.clear();
    }
    bool SetGamepadVibration(uint32_t, float, float) override { return false; }
};
static void CameraInputDirections() {
    HIKARI::INPUT::InputService input;
    auto backend = std::make_unique<CameraInputBackend>();
    auto* fake = backend.get();
    input.SetBackend(std::move(backend));
    CHECK(input.InitializeTransient());
    auto& map = input.EditActionMap();
    map.actions = {{"Gameplay.Look", "Look", HIKARI::INPUT::InputActionValueType::Axis2D, true}};
    HIKARI::INPUT::InputContextDefinition context;
    context.contextId = "DirectionTest";
    context.bindings = {{"Gameplay.Look", HIKARI::INPUT::InputBindingSource::MouseDeltaX, "DeltaX", 1, 0},
                        {"Gameplay.Look", HIKARI::INPUT::InputBindingSource::MouseDeltaY, "DeltaY", 1, 1}};
    map.contexts = {context};
    input.Contexts().ResetToDefaults(map);
    for (const char* mode : {"explore", "first_person"})
        for (float yaw : {0.f, 90.f, 180.f, 270.f})
            for (float pitch : {-50.f, 0.f, 50.f})
                for (bool invert : {false, true}) {
                    World world;
                    CameraRigService rigs;
                    world.Services().Register(rigs);
                    auto* camera = world.CreateObject("Input Camera");
                    CHECK(camera->SetDocumentId({1}));
                    CHECK(camera->AddComponent<CameraComponent>());
                    auto* target = world.CreateObject("Input Target");
                    CHECK(target->SetDocumentId({2}));
                    auto* follow = camera->AddComponent<CameraFollowComponent>();
                    follow->Deserialize({{"targetObjectId", WriteSceneObjectReference(2)}});
                    auto profile = MakeGameplayCameraPreset("third_person");
                    for (auto& item : profile.modes) {
                        item.follow.initialYawDegrees = yaw;
                        item.follow.initialPitchDegrees = pitch;
                        item.follow.collisionEnabled = false;
                        item.follow.invertVerticalLook = invert;
                        item.blendSeconds = 0;
                        item.keepUserOrbit = false;
                    }
                    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(profile);
                    follow->GameplayState().baseMode = mode;
                    CameraFollowSystem evaluator;
                    auto evaluate = [&] {
                        input.Update(1.f / 60);
                        CHECK(evaluator.EvaluatePreview(world, *camera, *follow, 1.f / 60, &input.GetSnapshot()));
                        return follow->GameplayState().previousPose;
                    };
                    fake->lookX = fake->lookY = 0;
                    auto pose = evaluate();
                    auto forward = MATH::Normalize(pose.target - pose.eye);
                    const auto basis = MATH::BuildViewBasisRH(forward, pose.up);
                    fake->lookX = 12;
                    auto turned = evaluate();
                    CHECK(MATH::Dot(MATH::Normalize(turned.target - turned.eye), basis.right) > 0.001f);
                    fake->lookX = 0;
                    fake->lookY = 12;
                    const auto turnedBasis = MATH::BuildViewBasisRH(turned.target - turned.eye, turned.up);
                    auto down = evaluate();
                    const float verticalTurn = MATH::Dot(MATH::Normalize(down.target - down.eye), turnedBasis.up);
                    CHECK(invert ? verticalTurn > 0.001f : verticalTurn < -0.001f);

                    // 地面上の W は見上げ/見下ろしに関係なく視線の水平成分へ進む。
                    GameProjectCameraPose sdk;
                    sdk.eye = {pose.eye.x, pose.eye.y, pose.eye.z};
                    sdk.target = {pose.target.x, pose.target.y, pose.target.z};
                    sdk.up = {pose.up.x, pose.up.y, pose.up.z};
                    GameProjectFloat3 move;
                    CHECK(ResolveCameraPlanarMovement(sdk, 0, 1, {0, 1, 0}, move,
                                                      GameProjectCameraPlanarForward::ViewDirection));
                    CHECK(Near(move.y, 0));
                    CHECK(MATH::Dot(MATH::Vec3{move.x, move.y, move.z},
                                    MATH::Normalize(MATH::Vec3{forward.x, 0, forward.z})) > .99f);
                    CHECK(ResolveCameraPlanarMovement(sdk, 1, 0, {0, 1, 0}, move,
                                                      GameProjectCameraPlanarForward::ViewDirection));
                    Camera3D projected;
                    projected.SetLookAt(pose.eye, pose.target, pose.up);
                    // 世界座標の符号ではなく、描画に使うビュー行列の画面右を検証する。
                    const auto screen = projected.GetView().TransformPoint({move.x, move.y, move.z, 0});
                    CHECK(screen.x > .99f);
                    fake->lookY = 0;
                }
    // XY 面の 2D 操作は既存の画面上方向を維持する。
    for (float tilt : {-0.5f, 0.f, 0.5f}) {
        GameProjectCameraPose pose;
        pose.eye = {0, 0, -10};
        pose.target = {0, tilt, 0};
        pose.up = {0, 1, -tilt / 10};
        GameProjectFloat3 move;
        CHECK(ResolveCameraPlanarMovement(pose, 0, 1, {0, 0, 1}, move));
        CHECK(move.y > .99f && Near(move.z, 0));
    }
    EDITOR::EditorDirectorCameraController observer;
    for (float yaw : {0.f, 1.57f, 3.14f}) {
        observer.Reset({0, 0, 0}, yaw, .4f);
        const auto before = observer.GetCamera();
        const auto basis = MATH::BuildViewBasisRH(before.GetTarget() - before.GetPosition(), before.GetUp());
        EDITOR::EditorDirectorCameraInput controls;
        controls.lookActive = true;
        controls.mouseDeltaX = 12;
        controls.mouseDeltaY = 12;
        CHECK(observer.Update(controls, 1.f / 60, 1.777f));
        const auto after = observer.GetCamera();
        const auto forward = MATH::Normalize(after.GetTarget() - after.GetPosition());
        CHECK(MATH::Dot(forward, basis.right) > 0.001f && MATH::Dot(forward, basis.up) < -0.001f);
        observer.ResetFromCamera(before);
        controls = {};
        controls.moveRight = true;
        CHECK(observer.Update(controls, 1.f / 60, 1.777f));
        CHECK(MATH::Dot(observer.GetCamera().GetPosition() - before.GetPosition(), basis.right) > 0);
    }
}
static void ContextualCameraInput() {
    HIKARI::INPUT::InputService input;
    auto backend = std::make_unique<CameraInputBackend>();
    auto* fake = backend.get();
    input.SetBackend(std::move(backend));
    CHECK(input.InitializeTransient());
    auto& map = input.EditActionMap();
    map.actions = {{"Gameplay.Aim", "Aim", HIKARI::INPUT::InputActionValueType::Button, true}};
    HIKARI::INPUT::InputContextDefinition context;
    context.contextId = "Flow";
    context.bindings.push_back({"Gameplay.Aim", HIKARI::INPUT::InputBindingSource::Keyboard, "A"});
    map.contexts = {context};
    input.Contexts().ResetToDefaults(map);
    input.Update(1.f / 60);
    fake->down = true;
    input.Update(1.f / 60);
    CHECK(input.GetSnapshot().IsDown("Gameplay.Aim"));
    World world;
    auto* camera = world.CreateObject("Camera");
    CHECK(camera->SetDocumentId({1}));
    auto* follow = camera->AddComponent<CameraFollowComponent>();
    auto profile = MakeGameplayCameraPreset("third_person");
    auto fpsAim = *profile.FindMode("first_person");
    fpsAim.id = "fps_aim";
    fpsAim.activationAction = "Gameplay.Aim";
    fpsAim.activationFromModes = {"first_person"};
    fpsAim.priority = 20;
    profile.modes.push_back(fpsAim);
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(profile);
    CHECK(PrepareGameplayCamera(*camera, *follow, &input.GetSnapshot(), 1.f / 60));
    CHECK(follow->GameplayState().activeMode == "aim");
    follow->GameplayState().baseMode = "first_person";
    CHECK(PrepareGameplayCamera(*camera, *follow, &input.GetSnapshot(), 1.f / 60));
    CHECK(follow->GameplayState().activeMode == "fps_aim");
    fake->down = false;
    input.Update(1.f / 60);
    CHECK(PrepareGameplayCamera(*camera, *follow, &input.GetSnapshot(), 1.f / 60));
    CHECK(follow->GameplayState().activeMode == "first_person");
}
static void CameraFlows(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    SequenceAsset asset;
    asset.displayName = "Flow test";
    asset.sequence.durationSeconds = 1;
    SEQUENCER::CameraCutClip shot;
    shot.id = 1;
    shot.durationSeconds = 1;
    shot.camera.useScenePose = false;
    shot.camera.base.position = {8, 4, -8};
    asset.sequence.cameraCutTrack.clips.push_back(shot);
    std::string error;
    CHECK(SaveSequenceAsset(root / "Assets/Flow.hsequence", asset, &error));
    AssetDatabase db;
    CHECK(db.Initialize(root));
    CHECK(db.ScanAssets(true));
    auto sequences = db.CollectByType(AssetType::Sequence);
    CHECK(sequences.size() == 1);
    SequenceAssetStore store;
    store.SetAssetDatabase(&db);
    World world;
    CameraRigService rigs;
    CameraDirector director;
    SEQUENCER::SequencePlaybackService playback;
    bool playing = true;
    RuntimePlayStateService play{&playing};
    world.Services().Register(play);
    world.Services().Register(rigs);
    world.Services().Register(director);
    world.Services().Register(playback);
    playback.SetAssetStore(&store);
    Camera3D fallback;
    CHECK(playback.RegisterDriver(std::make_unique<SEQUENCER::CameraSequenceTrackDriver>(director, world, fallback)));
    auto* camera = world.CreateObject("Camera");
    CHECK(camera->SetDocumentId({1}));
    CHECK(camera->AddComponent<CameraComponent>());
    auto* follow = camera->AddComponent<CameraFollowComponent>();
    follow->Deserialize({{"targetObjectId", WriteSceneObjectReference(2)}});
    auto profile = MakeGameplayCameraPreset("third_person");
    follow->GameplayState().previewSource = std::make_shared<const GameplayCameraProfile>(profile);
    auto* subject = world.CreateObject("Player");
    CHECK(subject->SetDocumentId({2}));
    CHECK(subject->SetLocalPosition({0, 0, -4}));
    auto* volume = world.CreateObject("Door");
    CHECK(volume->SetDocumentId({3}));
    auto* flow = volume->AddComponent<CameraFlowComponent>();
    nlohmann::json properties = {{"cameraObjectId", WriteSceneObjectReference(1)},
                                 {"subjectObjectId", WriteSceneObjectReference(2)},
                                 {"halfExtents", {1, 2, 1}},
                                 {"sequenceAssetGuid", sequences[0]->guid.value},
                                 {"afterMode", "first_person"}};
    flow->Deserialize(properties);
    nlohmann::json authored;
    flow->Serialize(authored);
    CameraFlowSystem system;
    CameraFollowSystem evaluator;
    director.SetBaseCamera({1});
    FrameContext frame;
    frame.gameDt = 1.f / 60;
    unsigned tick = 0;
    auto step = [&] {
        rigs.BeginFrame(++tick);
        system.LateUpdate(world, frame);
        CHECK(evaluator.EvaluatePreview(world, *camera, *follow, frame.gameDt));
        CameraRigSubmission pose;
        pose.cameraObjectId = {1};
        pose.sourceId = 1;
        pose.pose = follow->GameplayState().previousPose;
        CHECK(rigs.SubmitPose(pose));
        playback.Update(frame.gameDt);
        director.Resolve(world, fallback, 16.f / 9, frame.gameDt);
    };
    step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Armed);
    CHECK(follow->GameplayState().activeMode == "explore");
    CHECK(subject->SetLocalPosition({0, 0, 0}));
    step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Playing);
    CHECK(flow->Runtime().runs == 1);
    for (int i = 0; i < 100; ++i)
        step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Completed);
    CHECK(follow->GameplayState().baseMode == "first_person");
    CHECK(follow->GameplayState().activeMode == "first_person");
    CHECK(playback.GetActiveInstanceCount() == 0);
    nlohmann::json after;
    flow->Serialize(after);
    CHECK(after == authored);

    // SDK Views retain active-world, owner-thread and playback handle boundaries.
    const auto sequenceView = GameProjectSdkAccess::MakeSequenceView(world);
    const auto captureView = GameProjectSdkAccess::MakeSceneCaptureView(world);
    RuntimeSceneContext::SetCurrentWorld(&world);
    CHECK(sequenceView.IsValid() && captureView.IsValid());
    const auto handle = sequenceView.Play(sequences[0]->guid.value, {});
    CHECK(handle.IsValid() && sequenceView.IsPlaying(handle));
    CHECK(sequenceView.Pause(handle) && sequenceView.IsPaused(handle));
    CHECK(sequenceView.SetPlaybackRate(handle, -2.0f));
    CHECK(!sequenceView.SetPlaybackRate(handle, 0.0f));
    CHECK(sequenceView.Seek(handle, 0.5f));
    CHECK(sequenceView.Resume(handle) && sequenceView.IsActive(handle));
    GameProjectSequencePlaybackSnapshot snapshot;
    CHECK(sequenceView.GetSnapshot(handle, snapshot));
    playing = false;
    CHECK(!sequenceView.Pause(handle));
    playing = true;
    std::thread worker([&] {
        CHECK(!sequenceView.IsValid() && !captureView.IsValid());
        CHECK(!sequenceView.Stop(handle));
    });
    worker.join();
    CHECK(sequenceView.Stop(handle) && !sequenceView.Stop({}));

    SCENE_CAPTURE::SceneCaptureService captures;
    world.Services().Register(captures);
    GameProjectSceneCaptureDesc desc;
    desc.cameraObject = {{world.GetRuntimeIdentity()}, camera->GetRuntimeHandle().ToValue()};
    const auto capture = captureView.Create(desc);
    CHECK(capture.IsValid());
    GameProjectSceneCaptureStyleDesc style;
    style.style = GameProjectSceneCaptureStyle::Sketch;
    style.pixelSize = 8.0f;
    CHECK(captureView.SetStyle(capture, style));
    CHECK(captureView.Update(capture, desc));
    CHECK(captures.Requests()[0].style.style == style.style && captures.Requests()[0].style.pixelSize == 8.0f);
    const auto secondCapture = captureView.Create(desc);
    CHECK(secondCapture.IsValid());
    CHECK(captures.Requests()[1].style.style == GameProjectSceneCaptureStyle::None);
    for (float invalid :
         {-1.0f, 33.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto rejected = style;
        rejected.pixelSize = invalid;
        CHECK(!captureView.SetStyle(capture, rejected));
        CHECK(captures.Requests()[0].style.pixelSize == 8.0f);
    }
    auto rejected = style;
    rejected.style = static_cast<GameProjectSceneCaptureStyle>(4u);
    CHECK(!captureView.SetStyle(capture, rejected));
    rejected = style;
    rejected.paperColor.x = 1.01f;
    CHECK(!captureView.SetStyle(capture, rejected));
    rejected = style;
    rejected.depthLineGap = 0.0f;
    CHECK(!captureView.SetStyle(capture, rejected));
    // 版 4 は 140 byte、版 2・3 は 80 byte だけを受け付ける。大きさの合わない版と未知の版は拒む。
    for (const uint32_t version : {1u, 3u, 5u}) {
        rejected = style;
        rejected.version = version;
        CHECK(!captureView.SetStyle(capture, rejected));
    }
    // 版 3（80 byte）は深度の欄を読まず、深度の効果は無効のまま。
    auto legacy = style;
    legacy.version = 3u;
    legacy.structSize = 80u;
    legacy.depthFocus = 5.0f;
    CHECK(captureView.SetStyle(capture, legacy));
    CHECK(captures.Requests()[0].style.depthFocus == 0.0f && captures.Requests()[0].style.version == 4u);
    CHECK(captureView.SetStyle(capture, style));
    std::thread captureWorker([&] { CHECK(!captureView.SetStyle(capture, style)); });
    captureWorker.join();
    CHECK(captureView.Destroy(secondCapture));
    CHECK(!captureView.SetStyle(secondCapture, style));
    auto* display = world.CreateObject("Display");
    CHECK(display->AddComponent<ModelComponent>());
    GameProjectSceneCaptureMaterialBindingDesc binding;
    binding.capture = capture;
    binding.modelObject = {{world.GetRuntimeIdentity()}, display->GetRuntimeHandle().ToValue()};
    binding.fallbackMaterialIndex = 0;
    CHECK(captureView.BindMaterial(binding));
    CHECK(captures.Bindings().size() == 1 && captures.Bindings()[0].sourceRect.width == 1.0f);
    binding.reserved[0] = 1;
    CHECK(!captureView.BindMaterial(binding));
    CHECK(captures.Bindings().size() == 1);
    GameProjectSceneCaptureMaterialRegionBindingDesc region;
    region.capture = capture;
    region.modelObject = binding.modelObject;
    region.fallbackMaterialIndex = 0;
    region.sourceRect = {0.25f, 0.0f, 0.5f, 1.0f};
    region.quarterTurns = 1;
    CHECK(captureView.BindMaterialRegion(region));
    CHECK(captures.Bindings()[0].quarterTurns == 1 && captures.Bindings()[0].sourceRect.width == 0.5f);
    region.quarterTurns = 4;
    CHECK(!captureView.BindMaterialRegion(region));
    CHECK(captureView.Destroy(capture) && !captureView.Destroy(capture));
    CHECK(!captureView.SetStyle(capture, style));
    RuntimeSceneContext::SetCurrentWorld(nullptr);
    CHECK(!sequenceView.IsValid() && !captureView.IsValid());
    CHECK(!sequenceView.Resume(handle) && !captureView.Create(desc).IsValid());
    world.Services().Remove<SCENE_CAPTURE::SceneCaptureService>();

    for (int i = 0; i < 10; ++i)
        step();
    CHECK(flow->Runtime().runs == 1);
    CHECK(subject->SetLocalPosition({0, 0, -4}));
    step();
    CHECK(subject->SetLocalPosition({0, 0, 0}));
    step();
    CHECK(flow->Runtime().runs == 1);
    // Explicit requests can replay a once-only region; interruption preserves the previous base mode.
    follow->GameplayState().baseMode = "explore";
    CHECK(flow->RequestTrigger());
    step();
    CHECK(flow->Runtime().runs == 2);
    CHECK(flow->RequestInterrupt());
    step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Interrupted);
    CHECK(follow->GameplayState().baseMode == "explore");
    CHECK(playback.GetActiveInstanceCount() == 0);
    properties["finishOnInterrupt"] = true;
    flow->Deserialize(properties);
    CHECK(flow->RequestTrigger());
    step();
    CHECK(flow->RequestInterrupt());
    step();
    CHECK(follow->GameplayState().baseMode == "first_person");
    for (int i = 0; i < 40; ++i)
        step();
    // Mode-only flow on exit, filtered by the current base mode. It does not start a sequence.
    properties["sequenceAssetGuid"] = "";
    properties["triggerOnExit"] = true;
    properties["repeatOnReentry"] = true;
    properties["afterMode"] = "explore";
    properties["requiredMode"] = "first_person";
    flow->Deserialize(properties);
    CHECK(subject->SetLocalPosition({0, 0, -4}));
    step();
    CHECK(follow->GameplayState().baseMode == "explore");
    CHECK(playback.GetActiveInstanceCount() == 0);
    for (int i = 0; i < 40; ++i)
        step();
    // A busy cinematic is never replaced by a region. A rejected entry can be retried explicitly.
    properties["sequenceAssetGuid"] = sequences[0]->guid.value;
    properties["triggerOnExit"] = false;
    properties["requiredMode"] = "";
    flow->Deserialize(properties);
    auto occupied = playback.PlayInline(asset.sequence, {}, {}, 0, "Cinematics");
    CHECK(occupied.IsValid());
    CHECK(flow->RequestTrigger());
    step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Invalid);
    CHECK(playback.IsActive(occupied));
    CHECK(playback.Stop(occupied));
    CHECK(flow->RequestTrigger());
    step();
    CHECK(flow->Runtime().stage == CameraFlowStage::Playing);
    // Owner teardown releases its playback; no driver override may survive it.
    system.OnWorldDetached(world);
    CHECK(playback.GetActiveInstanceCount() == 0);
    CHECK(!director.HasActiveOverride());
    // Rotated/scaled region shares the existing camera-volume geometric definition.
    CHECK(volume->SetLocalRotation(MATH::Quat::FromEulerXYZ(0, 1.5707963f, 0)));
    CHECK(volume->SetLocalScale({2, 1, 1}));
    CHECK(CAMERA::ContainsCameraSubject(*volume, {1, 1, .5f}, {0, 0, 1.5f}));
    CHECK(!CAMERA::ContainsCameraSubject(*volume, {1, 1, .5f}, {.8f, 0, 0}));
    SCENE::SERIALIZATION::RemapSceneObjectReferences(authored, {{1, 11}, {2, 22}});
    CameraFlowComponent remapped;
    remapped.Deserialize(authored);
    CHECK(remapped.Settings().cameraObjectId.value == 11);
    CHECK(remapped.Settings().subjectObjectId.value == 22);
}
class ContractScene final : public DocumentSceneBase {
public:
    ContractScene() : DocumentSceneBase("camera-contracts") {}
    const char* GetSceneName() const override { return "Camera contracts"; }
};
// 模拟走动：相机の向きに沿って歩き（右手系で右は前×上）、段差の無い所では足の高さを保ち、録った走位を入力に
// 関係なくなぞり、機位の切り替えは CameraDirector と同じ混ぜ方で、止めると替身を元の位置へ戻す。
static void SimulatedWalk() {
    World world;
    auto* subject = world.CreateObject("Walker");
    CHECK(subject->SetDocumentId({7}));
    Transform3D start = subject->GetTransform();
    start.position = {2.0f, 1.5f, 0.0f};
    CHECK(subject->SetLocalTransform(start));
    EDITOR::GameplayCameraWalk walk;
    walk.Start(world, {7});
    CHECK(walk.Active());
    Camera3D camera;
    camera.SetPerspective(1.0f, 16.0f / 9, 0.1f, 500.0f);
    const auto step = [&](EDITOR::GameplayCameraWalk::Input input, const MATH::Vec3& forward) {
        walk.Step(world, input, forward, 0.5f);
        const MATH::Vec3 position = WorldPosition(*subject);
        camera.SetLookAt(position + MATH::Vec3{0.0f, 2.0f, -4.0f}, position);
        walk.Observe(world, camera, {});
        return position;
    };
    walk.SetRecording(true);
    auto position = step({1.0f, 0.0f, false}, {0.0f, -0.5f, 1.0f});
    CHECK(Near(position.x, 2.0f) && Near(position.y, 1.5f) && Near(position.z, 1.5f));
    position = step({0.0f, 1.0f, true}, {0.0f, 0.0f, 1.0f});
    CHECK(Near(position.x, -1.0f) && Near(position.y, 1.5f) && Near(position.z, 1.5f));
    walk.SetRecording(false);

    Camera3D from = camera, to = camera;
    to.SetLookAt({10.0f, 4.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    walk.BeginBlend(from, 1.0f);
    const Camera3D half = walk.Resolve(to, 0.5f);
    CHECK(MATH::Length(half.GetPosition() - CameraDirector::BlendViews(from, to, 0.5f).GetPosition()) < 1e-4f);
    CHECK(MATH::Length(walk.Resolve(to, 0.6f).GetPosition() - to.GetPosition()) < 1e-4f);

    walk.SetReplaying(true);
    position = step({0.0f, -1.0f, false}, {1.0f, 0.0f, 0.0f});
    CHECK(Near(position.x, 2.0f) && Near(position.z, 1.5f));
    position = step({0.0f, -1.0f, false}, {1.0f, 0.0f, 0.0f});
    CHECK(Near(position.x, -1.0f) && Near(position.z, 1.5f));
    walk.Stop(world);
    CHECK(!walk.Active());
    position = WorldPosition(*subject);
    CHECK(Near(position.x, 2.0f) && Near(position.y, 1.5f) && Near(position.z, 0.0f));
}

static void WorkspaceLayout() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto controller = std::make_unique<EDITOR::GameplayCameraWorkspaceController>();
    auto scene = std::make_unique<ContractScene>();
    EDITOR::EditorWorkspaceHost host;
    EDITOR::EditorOperationRegistry operations;
    std::string error;
    CHECK(controller->Document().New("third_person", error));
    auto* subject = scene->GetWorld().CreateObject("UI Player");
    CHECK(subject->SetDocumentId({10}));
    auto* camera = scene->GetWorld().CreateObject("UI Camera");
    CHECK(camera->SetDocumentId({90}));
    CHECK(camera->AddComponent<CameraComponent>());
    auto* follow = camera->AddComponent<CameraFollowComponent>();
    follow->Deserialize({{"targetObjectId", WriteSceneObjectReference(10)}});
    auto* volume = scene->GetWorld().CreateObject("UI Flow");
    CHECK(volume->SetDocumentId({300}));
    auto* flow = volume->AddComponent<CameraFlowComponent>();
    flow->Deserialize({{"subjectObjectId", WriteSceneObjectReference(10)},
                       {"cameraObjectId", WriteSceneObjectReference(90)},
                       {"afterMode", "first_person"}});
    SceneObjectData object;
    object.id = {300};
    object.name = "UI Flow";
    SceneComponentData component;
    component.type = "CameraFlowComponent";
    flow->Serialize(component.properties);
    object.components.push_back(component);
    scene->GetSceneDocument().objects.push_back(object);

    for (const auto size : {ImVec2{1400, 900}, ImVec2{2048, 1221}, ImVec2{1280, 720}}) {
        io.DisplaySize = size;
        CHECK(controller->PreviewEffect(*scene, "impact", .25f, 1, false, error));
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            controller->DrawDockSpace(frame == 0);
            controller->Draw(*scene, host, operations, subject->GetDocumentId());
            ImGui::Render();
            CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
        }
        for (const char* id : {"###GameplayCamera/Library", "###GameplayCamera/Inspector", "###GameplayCamera/Observer",
                               "###GameplayCamera/Monitor", "###GameplayCamera/Playtest"}) {
            const auto* window = ImGui::FindWindowByName(id);
            CHECK(window && window->DockNode);
            CHECK(window->Size.x > 100 && window->Size.y > 45);
            CHECK(window->Pos.x >= -1 && window->Pos.y >= -1);
            CHECK(window->Pos.x + window->Size.x <= size.x + 1 && window->Pos.y + window->Size.y <= size.y + 1);
        }
        const auto* context = ImGui::GetCurrentContext();
        CHECK(context->DisabledStackSize == 0 && context->ColorStack.empty() && context->StyleVarStack.empty());
    }
    controller->ClosePreviewRequest(host);
    ImGui::DestroyContext();
}
static void CameraSourceContinuity() {
    // 機位→演出→機位は同じ画なら連続。位置・画角の跳躍では履歴を破棄する。
    World world;
    CameraDirector director;
    Camera3D base;
    constexpr float aspect = 16.0f / 9.0f;
    base.SetAspect(aspect);
    CHECK(director.Resolve(world, base, aspect, 0).cameraCut);
    auto target = director.GetResolvedFrame().camera;
    CameraActivationRequest request;
    request.cameraObjectId = {99};
    auto token = director.PushOverride(request);
    CHECK(director.SetOverrideCamera(token, target));
    auto frame = director.Resolve(world, base, aspect, 0);
    CHECK(!frame.cameraCut && frame.continuousCameraSource);
    CHECK(director.ReleaseOverride(token));
    frame = director.Resolve(world, base, aspect, 0);
    CHECK(!frame.cameraCut && frame.continuousCameraSource);
    target.SetLookAt({10, 6, -10}, {0, 1, 0});
    token = director.PushOverride(request);
    CHECK(director.SetOverrideCamera(token, target));
    frame = director.Resolve(world, base, aspect, 0);
    CHECK(frame.cameraCut && !frame.continuousCameraSource);
    CHECK(director.ReleaseOverride(token));
    director.Resolve(world, base, aspect, 0);
    // 演出の最初の評価が dt > 0 でも、元の機位から始まるなら履歴を引き継ぐ。
    request.continuityOrigin = base;
    token = director.PushOverride(request);
    CHECK(director.SetOverrideCamera(token, target));
    frame = director.Resolve(world, base, aspect, 0.01f);
    CHECK(!frame.cameraCut && frame.continuousCameraSource && Near(frame.camera.GetPosition().x, 10));
    CHECK(director.ReleaseOverride(token));
    director.Resolve(world, base, aspect, 0);
    request.continuityOrigin.reset();
    request.blend = {CameraBlendMode::EaseInOut, 1};
    token = director.PushOverride(request);
    CHECK(director.SetOverrideCamera(token, target));
    frame = director.Resolve(world, base, aspect, 0.01f);
    CHECK(!frame.cameraCut && frame.continuousCameraSource);
    CHECK(director.ReleaseOverride(token));
    director.Resolve(world, base, aspect, 0);
    request.blend = {};
    target = base;
    target.SetPerspective(0.7f, aspect, base.GetNearZ(), base.GetFarZ());
    token = director.PushOverride(request);
    CHECK(director.SetOverrideCamera(token, target));
    frame = director.Resolve(world, base, aspect, 0);
    CHECK(frame.cameraCut && frame.projectionChanged && !frame.continuousCameraSource);

    // 実際の driver と cursor でも、初回の正逆再生だけを連続とし、始めの seek はカットにする。
    using namespace SEQUENCER;
    SequenceAsset asset;
    asset.sequence.durationSeconds = 2;
    CameraCutClip clip;
    clip.id = 1;
    clip.durationSeconds = 2;
    clip.camera.useScenePose = false;
    ShotPose start, end;
    start.interpolation = ShotInterpolation::Linear;
    end = start;
    end.time = 1;
    end.position.x = 10;
    clip.camera.keys = {start, end};
    asset.sequence.cameraCutTrack.clips = {clip};
    SequenceBindingContext bindings;
    for (int scenario = 0; scenario < 3; ++scenario) {
        CameraDirector sequenceDirector;
        const bool reverse = scenario == 1;
        const float from = reverse ? 1.0f : 0.0f;
        Camera3D entry;
        CinematicCameraEvaluation evaluation;
        CHECK(EvaluateSequenceCameraView(asset.sequence, bindings, world, sequenceDirector, from, aspect, evaluation,
                                         entry));
        sequenceDirector.Resolve(world, entry, aspect, 0);
        CameraSequenceTrackDriver driver(sequenceDirector, world, entry);
        SequencePlaybackInstanceView instance;
        instance.handle = {1, 1};
        instance.asset = &asset;
        instance.bindings = &bindings;
        driver.OnSequenceStarted(instance);
        SequencePlaybackCursor cursor;
        CHECK(cursor.Bind(2, from));
        SequencePlaybackOptions options;
        options.playbackRate = reverse ? -1.0f : 1.0f;
        CHECK(cursor.Play(options));
        if (scenario == 2)
            CHECK(cursor.Seek(0.05f));
        const auto context = cursor.Tick(0.05f, SequenceEvaluationMode::Runtime);
        driver.Evaluate(instance, context);
        const auto result = sequenceDirector.Resolve(world, entry, aspect, 0.05f);
        CHECK(result.cameraCut == (scenario == 2));
        CHECK(result.continuousCameraSource == (scenario != 2));
        CHECK(Near(result.camera.GetPosition().x, context.currentTimeSeconds * 10));
        driver.Reset();
    }
    // 止めて毎コマ seek で進める（プロジェクトの自前の時計・時間轴の引き）。同じ镜头の中の小さな seek は再生と
    // 同じく続きの画にし、時間方向の蓄積を捨てない。大きな seek は切る。
    {
        CameraDirector sequenceDirector;
        Camera3D entry;
        CinematicCameraEvaluation evaluation;
        CHECK(EvaluateSequenceCameraView(asset.sequence, bindings, world, sequenceDirector, 0.0f, aspect, evaluation,
                                         entry));
        sequenceDirector.Resolve(world, entry, aspect, 0);
        CameraSequenceTrackDriver driver(sequenceDirector, world, entry);
        SequencePlaybackInstanceView instance;
        instance.handle = {1, 1};
        instance.asset = &asset;
        instance.bindings = &bindings;
        driver.OnSequenceStarted(instance);
        SequencePlaybackCursor cursor;
        CHECK(cursor.Bind(2, 0.0f));
        driver.Evaluate(instance, cursor.Tick(0.0f, SequenceEvaluationMode::Runtime));
        sequenceDirector.Resolve(world, entry, aspect, 0);
        for (int step = 1; step <= 10; ++step) {
            CHECK(cursor.Seek(static_cast<float>(step) / 60.0f));
            const auto context = cursor.Tick(0.0f, SequenceEvaluationMode::Runtime);
            CHECK(context.IsDiscontinuous());
            driver.Evaluate(instance, context);
            const auto result = sequenceDirector.Resolve(world, entry, aspect, 1.0f / 60.0f);
            CHECK(!result.cameraCut && Near(result.camera.GetPosition().x, context.currentTimeSeconds * 10));
        }
        CHECK(cursor.Seek(0.9f));
        driver.Evaluate(instance, cursor.Tick(0.0f, SequenceEvaluationMode::Runtime));
        CHECK(sequenceDirector.Resolve(world, entry, aspect, 1.0f / 60.0f).cameraCut);
        driver.Reset();
    }
}

static void ManualSequenceClock() {
    // 世界時計を止めても指定した演出だけ進み、キーを離した Pause と逆再生は同じ cursor を使う。
    World world;
    CameraDirector director;
    Camera3D fallback;
    SEQUENCER::SequencePlaybackService playback;
    CHECK(playback.RegisterDriver(std::make_unique<SEQUENCER::CameraSequenceTrackDriver>(director, world, fallback)));
    CinematicSequence sequence;
    sequence.durationSeconds = 1;
    SEQUENCER::CameraCutClip clip;
    clip.id = 1;
    clip.durationSeconds = 1;
    clip.camera.useScenePose = false;
    sequence.cameraCutTrack.clips = {clip};
    SEQUENCER::SequencePlaybackOptions options;
    options.holdAtEnd = true;
    const auto game = playback.PlayInline(sequence, {}, options, 0, "game");
    const auto manual = playback.PlayInline(sequence, {}, options, 0, "manual");
    CHECK(game.IsValid() && manual.IsValid());
    CHECK(playback.SetUseUnscaledTime(manual, true));
    CHECK(!playback.SetUseUnscaledTime({}, true));
    playback.Update(0, .1f);
    CHECK(Near(playback.GetTimeSeconds(game), 0) && Near(playback.GetTimeSeconds(manual), .1f));
    const auto overrides = director.GetStatus().overrideCount;
    CHECK(playback.Pause(manual));
    playback.Update(0, .2f);
    CHECK(Near(playback.GetTimeSeconds(manual), .1f));
    CHECK(playback.Resume(manual));
    playback.Update(0, .1f);
    CHECK(Near(playback.GetTimeSeconds(manual), .2f));
    CHECK(playback.SetPlaybackRate(manual, -1));
    playback.Update(0, .1f);
    CHECK(Near(playback.GetTimeSeconds(manual), .1f));
    CHECK(director.GetStatus().overrideCount == overrides);
    playback.Update(0, 0);
    CHECK(Near(playback.GetTimeSeconds(manual), .1f));
    playback.Update(0, .2f);
    CHECK(playback.IsPaused(manual) && Near(playback.GetTimeSeconds(manual), 0));
    CHECK(playback.SetUseUnscaledTime(manual, false) && playback.SetPlaybackRate(manual, 1) && playback.Resume(manual));
    playback.Update(0, .1f);
    CHECK(Near(playback.GetTimeSeconds(manual), 0));
    playback.Update(.1f, .1f);
    CHECK(Near(playback.GetTimeSeconds(manual), .1f));
    playback.Reset();
}

int main(int argc, char** argv) {
    try {
        ManualSequenceClock();
        CameraSourceContinuity();
        FollowStateReset();
        Documents();
        DocumentSourceIdentity(std::filesystem::absolute(argv[0]).parent_path() / "camera-document-fixture");
        EffectStacks();
        ShotPoseKeys();
        DepthOfFieldCameraChannels();
        CameraSnapshotProjection();
        Runtime();
        Physics();
        CHECK(RunKinematicMotionContracts());
        ContextualCameraInput();
        CameraInputDirections();
        CameraFlows(std::filesystem::absolute(argv[0]).parent_path() / "camera-flow-fixture");
        SimulatedWalk();
        WorkspaceLayout();
        if (argc == 2) {
            std::string error;
            for (const auto* preset : {"third_person", "first_person", "top_down", "side_scroller", "fixed"})
                CHECK(SaveGameplayCameraAsset(TEXT::PathFromUtf8(argv[1]) / (std::string(preset) + ".hcamera"),
                                              MakeGameplayCameraPreset(preset), error));
        }
        std::cout << "Gameplay camera profiles, optical blend/focus channels, snapshot projection, runtime modes and "
                     "Jolt filtering: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
