#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;

// 物体の親の世界行列（親が無ければ単位）。键は親に対する局所の姿勢なので、空間视图との出し入れに使う。
MATH::Mat4 ParentWorld(const GameObject& object) {
    const GameObject* parent = object.GetParent();
    return parent ? parent->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
}

// time の键を置く（半フレーム以内に键があれば、その時刻と動き方を保って姿勢だけ書き換える）。置いた键の番号。
int PutObjectKey(SEQUENCER::ObjectTrack& track, SEQUENCER::ObjectTrackKey key, float time, float tolerance) {
    for (size_t i = 0; i < track.keys.size(); ++i)
        if (std::abs(track.keys[i].time - time) <= tolerance) {
            key.time = track.keys[i].time;
            key.interpolation = track.keys[i].interpolation;
            track.keys[i] = key;
            return static_cast<int>(i);
        }
    if (track.keys.size() >= SEQUENCER::kMaximumObjectTrackKeys)
        return -1;
    key.time = time;
    const auto at = std::upper_bound(track.keys.begin(), track.keys.end(), time,
                                     [](float t, const SEQUENCER::ObjectTrackKey& k) { return t < k.time; });
    // 新しい键の動き方は前の键に合わせる。
    if (at != track.keys.begin())
        key.interpolation = std::prev(at)->interpolation;
    return static_cast<int>(track.keys.insert(at, key) - track.keys.begin());
}
} // namespace

SEQUENCER::ObjectTrack* CinematicsWorkspaceController::SelectedObjectTrack(CinematicSequence& sequence) {
    if (selectionKind_ != SelectionKind::Object)
        return nullptr;
    for (auto& track : sequence.objectTracks)
        if (track.id == selectedId_)
            return &track;
    return nullptr;
}

bool CinematicsWorkspaceController::ObjectPoseAt(DocumentSceneBase& scene, const CinematicSequence& sequence,
                                                 const SEQUENCER::ObjectTrack& track, float seconds,
                                                 SEQUENCER::ObjectTrackKey& out) const {
    SEQUENCER::ObjectTrackSample sample;
    if (SEQUENCER::SampleObjectTrack(track, seconds, sample)) {
        out.position = sample.position;
        out.rotation = sample.rotation;
        out.scale = sample.scale;
        out.light = sample.light;
        return true;
    }
    SceneObjectId id;
    const GameObject* object = previewBindings_.Resolve(sequence.bindings, track.target, id)
                                   ? scene.GetWorld().FindObject(id)
                                   : nullptr;
    Transform3D local;
    if (!object || !scene.GetWorld().TryGetLocalTransform(object->GetRuntimeHandle(), local))
        return false;
    out.position = local.position;
    out.rotation = local.rotation;
    out.scale = local.scale;
    out.light = 1.0f;
    return true;
}

bool CinematicsWorkspaceController::CreateObjectTrack(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                      SceneObjectId objectId) {
    const GameObject* object = scene.GetWorld().FindObject(objectId);
    if (!object)
        return false;
    // 同じ物体の轨が既にあれば、それを選ぶ（二本の轨が同じ物体を取り合わないように）。
    for (const auto& track : sequence.objectTracks) {
        SceneObjectId bound;
        if (previewBindings_.Resolve(sequence.bindings, track.target, bound) && bound == objectId) {
            selectionKind_ = SelectionKind::Object;
            selectedId_ = track.id;
            selectedObjectKey_ = -1;
            return false;
        }
    }
    if (sequence.objectTracks.size() >= SEQUENCER::kMaximumObjectTracks)
        return false;
    SEQUENCER::ObjectTrack track;
    for (const auto& other : sequence.objectTracks)
        track.id = (std::max)(track.id, other.id + 1);
    track.name = object->GetName();
    track.target = SEQUENCER::FindOrCreateSceneObjectBinding(sequence.bindings, objectId, track.name);
    SEQUENCER::ObjectTrackKey key;
    if (!ObjectPoseAt(scene, sequence, track, 0.0f, key))
        return false;
    key.time = cursor_.GetTimeSeconds();
    track.keys.push_back(key);
    sequence.objectTracks.push_back(std::move(track));
    selectionKind_ = SelectionKind::Object;
    selectedId_ = sequence.objectTracks.back().id;
    selectedObjectKey_ = 0;
    previewEnabled_ = true;
    return true;
}

bool CinematicsWorkspaceController::AddObjectKeyAtCursor(DocumentSceneBase& scene, CinematicSequence& sequence) {
    auto* track = SelectedObjectTrack(sequence);
    SEQUENCER::ObjectTrackKey key;
    if (!track || !ObjectPoseAt(scene, sequence, *track, cursor_.GetTimeSeconds(), key))
        return false;
    selectedObjectKey_ = PutObjectKey(*track, key, cursor_.GetTimeSeconds(), 0.5f / curveView_.fps);
    return selectedObjectKey_ >= 0;
}

bool CinematicsWorkspaceController::DrawObjectTrackMenu(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.objectTrack").c_str(), "addObjectTrack"))
        ImGui::OpenPopup("objectTrackMenu");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TEXT::Localize("cine.newObjectTrack").c_str());
    if (!ImGui::BeginPopup("objectTrackMenu"))
        return false;
    static ImGuiTextFilter filter;
    if (ImGui::IsWindowAppearing()) {
        filter.Clear();
        ImGui::SetKeyboardFocusHere();
    }
    filter.Draw(TEXT::Localize("uix.search").c_str());
    // 今選んでいる物体を先頭に出す。
    const auto& objects = scene.GetSceneDocument().objects;
    for (const auto& object : objects)
        if (object.id == activeSceneObject_ && ImGui::Selectable(("● " + object.name).c_str())) {
            changed |= CreateObjectTrack(scene, sequence, object.id);
            ImGui::CloseCurrentPopup();
        }
    ImGui::Separator();
    if (ImGui::BeginChild("objects", {280.0f, 260.0f})) {
        for (const auto& object : objects) {
            if (!filter.PassFilter(object.name.c_str()))
                continue;
            if (ImGui::Selectable((object.name + "##" + std::to_string(object.id.value)).c_str())) {
                changed |= CreateObjectTrack(scene, sequence, object.id);
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
#else
    (void)scene;
    (void)sequence;
#endif
    return changed;
}

bool CinematicsWorkspaceController::DrawObjectTrackLane(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                        SEQUENCER::ObjectTrack& track, float timeOriginX, float y,
                                                        float width, float rowHeight) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    auto* draw = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    const bool selectedTrack = selectionKind_ == SelectionKind::Object && selectedId_ == track.id;
    const float middle = y + rowHeight * 0.5f;
    // 最初から最後の键までを帯で示す（键の外は端の姿勢のまま）。
    if (!track.keys.empty()) {
        const float x0 = timeOriginX + track.keys.front().time * zoom_;
        const float x1 = timeOriginX + track.keys.back().time * zoom_;
        draw->AddRectFilled({x0, y + 9}, {(std::max)(x0 + 2.0f, x1), y + rowHeight - 9},
                            track.enabled ? IM_COL32(150, 112, 58, 170) : IM_COL32(90, 80, 70, 90), 3);
        if (selectedTrack)
            draw->AddRect({x0 - 1, y + 8}, {(std::max)(x0 + 2.0f, x1) + 1, y + rowHeight - 8},
                          IM_COL32(235, 245, 255, 160), 3);
    }
    ImGui::PushID("objectTrack");
    ImGui::PushID(static_cast<int>(track.id));
    // 行の空き所：押すと轨を選び、双击した時刻に今の姿勢で键を足す。
    ImGui::SetCursorScreenPos({timeOriginX, y});
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("lane", {(std::max)(1.0f, width), rowHeight});
    if (ImGui::IsItemClicked()) {
        selectionKind_ = SelectionKind::Object;
        selectedId_ = track.id;
        selectedObjectKey_ = -1;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        const float time = SnapTimelineTime(sequence, (std::max)(0.0f, (io.MousePos.x - timeOriginX) / zoom_), 0);
        SEQUENCER::ObjectTrackKey key;
        if (ObjectPoseAt(scene, sequence, track, time, key)) {
            selectedObjectKey_ = PutObjectKey(track, key, time, 0.5f / curveView_.fps);
            cursor_.Pause();
            cursor_.Seek(time);
            previewEnabled_ = true;
            changed = true;
        }
    }
    // 键の菱形：押して選び（再生位置もそこへ）、左右に引くと時刻を変える（前後の键は越えない）。
    const float frame = 1.0f / (std::max)(1.0f, curveView_.fps);
    for (size_t i = 0; i < track.keys.size(); ++i) {
        auto& key = track.keys[i];
        const float px = timeOriginX + key.time * zoom_;
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetCursorScreenPos({px - 7.0f, middle - 7.0f});
        ImGui::InvisibleButton("key", {14.0f, 14.0f});
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (ImGui::IsItemActivated()) {
            selectionKind_ = SelectionKind::Object;
            selectedId_ = track.id;
            selectedObjectKey_ = static_cast<int>(i);
            draggingId_ = track.id;
            draggingKind_ = SelectionKind::Object;
            dragStart_ = key.time;
            dragMouseX_ = io.MousePos.x;
            cursor_.Pause();
            cursor_.Seek(key.time);
            previewEnabled_ = true;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            const float lower = i > 0 ? track.keys[i - 1].time + frame : 0.0f;
            const float upper = i + 1 < track.keys.size() ? track.keys[i + 1].time - frame : 1.0e9f;
            const float time =
                SnapTimelineTime(sequence, dragStart_ + (io.MousePos.x - dragMouseX_) / zoom_, 0, false);
            if (lower <= upper && std::abs(std::clamp(time, lower, upper) - key.time) > 1.0e-6f) {
                key.time = std::clamp(time, lower, upper);
                cursor_.Seek(key.time);
                changed = true;
            }
        }
        if (hot)
            ImGui::SetTooltip("%.2f s", key.time);
        const bool chosen = selectedTrack && selectedObjectKey_ == static_cast<int>(i);
        const float r = chosen || hot ? 6.5f : 5.0f;
        const ImU32 color = chosen ? IM_COL32(255, 220, 120, 255)
                            : hot  ? IM_COL32(255, 255, 255, 255)
                                   : IM_COL32(236, 196, 128, 235);
        draw->AddQuadFilled({px, middle - r}, {px + r, middle}, {px, middle + r}, {px - r, middle}, color);
        if (key.interpolation == SEQUENCER::ObjectKeyInterpolation::Step)
            draw->AddLine({px + r + 1, middle}, {px + r + 5, middle}, color, 1.5f);
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::PopID();
#else
    (void)scene;
    (void)sequence;
    (void)track;
    (void)timeOriginX;
    (void)y;
    (void)width;
    (void)rowHeight;
#endif
    return changed;
}

bool CinematicsWorkspaceController::DrawObjectTrackInspector(DocumentSceneBase& scene, CinematicSequence& sequence) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    auto* track = SelectedObjectTrack(sequence);
    if (!track)
        return false;
    SceneObjectId objectId;
    GameObject* object = previewBindings_.Resolve(sequence.bindings, track->target, objectId)
                             ? scene.GetWorld().FindObject(objectId)
                             : nullptr;
    PanelTitle(track->name.c_str(), object ? nullptr : TEXT::Localize("cine.unbound").c_str());
    changed |= ImGui::Checkbox(TEXT::Localize("ui.vfx.effects.enabled").c_str(), &track->enabled);
    changed |= ImGui::Checkbox(TEXT::Localize("cine.restoreOnStop").c_str(), &track->restoreOnStop);
    if (object && object->GetComponent<LightComponent>())
        changed |= ImGui::Checkbox(TEXT::Localize("cine.animateLight").c_str(), &track->animateLight);
    ImGui::Separator();
    if (selectedObjectKey_ >= static_cast<int>(track->keys.size()))
        selectedObjectKey_ = -1;
    if (selectedObjectKey_ < 0) {
        ImGui::TextDisabled(TEXT::Localize("cine.objectKeyCount").c_str(), static_cast<int>(track->keys.size()));
        if (IconTextButton(EditorGlyph::Keyframe, TEXT::Localize("cine.addKey").c_str(), "addObjectKey"))
            changed |= AddObjectKeyAtCursor(scene, sequence);
    } else {
        const size_t index = static_cast<size_t>(selectedObjectKey_);
        auto& key = track->keys[index];
        // 時刻は前後の键の間に限る（键の並びと番号を変えない）。
        const float frame = 1.0f / (std::max)(1.0f, curveView_.fps);
        const float lower = index > 0 ? track->keys[index - 1].time + frame : 0.0f;
        const float upper = index + 1 < track->keys.size() ? track->keys[index + 1].time - frame
                                                           : kMaxCinematicSequenceDurationSeconds;
        if (ImGui::DragFloat(TEXT::Localize("cine.keyTime").c_str(), &key.time, frame, lower, upper, "%.3f s")) {
            key.time = std::clamp(key.time, lower, (std::max)(lower, upper));
            cursor_.Seek(key.time);
            changed = true;
        }
        changed |= ImGui::DragFloat3(TEXT::Localize("cine.position").c_str(), &key.position.x, 0.01f);
        // 角度は表示の度数を覚え、同じ向きの別表現へ跳ばないよう前の値に寄せる。
        const bool sameRotation = objectEulerSource_.x == key.rotation.x && objectEulerSource_.y == key.rotation.y &&
                                  objectEulerSource_.z == key.rotation.z && objectEulerSource_.w == key.rotation.w;
        if (!sameRotation)
            objectEuler_ = MATH::EulerXYZDegreesFromQuatNearest(key.rotation, objectEuler_);
        if (ImGui::DragFloat3(TEXT::Localize("cine.objectRotation").c_str(), &objectEuler_.x, 0.25f)) {
            key.rotation = MATH::Quat::FromEulerXYZ(objectEuler_.x * kRadians, objectEuler_.y * kRadians,
                                                    objectEuler_.z * kRadians);
            changed = true;
        }
        objectEulerSource_ = key.rotation;
        changed |= ImGui::DragFloat3(TEXT::Localize("cine.objectScale").c_str(), &key.scale.x, 0.005f, 0.0f, 1000.0f);
        if (track->animateLight)
            changed |= ImGui::DragFloat(TEXT::Localize("cine.lightScale").c_str(), &key.light, 0.01f, 0.0f, 100.0f,
                                        "×%.2f");
        static constexpr std::array kInterpolationKeys{"cine.interpSmooth", "cine.interpLinear", "cine.interpEase",
                                                       "cine.interpStep"};
        int interpolation = static_cast<int>(key.interpolation);
        const auto current = TEXT::Localize(kInterpolationKeys[static_cast<size_t>(interpolation)]);
        if (ImGui::BeginCombo(TEXT::Localize("cine.interpolation").c_str(), current.c_str())) {
            for (int i = 0; i < static_cast<int>(kInterpolationKeys.size()); ++i)
                if (ImGui::Selectable(TEXT::Localize(kInterpolationKeys[static_cast<size_t>(i)]).c_str(),
                                      i == interpolation)) {
                    key.interpolation = static_cast<SEQUENCER::ObjectKeyInterpolation>(i);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        // 場面に置いた姿勢（演出の外での位置）をこの键に写す。演出の終わりに元の場所へ戻す時など。
        const SceneObjectData* placed = nullptr;
        for (const auto& data : scene.GetSceneDocument().objects)
            if (data.id == objectId)
                placed = &data;
        ImGui::BeginDisabled(placed == nullptr);
        if (ImGui::Button(TEXT::Localize("cine.keyFromScene").c_str()) && placed) {
            const auto& t = placed->transform;
            key.position = t.position;
            key.rotation = MATH::Quat::FromEulerXYZ(t.rotationEulerDeg.x * kRadians, t.rotationEulerDeg.y * kRadians,
                                                    t.rotationEulerDeg.z * kRadians);
            key.scale = t.scale;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("cine.deleteKey").c_str())) {
            track->keys.erase(track->keys.begin() + static_cast<std::ptrdiff_t>(index));
            selectedObjectKey_ = -1;
            return true;
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", TEXT::Localize("cine.objectTrackHint").c_str());
    if (ImGui::Button(TEXT::Localize("cine.deleteTrack").c_str())) {
        selectedObjectKey_ = -1;
        return DeleteSelectedClip(sequence);
    }
#else
    (void)scene;
    (void)sequence;
#endif
    return changed;
}

bool CinematicsWorkspaceController::DrawObjectTrackGizmo(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                         const EditorViewportRect& viewport, bool& interacting) {
#if defined(HIKARI_WITH_EDITOR)
    auto* track = SelectedObjectTrack(sequence);
    SceneObjectId objectId;
    const GameObject* object = track && previewBindings_.Resolve(sequence.bindings, track->target, objectId)
                                   ? scene.GetWorld().FindObject(objectId)
                                   : nullptr;
    if (!object)
        return false;
    const MATH::Mat4 parent = ParentWorld(*object);
    const auto& observer = directorViewPanel_.GetViewCamera();
    const ImVec2 origin{viewport.x, viewport.y}, size{viewport.width, viewport.height};
    auto* draw = ImGui::GetWindowDrawList();
    const auto worldPoint = [&](const MATH::Vec3& local) {
        const auto v = parent.TransformPoint({local.x, local.y, local.z, 1.0f});
        return MATH::Vec3{v.x, v.y, v.z};
    };
    // 道筋（最初から最後の键まで）と键の位置。
    if (track->keys.size() >= 2) {
        const float first = track->keys.front().time, last = track->keys.back().time;
        ImVec2 previous;
        bool previousValid = false;
        for (int i = 0; i <= 64; ++i) {
            SEQUENCER::ObjectTrackSample sample;
            SEQUENCER::SampleObjectTrack(*track, first + (last - first) * i / 64.0f, sample);
            ImVec2 screen;
            const bool valid = DIRECTOR_VIEW::ProjectWorldToViewport(observer, worldPoint(sample.position), origin,
                                                                     size, screen);
            if (valid && previousValid)
                draw->AddLine(previous, screen, IM_COL32(236, 196, 128, 200), 1.5f);
            previous = screen;
            previousValid = valid;
        }
        for (size_t i = 0; i < track->keys.size(); ++i) {
            ImVec2 screen;
            if (DIRECTOR_VIEW::ProjectWorldToViewport(observer, worldPoint(track->keys[i].position), origin, size,
                                                      screen)) {
                const bool chosen = selectedObjectKey_ == static_cast<int>(i);
                const float r = chosen ? 5.0f : 3.5f;
                draw->AddQuadFilled({screen.x, screen.y - r}, {screen.x + r, screen.y}, {screen.x, screen.y + r},
                                    {screen.x - r, screen.y},
                                    chosen ? IM_COL32(255, 220, 120, 255) : IM_COL32(236, 196, 128, 230));
            }
        }
    }
    // 手柄は再生位置の姿勢に出す。W 移動・E 回転・R 大きさ（物体には注視点が無いので R は大きさ）。
    const float time = cursor_.GetTimeSeconds();
    SEQUENCER::ObjectTrackKey pose;
    if (!ObjectPoseAt(scene, sequence, *track, time, pose))
        return false;
    MATH::Vec3 position, scale;
    MATH::Quat rotation;
    if (!MATH::DecomposeTRS(parent * MATH::Mat4::TRS(pose.position, pose.rotation, pose.scale), position, rotation,
                            scale))
        return false;
    TransformData transform;
    transform.position = position;
    transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(rotation);
    transform.scale = scale;
    EditorTransformGizmoState state;
    state.operation = stageTool_ == StageTool::Rotate ? EditorTransformGizmoOperation::Rotate
                      : stageTool_ == StageTool::Aim  ? EditorTransformGizmoOperation::Scale
                                                      : EditorTransformGizmoOperation::Translate;
    state.snapEnabled = ImGui::GetIO().KeyCtrl;
    const auto edit = stageGizmo_.DrawTransform(transform, observer, state, viewport);
    interacting |= edit.interacting;
    if (!edit.changed)
        return false;
    if (state.operation == EditorTransformGizmoOperation::Translate)
        position = edit.transform.position;
    else if (state.operation == EditorTransformGizmoOperation::Rotate)
        rotation = edit.rotation;
    else
        scale = edit.transform.scale;
    SEQUENCER::ObjectTrackKey key = pose;
    if (!MATH::DecomposeTRS(MATH::Inverse(parent) * MATH::Mat4::TRS(position, rotation, scale), key.position,
                            key.rotation, key.scale))
        return false;
    selectedObjectKey_ = PutObjectKey(*track, key, time, 0.5f / curveView_.fps);
    cursor_.Pause();
    previewEnabled_ = true;
    return selectedObjectKey_ >= 0;
#else
    (void)scene;
    (void)sequence;
    (void)viewport;
    (void)interacting;
    return false;
#endif
}
} // namespace HIKARI::EDITOR
