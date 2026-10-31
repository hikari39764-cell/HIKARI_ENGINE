#include "Editor/Authoring/HIKARI_CurveKeyEditing.h"
#include "Editor/Authoring/HIKARI_SequenceCameraEditing.h"
#include "Editor/Widgets/HIKARI_ScalarCurveEditor.h"
#include "Core/Serialization/Json/HIKARI_ScalarCurveJson.h"
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Scene/Sequencer/Serialization/HIKARI_CinematicSequenceJson.h"
#include <cmath>
#include <iostream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace HIKARI;
using namespace HIKARI::EDITOR;
void Check(bool value, const std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("Curve editing contract: " + std::to_string(where.line()));
}
bool Near(float a, float b, float tolerance = .0001f) { return std::abs(a - b) < tolerance; }
ScalarCurve Line() {
    ScalarCurve curve;
    curve.keys = {{0, 0, 1, 1}, {1, 1, 1, 1}, {2, 2, 1, 1}, {3, 3, 1, 1}, {4, 4, 1, 1}};
    return curve;
}
void CurveEvaluationContracts() {
    auto curve = Line();
    for (int i = 0; i <= 400; ++i) {
        Check(Near(curve.Evaluate(i * .01f), i * .01f));
        Check(Near(curve.Velocity(i * .01f), 1));
    }
    CurveEditChannel channel{"x", "X", &curve, 0, 4};
    std::span channels{&channel, size_t{1}};
    CurveKeyEditing edit;
    edit.selection = {{"x", 1}, {"x", 2}};
    Check(edit.ApplyPreset(channels, CurveKeyPreset::Ease));
    Check(curve.keys[1].value == 1 && curve.keys[2].value == 2);
    Check(Near(curve.Velocity(1), 0) && Near(curve.Velocity(2), 0));
    curve.keys[1].shape.outWeight = .7f;
    curve.keys[2].shape.inWeight = .1f;
    Check(Near(curve.Evaluate(1), 1) && Near(curve.Evaluate(2), 2));
    Check(!Near(curve.Evaluate(1.5f), 1.5f));
    const float epsilon = .001f;
    Check(Near(curve.Velocity(1.5f), (curve.Evaluate(1.5f + epsilon) - curve.Evaluate(1.5f - epsilon)) / (2 * epsilon),
               .002f));
    const auto saved = WriteScalarCurveKeyJson(curve.keys[1]);
    Check(ReadScalarCurveKeyJson(saved) == curve.keys[1]);
    edit.selection = {{"x", 1}};
    edit.ApplyPreset(channels, CurveKeyPreset::EaseIn);
    Check(curve.keys[1].inSlope == 0);
    Check(edit.ApplyPreset(channels, CurveKeyPreset::Linear));
    Check(Near(curve.Evaluate(1.3f), 1.3f));
    Check(edit.ApplyPreset(channels, CurveKeyPreset::Hold));
    Check(curve.Evaluate(1.9f) == 1 && curve.Evaluate(2) == 2);
    Check(edit.ApplyPreset(channels, CurveKeyPreset::Auto));
    curve.keys[2].value = 4;
    Check(Near(ResolvedScalarKey(curve.keys, 1).outSlope, 2));

    // 時間曲線の過大な手柄も到達点を変えず、全区間で前進する。
    curve.keys = {{0, 0, 0, 100}, {2, 2, 100, 0}};
    channel.end = 2;
    channel.monotonic = true;
    channel.locked = {0, 1};
    CurveKeyEditing::Constrain(channel);
    float previous = 0;
    for (int i = 0; i <= 200; ++i) {
        const float value = curve.Evaluate(i * .01f);
        Check(value >= previous - .00001f && value <= 2.00001f);
        previous = value;
    }
    edit.selection = {{"x", 0}, {"x", 1}};
    edit.Begin(channels);
    edit.Transform(channels, 1, 20);
    edit.End();
    Check(curve.keys.front().time == 0 && curve.keys.front().value == 0 && curve.keys.back().time == 2 &&
          curve.keys.back().value == 2);
    Check(!edit.Delete(channels));

    UiAnimationAsset ui;
    ui.clips.push_back({});
    ui.clips[0].duration = 4;
    ui.clips[0].tracks.push_back({});
    ui.clips[0].tracks[0].curve = curve;
    curve.keys[0].shape.outWeight = .6f;
    ui.clips[0].tracks[0].curve = curve;
    UiAnimationAsset loaded;
    std::string error;
    Check(ReadUiAnimationAsset(WriteUiAnimationAsset(ui), loaded, error));
    Check(loaded.clips[0].tracks[0].curve == curve);
}
void GroupEditingContracts() {
    auto x = Line(), y = Line();
    std::array channels{CurveEditChannel{"x", "X", &x, 0, 4, 0}, CurveEditChannel{"y", "Y", &y, 0, 4, 0}};
    channels[0].locked = channels[1].locked = {0, 4};
    CurveKeyEditing edit;
    edit.selection = {{"x", 1}, {"x", 2}, {"x", 3}, {"y", 2}};
    edit.Begin(channels);
    Check(edit.Transform(channels, .5f, 2));
    Check(x.keys[1].time == 1.5f && y.keys[1].time == 1.5f && y.keys[1].value == 1);
    Check(y.keys[2].value == 4 && x.keys[2].value == 4);
    Check(edit.Cancel(channels));
    Check(x == Line() && y == Line());
    edit.Begin(channels);
    Check(edit.Transform(channels, 10));
    Check(Near(x.keys[2].time - x.keys[1].time, 1));
    Check(x.keys[3].time < 4 && x.keys[0].time == 0 && x.keys[4].time == 4);
    edit.Cancel(channels);
    edit.selection = {{"x", 1}};
    edit.Copy(channels);
    Check(edit.Paste(channels, 1.5f, "x"));
    Check(x.keys.size() == 6 && y.keys.size() == 6 && x.keys[2].time == 1.5f && y.keys[2].time == 1.5f);
    Check(edit.Delete(channels));
    Check(x == Line() && y == Line());
    // 一部が範囲外になる貼り付けは、成功した部分も含めて元へ戻す。
    edit.selection = {{"x", 1}, {"x", 2}};
    edit.Copy(channels);
    Check(!edit.Paste(channels, 3.5f, "x"));
    Check(x == Line() && y == Line());
    edit.selection = {{"x", 1}};
    edit.ApplyPreset(channels, CurveKeyPreset::Ease);
    edit.Copy(channels);
    x = Line();
    y = Line();
    for (auto& key : x.keys)
        key.interpolation = ScalarCurveInterpolation::Linear;
    Check(edit.PasteEasing(channels));
    Check(Near(x.Velocity(.9999f), 0, .001f) && Near(x.Velocity(1), 0));
    Check(x.keys[1].time == 1 && x.keys[1].value == 1);
    x = Line();
    edit.selection = {{"x", 1}, {"x", 3}};
    edit.Begin(channels);
    Check(edit.Transform(channels, 0, 0, .5f, 1));
    edit.End();
    // 未選択の中間キーと重なる伸縮は拒否され、順序を破壊しない。
    for (size_t i = 1; i < x.keys.size(); ++i)
        Check(x.keys[i].time > x.keys[i - 1].time);
}
void PoseProjectionContracts() {
    using namespace HIKARI::SEQUENCER;
    ShotCamera camera;
    camera.useScenePose = false;
    camera.keys = {{0, {0, 1, 2}, {}, 50}, {1, {2, 3, 4}, {}, 60}, {2, {4, 5, 6}, {}, 70}};
    camera.keys[0].reference = {4};
    camera.keys[0].position = {0, 0, 0};
    auto resolved = camera;
    resolved.keys[0].position = {10, 11, 12};
    std::array<ScalarCurve, 4> curves;
    for (unsigned c = 0; c < 4; ++c)
        curves[c] = GetShotPoseCurve(resolved, c);
    const auto before = camera;
    curves[0].keys[1].shape.outWeight = .7f;
    curves[0].keys[1].outSlope = 3;
    curves[1].keys[1].value += 5;
    ApplyResolvedShotPoseCurves(camera, resolved, curves);
    Check(camera.keys[0].reference == before.keys[0].reference && camera.keys[0].position.x == 0);
    Check(camera.keys[0].customTangents == 0 && camera.keys[2].customTangents == 0);
    Check(camera.keys[1].position.y == 8 && camera.keys[1].position.z == 4);
    Check(GetShotPoseCurve(camera, 0).keys[1].shape.outWeight == .7f);
    CinematicSequence sequence;
    sequence.cameraCutTrack.clips.push_back({});
    sequence.cameraCutTrack.clips[0].id = 1;
    sequence.cameraCutTrack.clips[0].camera = camera;
    nlohmann::json json;
    SerializeCinematicSequenceJson(sequence, json);
    CinematicSequence loaded;
    DeserializeCinematicSequenceJson(json, loaded);
    Check(loaded.cameraCutTrack.clips.size() == 1);
    for (int i = 0; i <= 200; ++i) {
        const auto a = EvaluateShotPose(camera, i * .01f),
                   b = EvaluateShotPose(loaded.cameraCutTrack.clips[0].camera, i * .01f);
        Check(MATH::Length(a.position - b.position) < .0001f);
    }
}
void CanvasInputContracts() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {1000, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    auto x = Line(), y = Line();
    std::array channels{CurveEditChannel{"x", "X", &x, 0, 4, 0}, CurveEditChannel{"y", "Y", &y, 0, 4, 0}};
    ScalarCurveView view;
    view.activeChannel = "x";
    bool lanes = true;
    const auto frame = [&](ImVec2 mouse, bool down = false, bool shift = false) {
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1000, 700});
        ImGui::Begin("Curve", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::SetCursorScreenPos({20, 60});
        const CurveCanvasLayout layout =
            lanes ? CurveCanvasLayout{{780, 56}, true, 120, 28, 0, 100} : CurveCanvasLayout{{780, 280}};
        const bool changed = DrawCurveEditorCanvas(channels, 0, view, layout);
        ImGui::End();
        ImGui::Render();
        return changed;
    };
    frame({220, 64});
    frame({220, 64}, true);
    frame({455, 112}, true);
    frame({455, 112});
    Check(view.editing.selection.size() == 6);
    Check(x == Line() && y == Line());
    frame({340, 74});
    frame({340, 74}, true);
    frame({390, 74}, true);
    frame({390, 74});
    Check(view.editing.selection.size() == 6);
    Check(Near(x.keys[1].time, 1.5f) && Near(y.keys[3].time, 3.5f));
    // Shift は既存の選択だけを切り替え、クリックでは再生位置を変更しない。
    frame({390, 74}, false, true);
    frame({390, 74}, true, true);
    frame({390, 74}, false, true);
    Check(view.editing.selection.size() == 5 && !view.seekTime);
    frame({390, 102});
    frame({390, 102}, true);
    frame({410, 102}, true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame({410, 102}, true);
    Check(Near(x.keys[1].time, 1.5f) && Near(y.keys[3].time, 3.5f));
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame({410, 102});
    io.AddKeyEvent(ImGuiKey_F9, true);
    frame({410, 102});
    Check(x.keys[1].inSlope == 0 && y.keys[1].outSlope == 0);
    io.AddKeyEvent(ImGuiKey_F9, false);
    frame({410, 102});
    // マウス座標は整数ピクセルへ丸められるので、値と重みの誤差を一画素以内で確認する。
    // 数値グラフの矩形選択と複数点ドラッグも、帯と同じ選択・時刻グループを使う。
    lanes = false;
    x = Line();
    y = Line();
    view = {};
    view.activeChannel = "x";
    view.fitted = true;
    view.low = -1;
    view.high = 5;
    view.duration = 5;
    channels[1].visible = false;
    frame({200, 150});
    frame({200, 150}, true);
    frame({510, 260}, true);
    frame({510, 260});
    Check(view.editing.selection.size() == 3);
    frame({357.6f, 202});
    frame({357.6f, 202}, true);
    frame({393.8f, 182.3333f}, true);
    frame({393.8f, 182.3333f});
    Check(Near(x.keys[1].time, 1.25f) && Near(x.keys[3].value, 3.5f, .02f));
    Check(Near(y.keys[1].time, 1.25f) && y.keys[1].value == 1);
    // 速度の点と影響手柄を区別する。点を上下しても重みや終点座標が勝手に動かない。
    x = Line();
    y = Line();
    view.editing.selection.clear();
    view.speedGraph = true;
    channels[0].locked = {0, 4};
    frame({647.2f, 241.3333f});
    frame({647.2f, 241.3333f}, true);
    frame({647.2f, 202}, true);
    frame({647.2f, 202});
    Check(Near(x.keys[4].inSlope, 2) && x.keys[4].value == 4 && x.keys[4].time == 4);
    Check(x.keys[4].shape.inWeight == 1.f / 3);
    frame({598.9333f, 202});
    frame({598.9333f, 202}, true);
    frame({550.6667f, 202}, true);
    frame({550.6667f, 202});
    Check(Near(x.keys[4].shape.inWeight, 2.f / 3, .01f) && x.keys[4].value == 4);
    ImGui::DestroyContext();
}
} // namespace
void CurveEditingContracts() {
    CurveEvaluationContracts();
    GroupEditingContracts();
    PoseProjectionContracts();
    CanvasInputContracts();
    std::cout
        << "Curves: weighted evaluation, presets, grouped edits, locks, serialization, marquee/Shift/drag/Escape/F9: PASS\n";
}
