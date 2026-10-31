#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <numbers>
#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Scene/Sequencer/Drivers/HIKARI_SequenceTextPresenter.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <source_location>

namespace {
void Check(bool value, const std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("Camera editing contract failed: " + std::to_string(where.line()));
}
bool Close(const HIKARI::Camera3D& a, const HIKARI::Camera3D& b) {
    return HIKARI::MATH::Length(a.GetPosition() - b.GetPosition()) < 0.0001f &&
           HIKARI::MATH::Length(a.GetTarget() - b.GetTarget()) < 0.0001f &&
           std::abs(a.GetFovYRad() - b.GetFovYRad()) < 0.0001f;
}
// 任意の外部資料で既存制作物との同値性を検証する。資料は製品やリポジトリに含めない。
void CameraFixtureContracts() {
    using namespace HIKARI;
    using namespace HIKARI::SEQUENCER;
    char* filename = nullptr;
    size_t size = 0;
    if (_dupenv_s(&filename, &size, "HIKARI_CAMERA_FIXTURE") != 0 || !filename)
        return;
    const std::filesystem::path path(filename);
    std::free(filename);
    std::ifstream stream(path);
    nlohmann::json fixture;
    stream >> fixture;
    World world;
    CameraDirector director;
    SequenceBindingContext bindings;
    const auto vector = [](const auto& j) {
        return MATH::Vec3{j[0].template get<float>(), j[1].template get<float>(), j[2].template get<float>()};
    };
    auto* optical = world.CreateObject("Optics");
    Check(optical->SetDocumentId({3}));
    Check(optical->AddComponent<CameraComponent>() != nullptr);
    Check(bindings.BindSlot("Camera", {3}));
    for (const auto& station : fixture.at("stations")) {
        auto* object = world.CreateObject("Station");
        Check(object->SetDocumentId({station.at("objectId").get<uint64_t>()}));
        Check(object->SetLocalPosition(vector(station.at("position"))));
        Check(object->SetLocalRotation(
            MATH::Quat::FromEulerXYZ(station.at("euler")[0].get<float>() * std::numbers::pi_v<float> / 180.f,
                                     station.at("euler")[1].get<float>() * std::numbers::pi_v<float> / 180.f,
                                     station.at("euler")[2].get<float>() * std::numbers::pi_v<float> / 180.f)));
        auto* component = object->AddComponent<CameraComponent>();
        Check(component != nullptr);
        component->Deserialize({{"verticalFovDegrees", station.at("fov")},
                                {"nearClip", station.at("near")},
                                {"farClip", station.at("far")}});
    }
    size_t frames = 0;
    for (const auto& entry : fixture.at("sequences")) {
        SequenceAsset asset;
        std::string error;
        if (!LoadSequenceAsset(entry.at("path").get<std::string>(), {}, asset, &error))
            throw std::runtime_error(error);
        for (const auto& sample : entry.at("samples")) {
            Camera3D actual;
            CinematicCameraEvaluation evaluation;
            Check(EvaluateSequenceCameraView(asset.sequence, bindings, world, director, sample.at("time"), 16.f / 9,
                                             evaluation, actual));
            const auto& q = sample.at("rotation");
            MATH::Quat rotation{q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()};
            Check(MATH::Length(actual.GetPosition() - vector(sample.at("position"))) < .0002f);
            Check(MATH::Length(MATH::Normalize(actual.GetTarget() - actual.GetPosition()) -
                               MATH::RotateVector(rotation, {0, 0, 1})) < .0002f);
            Check(MATH::Length(actual.GetUp() - MATH::RotateVector(rotation, {0, 1, 0})) < .0002f);
            Check(std::abs(actual.GetFovYRad() * 180.f / std::numbers::pi_v<float> - sample.at("fov").get<float>()) <
                  .001f);
            ++frames;
        }
    }
    std::cout << "Camera baseline: " << fixture.at("sequences").size() << " sequences, " << frames
              << " frames matched.\n";
}

} // namespace
// 文字轨：保存と読み込みで値が保たれ、壊れた片段は読み込みを失敗させ、縦書きは右の列から並び一字ずつ現れる。
void SequenceTextTrackContracts() {
    using namespace HIKARI;
    using namespace HIKARI::SEQUENCER;
    CinematicSequence sequence;
    SequenceTextClip clip;
    clip.id = 7u;
    clip.text = "あいう\nえお";
    clip.tag = "題";
    clip.startSeconds = 2.0f;
    clip.durationSeconds = 4.0f;
    clip.style.value = "0123456789abcdef0123456789abcdef";
    clip.layout = SequenceTextLayout::Vertical;
    clip.alignX = SequenceTextAlign::Center;
    clip.alignY = SequenceTextAlign::Start;
    clip.x = 0.5f;
    clip.y = 0.25f;
    clip.columnSpacing = 1.6f;
    clip.tint = {0.5f, 0.25f, 0.125f, 0.75f};
    clip.reveal = SequenceTextReveal::Typewriter;
    clip.revealSeconds = 2.0f;
    clip.fadeInSeconds = 0.5f;
    clip.fadeOutSeconds = 1.0f;
    clip.order = 46;
    sequence.textTrack.clips.push_back(clip);
    nlohmann::json saved;
    SerializeCinematicSequenceJson(sequence, saved);
    CinematicSequence loaded;
    DeserializeCinematicSequenceJson(saved, loaded);
    Check(loaded.textTrack.clips.size() == 1u);
    const auto& back = loaded.textTrack.clips.front();
    Check(back.id == 7u && back.text == clip.text && back.tag == clip.tag && back.style == clip.style);
    Check(back.layout == SequenceTextLayout::Vertical && back.alignY == SequenceTextAlign::Start);
    Check(back.reveal == SequenceTextReveal::Typewriter && back.order == 46 && back.tint == clip.tint);
    Check(GetCinematicSequenceContentEnd(loaded) >= 6.0f);
    // 壊れた片段（未知の並べ方、長すぎる印）は直さずに読み込みを失敗させる。
    for (const auto& [key, value] : {std::pair<const char*, nlohmann::json>{"layout", "diagonal"},
                                     std::pair<const char*, nlohmann::json>{"tag", std::string(64, 'x')}}) {
        nlohmann::json broken = saved;
        bool edited = false;
        for (auto& track : broken.at("tracks"))
            if (track.at("type") == "text") {
                track.at("clips").at(0)[key] = value;
                edited = true;
            }
        Check(edited);
        bool rejected = false;
        try {
            CinematicSequence ignored;
            DeserializeCinematicSequenceJson(broken, ignored);
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected);
    }
    // 淡入・淡出と一字ずつ。
    Check(std::abs(GetSequenceTextOpacity(clip, 0.25f) - 0.5f) < 1.0e-4f);
    Check(GetSequenceTextOpacity(clip, 2.0f) == 1.0f && GetSequenceTextOpacity(clip, 4.5f) == 0.0f);
    Check(std::abs(GetSequenceTextReveal(clip, 1.0f) - 0.5f) < 1.0e-4f);
    // 縦書き：二列、右の列が先。時刻 3 秒（現れ 50%、5 字のうち 2.5 字）では右の列だけが途中まで出る。
    std::vector<SequenceTextDraw> draws;
    BuildSequenceTextDraws(clip, 3.0f, 40.0f, 1.08f, 100u, draws);
    Check(draws.size() == 1u && draws[0].text == "あいう");
    Check(std::abs(draws[0].request.revealAmount - 2.5f / 3.0f) < 1.0e-4f);
    draws.clear();
    BuildSequenceTextDraws(clip, 4.5f, 40.0f, 1.08f, 100u, draws);
    Check(draws.size() == 2u && draws[1].text == "えお" && draws[0].request.x > draws[1].request.x);
    Check(std::abs(draws[0].request.x - draws[1].request.x - 64.0f) < 1.0e-3f);
    // 箱の中央が置き場所（960）に来る：箱の幅 = 1.25×40 + 1.6×40。
    const float boxWidth = 50.0f + 64.0f;
    Check(std::abs(draws[0].request.x + 50.0f - (960.0f + boxWidth * 0.5f)) < 1.0e-3f);
    Check(std::abs(draws[0].request.y - 270.0f) < 1.0e-3f);
    Check(draws[0].request.order == 46 && std::abs(draws[0].request.textColor.a - 0.75f) < 1.0e-4f);
}

void CurveEditingContracts();
void SequenceCameraEditingContracts() {
    CurveEditingContracts();
    SequenceTextTrackContracts();
    using namespace HIKARI;
    using namespace HIKARI::EDITOR;
    using namespace HIKARI::SEQUENCER;
    CameraFixtureContracts();
    World world;
    CameraDirector director;
    SequenceBindingContext bindings;
    CinematicSequence sequence;
    CameraCutClip clip;
    clip.id = 1;
    clip.durationSeconds = 4;
    clip.camera.useScenePose = false;
    clip.camera.keys = {{0, {0, 2, -8}, {}, 40}, {2, {4, 3, -4}, {}, 60}, {4, {8, 1, 0}, {}, 50}};
    clip.camera.motion.push_back({});
    clip.camera.motion[0].kind = CameraMotionKind::Dolly;
    clip.camera.motion[0].amount.value = .5f;
    sequence.cameraCutTrack.clips = {clip};
    sequence.durationSeconds = 4;
    NormalizeCinematicSequence(sequence);
    const auto original = sequence;
    const auto eval = [&](const CinematicSequence& value, float time) {
        CinematicCameraEvaluation info;
        Camera3D camera;
        Check(EvaluateSequenceCameraView(value, bindings, world, director, time, 16.f / 9, info, camera));
        return camera;
    };
    const auto second = SplitSequenceShot(sequence, 1, 1.3f);
    Check(second != 0);
    for (int i = 0; i <= 80; ++i)
        Check(Close(eval(original, i * .05f), eval(sequence, i * .05f)));
    const auto duplicate = DuplicateSequenceShot(sequence, second);
    Check(duplicate != 0);
    const auto& copy = sequence.cameraCutTrack.clips.back();
    for (int i = 0; i <= 40; ++i) {
        const float local = copy.durationSeconds * i / 40;
        Camera3D a, b;
        Check(EvaluateSequenceShotView(sequence, bindings, copy, world, director, local, 1.5f, a));
        Check(EvaluateSequenceShotView(original, bindings, clip, world, director, local + 1.3f, 1.5f, b));
        Check(Close(a, b));
    }
    // 同一素材のキーは曲線編集からも空間編集からも一つだけ更新される。
    auto& camera = sequence.cameraCutTrack.clips.front().camera;
    auto curve = GetShotPoseCurve(camera, 0);
    curve.keys[1].value = 7;
    curve.keys[1].outSlope = 2;
    SetShotPoseCurve(camera, 0, curve);
    Check(camera.keys[1].position.x == 7 && camera.keys[1].position.y == 3);
    Check(GetShotPoseCurve(camera, 0) == curve);
    camera.timing.keys = {{0, 0, 0, 0}, {4, 4, 0, 0}};
    auto untimed = camera;
    untimed.timing = {};
    Check(MATH::Length(EvaluateShotPose(camera, ShotMaterialTime(camera, 1)).position -
                       EvaluateShotPose(untimed, .625f).position) < .0001f);
    nlohmann::json saved;
    SerializeCinematicSequenceJson(sequence, saved);
    CinematicSequence loaded;
    DeserializeCinematicSequenceJson(saved, loaded);
    nlohmann::json again;
    SerializeCinematicSequenceJson(loaded, again);
    Check(saved == again);
    // 削除した全域軌道は黙って捨てず、明示的に拒否する。
    auto invalid = saved;
    invalid["tracks"].push_back({{"type", "cameraTransform"}});
    bool rejected = false;
    try {
        DeserializeCinematicSequenceJson(invalid, loaded);
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected);
    // 場景ファイルの読込境界では例外を外へ出さず、読込失敗として返す（旧版の場景で編集器が落ちない）。
    {
        const auto scenePath = std::filesystem::temp_directory_path() / "hikari-legacy-camera-track.scene.json";
        nlohmann::json legacyScene{{"version", kCurrentSceneDocumentVersion},
                                   {"cinematics", {{"sequences", nlohmann::json::array({invalid})}}}};
        std::ofstream(scenePath) << legacyScene.dump();
        SceneDocument document;
        bool threw = false;
        bool loadedScene = true;
        try {
            loadedScene = SceneSerializer{}.LoadFromFile(scenePath.string(), document);
        } catch (...) {
            threw = true;
        }
        std::filesystem::remove(scenePath);
        Check(!threw && !loadedScene);
    }
    // 同じ曲線ウィジェットを使う UI の保存でも保持・線形モードが失われない。
    UiAnimationAsset ui;
    ui.clips.push_back({});
    ui.clips[0].duration = 1;
    ui.clips[0].tracks.push_back({});
    auto& uiCurve = ui.clips[0].tracks[0].curve;
    uiCurve.keys = {
        {0, 1, 0, 0, ScalarCurveInterpolation::Hold}, {.5f, 2, 0, 0, ScalarCurveInterpolation::Linear}, {1, 4, 0, 0}};
    UiAnimationAsset reread;
    std::string uiError;
    Check(ReadUiAnimationAsset(WriteUiAnimationAsset(ui), reread, uiError));
    Check(reread.clips[0].tracks[0].curve == uiCurve);
    Check(std::abs(uiCurve.Evaluate(.25f) - 1) < .0001f && std::abs(uiCurve.Evaluate(.75f) - 3) < .0001f);
    // 文書の Undo/Redo は曲線と分割を同じ原本として戻す。
    SequenceEditorDocument document;
    document.NewAsset("Unified camera");
    *document.GetAssetSequence() = original;
    SceneCinematicsSettings embedded;
    auto before = document.GetSettings(embedded);
    Check(SplitSequenceShot(*document.GetAssetSequence(), 1, 2) != 0);
    document.RecordApplied(before, 0);
    Check(document.IsDirty() && document.Undo());
    Check(document.GetAssetSequence()->cameraCutTrack.clips.size() == 1);
    Check(document.Redo() && document.GetAssetSequence()->cameraCutTrack.clips.size() == 2);
    // Scene の共有機位を変更すると、参照する全ての片段が同じ値を再評価する。
    auto* object = world.CreateObject("Shared station");
    Check(object->SetDocumentId({41}));
    Check(object->AddComponent<CameraComponent>() != nullptr);
    SequenceBinding binding;
    binding.id = {1};
    binding.sceneObjectId = {41};
    sequence.bindings = {binding};
    CameraCutClip station;
    station.id = 9;
    station.camera.useScenePose = false;
    station.camera.base.reference = {1};
    station.camera.base.position = {};
    station.camera.base.fov = 0;
    Camera3D start, moved;
    Check(EvaluateSequenceShotView(sequence, bindings, station, world, director, 0, 1, start));
    Check(object->SetLocalPosition({3, 0, 0}));
    Check(EvaluateSequenceShotView(sequence, bindings, station, world, director, 0, 1, moved));
    Check(std::abs(moved.GetPosition().x - start.GetPosition().x - 3) < .0001f);
    // 曲線の絶対値を編集しても共有参照が残り、他軸の偏移は増えない。
    station.camera.keys = {station.camera.base, station.camera.base};
    station.camera.keys[0].time = 0;
    station.camera.keys[1].time = 2;
    ShotCamera resolved;
    Check(ResolveSequenceShotCamera(sequence, bindings, station, world, director, resolved));
    auto sharedCurve = GetShotPoseCurve(resolved, 0);
    sharedCurve.keys[0].value += 1;
    std::array<ScalarCurve, 4> sharedCurves;
    for (unsigned c = 0; c < 4; ++c)
        sharedCurves[c] = GetShotPoseCurve(resolved, c);
    sharedCurves[0] = sharedCurve;
    ApplyResolvedShotPoseCurves(station.camera, resolved, sharedCurves);
    Check(station.camera.keys[0].reference == SequenceBindingId{1});
    Check(std::abs(station.camera.keys[0].position.x - 1) < .0001f);
    Check(EvaluateSequenceShotView(sequence, bindings, station, world, director, 0, 1, moved));
    Check(std::abs(moved.GetPosition().x - 4) < .0001f);
    station.camera.keys.clear();
    station.camera.base.reference = {99};
    Check(!EvaluateSequenceShotView(sequence, bindings, station, world, director, 0, 1, moved));
}
