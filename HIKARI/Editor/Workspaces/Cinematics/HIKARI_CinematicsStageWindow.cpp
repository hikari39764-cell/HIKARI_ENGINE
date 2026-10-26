#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_TimeService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
Camera3D CameraFromPose(const Camera3D& lens, const MATH::Vec3& position, const MATH::Quat& rotation) {
    Camera3D camera = lens;
    camera.SetLookAt(position, position + MATH::RotateVector(rotation, {0.0f, 0.0f, 1.0f}),
                     MATH::RotateVector(rotation, {0.0f, 1.0f, 0.0f}));
    return camera;
}

MATH::Quat CameraRotation(const Camera3D& camera) {
    return MATH::QuatFromForwardUp(camera.GetTarget() - camera.GetPosition(), camera.GetUp());
}

bool SameView(const Camera3D& a, const Camera3D& b) {
    const auto close = [](const MATH::Vec3& x, const MATH::Vec3& y) { return MATH::Length(x - y) < 1.0e-4f; };
    return close(a.GetPosition(), b.GetPosition()) && close(a.GetTarget(), b.GetTarget()) &&
           close(a.GetUp(), b.GetUp()) && std::abs(a.GetFovYRad() - b.GetFovYRad()) < 1.0e-5f;
}
} // namespace

Camera3D CinematicsWorkspaceController::ResolveShotCamera(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                          const SEQUENCER::CameraCutClip& shot, float localTime,
                                                          bool includeMotion) const {
    Camera3D camera;
    if (!SEQUENCER::EvaluateSequenceShotView(sequence, previewBindings_, shot, scene.GetWorld(),
                                             scene.GetCameraDirector(), localTime, 16.f / 9, camera, includeMotion))
        return scene.GetCamera();
    return camera;
}

bool CinematicsWorkspaceController::DrawStage(DocumentSceneBase& scene, CinematicSequence& sequence,
                                              EditorContext& context, EditorWorkspaceHost& host, bool enabled) {
    auto& view = host.GetCinematicsDirectorView();
    if (!spaceView_) {
        RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
        pilot_ = false;
        return false;
    }
    bool changed = false;
    if (ImGui::Begin(TEXT::LocalizeWindow("cine.stage", "Cinematics/Space").c_str(), &spaceView_,
                     ImGuiWindowFlags_NoScrollbar)) {
        const auto frameAll = [&] {
            std::vector<MATH::Vec3> positions;
            for (const auto& shot : sequence.cameraCutTrack.clips) {
                for (int sample = 0; sample <= 12; ++sample) {
                    const auto camera = ResolveShotCamera(scene, sequence, shot, shot.durationSeconds * sample / 12.0f);
                    positions.push_back(camera.GetPosition());
                    positions.push_back(camera.GetTarget());
                }
            }
            if (positions.empty())
                positions.push_back(scene.GetCamera().GetTarget());
            auto low = positions.front(), high = low;
            for (const auto& point : positions) {
                low = {(std::min)(low.x, point.x), (std::min)(low.y, point.y), (std::min)(low.z, point.z)};
                high = {(std::max)(high.x, point.x), (std::max)(high.y, point.y), (std::max)(high.z, point.z)};
            }
            const auto center = (low + high) * 0.5f;
            const float radius = (std::max)(3.0f, MATH::Length(high - low) * 0.5f);
            Camera3D camera = scene.GetCamera();
            const auto available = ImGui::GetContentRegionAvail();
            const float aspect = (std::max)(0.1f, available.x / (std::max)(1.0f, available.y - 70));
            const float halfFov = std::atan(std::tan(camera.GetFovYRad() * 0.5f) * (std::min)(1.0f, aspect));
            const float distance = radius * 1.15f / (std::max)(0.05f, std::sin(halfFov));
            camera.SetLookAt(center + MATH::Normalize(MATH::Vec3{1, 0.65f, -1}) * distance, center, {0, 1, 0});
            pilot_ = false;
            directorViewPanel_.SetFreeCamera(camera);
        };
        if (!stageInitialized_ && view.extent.width && view.extent.height) {
            frameAll();
            stageInitialized_ = true;
        }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const auto icon = [&](EditorGlyph glyph, const char* key) {
            return IconButton(glyph, key, EditorButtonTone::Quiet, {26, 26}, TEXT::Localize(key).c_str());
        };
        if (icon(EditorGlyph::Focus, "cine.frameAllCameras") ||
            (!pilot_ && ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_F, focused)))
            frameAll();
        NextToolbarItem();
        if (IconToggleButton(EditorGlyph::Animation, "shotPaths", showShotPaths_, {26, 26},
                             TEXT::Localize("cine.showPaths").c_str()))
            showShotPaths_ = !showShotPaths_;
        NextToolbarItem();
        ImGui::BeginDisabled(!enabled);
        if (icon(EditorGlyph::Add, "cine.shotFromView")) {
            pilot_ = false;
            CreateShot(sequence, directorViewPanel_.GetViewCamera());
            changed = true;
        }
        NextToolbarItem();
        auto* selected = SelectedShot(sequence);
        ImGui::BeginDisabled(pilot_ || (selected && SharedPoseAtCursor(*selected)));
        if (icon(EditorGlyph::Keyframe, "cine.captureView")) {
            CapturePose(scene, sequence, directorViewPanel_.GetViewCamera());
            changed = true;
        }
        ImGui::EndDisabled();
        NextToolbarItem();
        // 手柄の種類（W 移動・E 回転・R 注視点）。
        const auto tool = [&](EditorGlyph glyph, const char* id, StageTool value, const char* key) {
            if (IconToggleButton(glyph, id, stageTool_ == value, {26, 26}, TEXT::Localize(key).c_str()))
                stageTool_ = value;
            NextToolbarItem();
        };
        tool(EditorGlyph::Translate, "stageMove", StageTool::Move, "cine.toolMove");
        tool(EditorGlyph::Rotate, "stageRotate", StageTool::Rotate, "cine.toolRotate");
        tool(EditorGlyph::Focus, "stageAim", StageTool::Aim, "cine.toolAim");
        if (!pilot_ && focused && !ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_W, focused))
                stageTool_ = StageTool::Move;
            if (ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_E, focused))
                stageTool_ = StageTool::Rotate;
            if (ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_R, focused))
                stageTool_ = StageTool::Aim;
        }
        // 驾驶：选んだ镜头の視点で飛ぶ。
        ImGui::BeginDisabled(selected == nullptr || SharedPoseAtCursor(*selected));
        if (IconToggleButton(EditorGlyph::Camera, "pilotShot", pilot_, {26, 26},
                             TEXT::Localize("cine.pilotShot").c_str())) {
            pilot_ = !pilot_;
            pilotShot_ = 0;
            if (pilot_)
                flight_ = FlightState::Off;
        }
        ImGui::EndDisabled();
        NextToolbarItem();
        // 飛行録画は自由に向きを変えられる镜头だけ（注視の相手に結んだ镜头は向きが決まっている）。
        const bool aimLocked = selected && selected->camera.lookAt.IsValid();
        ImGui::BeginDisabled(selected == nullptr || aimLocked || pilot_ || !selected->camera.timing.keys.empty());
        if (IconToggleButton(flight_ == FlightState::Off ? EditorGlyph::Record : EditorGlyph::Stop, "flightRecord",
                             flight_ != FlightState::Off, {26, 26}, TEXT::Localize("cine.recordFlight").c_str())) {
            flight_ = flight_ == FlightState::Off ? FlightState::Armed : FlightState::Off;
            flightShot_ = selected != nullptr ? selected->id : 0;
            if (flight_ == FlightState::Off)
                cursor_.Pause();
        }
        ImGui::EndDisabled();
        if (flight_ != FlightState::Off || pilot_) {
            NextToolbarItem();
            const char* status = pilot_                          ? "cine.pilotActive"
                                 : flight_ == FlightState::Armed ? "cine.flightArmed"
                                                                 : "cine.flightRecording";
            ImGui::TextColored({1.0f, 0.55f, 0.45f, 1.0f}, "%s", TEXT::Localize(status).c_str());
        }
        ImGui::EndDisabled();
        directorViewPanel_.SetTargetCamera({});
        // Esc：驾驶をやめる。驾驶していなければ軌道の点の選択を外す（镜头の手柄へ戻る）。
        if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (pilot_)
                pilot_ = false;
            else
                selectedRailPoint_ = -1;
        }
        changed |= UpdatePilot(scene, sequence, false);
        const auto result = directorViewPanel_.DrawContents(scene, view, {}, context.transformGizmo,
                                                            TIME::GetFrameContext().unscaledDt, !enabled, false);
        if (enabled)
            changed |= UpdatePilot(scene, sequence, result.viewMoved);
        // 右ボタンで飛び始めると视口のボタンが活性になるので、活性の項目があっても窓の上かどうかで判定する。
        changed |= UpdateFlightRecording(scene, sequence,
                                         enabled &&
                                             ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                                    ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                             ImGui::IsMouseDown(ImGuiMouseButton_Right));
        changed |= DrawShotOverlays(scene, sequence, result.viewport, enabled);
    } else {
        RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
        flight_ = FlightState::Off;
        pilot_ = false;
    }
    ImGui::End();
    return changed;
}

bool CinematicsWorkspaceController::UpdatePilot(DocumentSceneBase& scene, CinematicSequence& sequence, bool viewMoved) {
    auto* shot = SelectedShot(sequence);
    if (!pilot_ || shot == nullptr || SharedPoseAtCursor(*shot)) {
        pilot_ = false;
        return false;
    }
    const float local = ShotLocalTime(*shot);
    if (viewMoved) {
        // 飛んだ視点を镜头の姿勢にする。傾きと画角は镜头のまま（视图の操作は傾けない）。
        const Camera3D view = directorViewPanel_.GetViewCamera();
        const auto current =
            CaptureShotPoseFor(scene, sequence, *shot, ResolveShotCamera(scene, sequence, *shot, local, false), local);
        SEQUENCER::ShotPose pose = CaptureShotPoseFor(scene, sequence, *shot, view, local);
        pose.rotation = SEQUENCER::ShotRotationLookingAlong(SEQUENCER::ShotPoseForward(pose), current.rotation);
        WriteShotPose(sequence, *shot, pose, kPosePosition | kPoseRotation);
        cursor_.Pause();
        pilotCamera_ = view;
        pilotShot_ = shot->id;
        return true;
    }
    // 再生位置や镜头が変わったら視点を合わせる（同じ镜头の中は画の履歴を保つ）。
    const Camera3D desired = ResolveShotCamera(scene, sequence, *shot, local, false);
    if (pilotShot_ != shot->id || !SameView(desired, pilotCamera_)) {
        directorViewPanel_.SetFreeCamera(desired, pilotShot_ != shot->id);
        pilotCamera_ = desired;
        pilotShot_ = shot->id;
    }
    return false;
}

bool CinematicsWorkspaceController::UpdateFlightRecording(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                          bool startRequested) {
    // 键の間隔 [s]。键の間は滑らかにつなぐので、これで手の揺れの細部は残しつつ键が増えすぎない。
    constexpr float kKeyInterval = 0.1f;
    auto shot = std::find_if(sequence.cameraCutTrack.clips.begin(), sequence.cameraCutTrack.clips.end(),
                             [&](const auto& clip) { return clip.id == flightShot_; });
    if (flight_ == FlightState::Off || shot == sequence.cameraCutTrack.clips.end()) {
        flight_ = FlightState::Off;
        return false;
    }
    if (flight_ == FlightState::Armed) {
        if (!startRequested)
            return false;
        cursor_.Seek(shot->startTimeSeconds);
        SEQUENCER::SequencePlaybackOptions options;
        options.holdAtEnd = true;
        cursor_.Play(options);
        previewEnabled_ = true;
        flight_ = FlightState::Recording;
        flightLastKey_ = -kKeyInterval;
    }
    const float local = ShotLocalTime(*shot);
    const bool ended = !cursor_.IsPlaying() || local >= shot->durationSeconds;
    if (!ended && local - flightLastKey_ < kKeyInterval)
        return false;
    shot->camera.useScenePose = false;
    auto pose = CaptureShotPoseFor(scene, sequence, *shot, directorViewPanel_.GetViewCamera(), local);
    // 画角は镜头のまま。前の键からこの時刻までにあった键は、録り直した区間なので消す。
    SEQUENCER::ShotPose current;
    if (!SEQUENCER::EvaluateSequenceShotPose(sequence, previewBindings_, *shot, scene.GetWorld(),
                                             scene.GetCameraDirector(), pose.time, current)) {
        flight_ = FlightState::Off;
        return false;
    }
    pose.fov = current.fov;
    std::erase_if(shot->camera.keys, [&](const SEQUENCER::ShotPose& key) {
        return !key.reference.IsValid() && key.time > flightLastKey_ + shot->sourceOffsetSeconds &&
               key.time <= pose.time;
    });
    // 録画は共有端点を切り離さず、途中の姿勢だけを書き換える。
    if (!SharedPoseAtCursor(*shot))
        SEQUENCER::SetShotPose(shot->camera, pose, true);
    flightLastKey_ = local;
    previewChanged_ = true;
    if (ended) {
        flight_ = FlightState::Off;
        cursor_.Pause();
    }
    return true;
}

bool CinematicsWorkspaceController::DrawRailOverlay(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                    SEQUENCER::CameraCutClip& shot, const EditorViewportRect& viewport,
                                                    bool& interacting) {
    auto& rail = shot.camera.rail;
    if (rail.points.empty())
        return false;
    const auto& observer = directorViewPanel_.GetViewCamera();
    const ImVec2 origin{viewport.x, viewport.y}, size{viewport.width, viewport.height};
    auto* draw = ImGui::GetWindowDrawList();
    SceneObjectId anchorId;
    const auto* anchor = previewBindings_.Resolve(sequence.bindings, shot.camera.anchor, anchorId)
                             ? scene.GetWorld().FindObject(anchorId)
                             : nullptr;
    const MATH::Mat4 toWorld = anchor ? anchor->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
    const MATH::Mat4 toLocal = MATH::Inverse(toWorld);
    const auto world = [&](const MATH::Vec3& p) {
        const auto v = toWorld.TransformPoint({p.x, p.y, p.z, 1.0f});
        return MATH::Vec3{v.x, v.y, v.z};
    };
    const auto project = [&](const MATH::Vec3& point, ImVec2& screen) {
        return DIRECTOR_VIEW::ProjectWorldToViewport(observer, point, origin, size, screen);
    };
    const ImU32 color = IM_COL32(255, 160, 90, 230);
    ImVec2 last;
    bool lastValid = false;
    for (int i = 0; i <= 96; ++i) {
        ImVec2 screen;
        const bool valid = project(world(SEQUENCER::EvaluateShotRail(rail, i / 96.0f)), screen);
        if (valid && lastValid)
            draw->AddLine(last, screen, color, 2.0f);
        last = screen;
        lastValid = valid;
    }
    const auto mouse = ImGui::GetIO().MousePos;
    for (size_t i = 0; i < rail.points.size(); ++i) {
        ImVec2 screen;
        if (!project(world(rail.points[i]), screen))
            continue;
        const bool selectedPoint = selectedRailPoint_ == static_cast<int>(i);
        draw->AddCircleFilled(screen, selectedPoint ? 7.0f : 5.0f,
                              selectedPoint ? IM_COL32(255, 220, 120, 255) : color);
        char label[16];
        std::snprintf(label, sizeof(label), "%d", static_cast<int>(i + 1));
        draw->AddText({screen.x + 8, screen.y - 7}, color, label);
        if (!interacting && ImGui::IsWindowHovered() && std::abs(mouse.x - screen.x) < 10 &&
            std::abs(mouse.y - screen.y) < 10 && ImGui::IsMouseClicked(0)) {
            selectedRailPoint_ = static_cast<int>(i);
            interacting = true;
        }
    }
    if (selectedRailPoint_ < 0 || selectedRailPoint_ >= static_cast<int>(rail.points.size()))
        return false;
    TransformData transform;
    transform.position = world(rail.points[static_cast<size_t>(selectedRailPoint_)]);
    transform.scale = {1, 1, 1};
    EditorTransformGizmoState state;
    state.operation = EditorTransformGizmoOperation::Translate;
    state.snapEnabled = ImGui::GetIO().KeyCtrl;
    const auto edit = stageGizmo_.DrawTransform(transform, observer, state, viewport);
    interacting |= edit.interacting;
    if (!edit.changed)
        return false;
    const auto local =
        toLocal.TransformPoint({edit.transform.position.x, edit.transform.position.y, edit.transform.position.z, 1.0f});
    rail.points[static_cast<size_t>(selectedRailPoint_)] = {local.x, local.y, local.z};
    previewChanged_ = true;
    return true;
}

bool CinematicsWorkspaceController::DrawShotOverlays(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                     const EditorViewportRect& viewport, bool enabled) {
    if (viewport.width < 1 || viewport.height < 1)
        return false;
    const auto& observer = directorViewPanel_.GetViewCamera();
    const ImVec2 origin{viewport.x, viewport.y}, size{viewport.width, viewport.height};
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const auto project = [&](MATH::Vec3 point, ImVec2& screen) {
        return DIRECTOR_VIEW::ProjectWorldToViewport(observer, point, origin, size, screen);
    };
    const auto line = [&](MATH::Vec3 a, MATH::Vec3 b, ImU32 color) {
        ImVec2 pa, pb;
        if (project(a, pa) && project(b, pb))
            draw->AddLine(pa, pb, color, 1.4f);
    };
    uint64_t clicked = 0;
    bool changed = false, interacting = false;
    auto* selected = SelectedShot(sequence);
    if (pilot_ && selected != nullptr) {
        // 驾驶中は镜头の画（16:9、縦の画角は视图と同じ）の枠を示し、外を暗くする。
        const float frameWidth = (std::min)(size.x, size.y * 16.0f / 9.0f);
        const float frameHeight = frameWidth * 9.0f / 16.0f;
        const ImVec2 low{origin.x + (size.x - frameWidth) * 0.5f, origin.y + (size.y - frameHeight) * 0.5f};
        const ImVec2 high{low.x + frameWidth, low.y + frameHeight};
        const ImU32 shade = IM_COL32(0, 0, 0, 110);
        draw->AddRectFilled(origin, {low.x, origin.y + size.y}, shade);
        draw->AddRectFilled({high.x, origin.y}, {origin.x + size.x, origin.y + size.y}, shade);
        draw->AddRectFilled({low.x, origin.y}, {high.x, low.y}, shade);
        draw->AddRectFilled({low.x, high.y}, {high.x, origin.y + size.y}, shade);
        draw->AddRect(low, high, IM_COL32(255, 204, 104, 200), 0.0f, 0, 1.5f);
        draw->AddText({low.x + 8, low.y + 6}, IM_COL32(255, 204, 104, 230), selected->camera.name.c_str());
    }
    if (selected && enabled && !pilot_ && !SharedPoseAtCursor(*selected)) {
        changed |= DrawRailOverlay(scene, sequence, *selected, viewport, interacting);
        const bool railPoint =
            selectedRailPoint_ >= 0 && selectedRailPoint_ < static_cast<int>(selected->camera.rail.points.size());
        if (!railPoint) {
            const float local = ShotLocalTime(*selected);
            const Camera3D camera = ResolveShotCamera(scene, sequence, *selected, local, false);
            const bool bound = selected->camera.lookAt.IsValid();
            const MATH::Quat rotation = CameraRotation(camera);
            TransformData transform;
            transform.scale = {1, 1, 1};
            transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(rotation);
            // 注視点は相手（と偏移）か、向きの先の環繞の中心。
            transform.position = stageTool_ == StageTool::Aim ? camera.GetTarget() : camera.GetPosition();
            if (stageTool_ == StageTool::Aim)
                line(camera.GetPosition(), transform.position, IM_COL32(255, 204, 104, 200));
            EditorTransformGizmoState state;
            state.operation = stageTool_ == StageTool::Rotate ? EditorTransformGizmoOperation::Rotate
                                                              : EditorTransformGizmoOperation::Translate;
            state.snapEnabled = ImGui::GetIO().KeyCtrl;
            const auto edit = stageGizmo_.DrawTransform(transform, observer, state, viewport);
            interacting |= edit.interacting;
            if (edit.changed) {
                const auto current = CaptureShotPoseFor(scene, sequence, *selected, camera, local);
                if (stageTool_ == StageTool::Rotate) {
                    const auto pose = CaptureShotPoseFor(
                        scene, sequence, *selected, CameraFromPose(camera, camera.GetPosition(), edit.rotation), local);
                    WriteShotPose(sequence, *selected, pose, kPoseRotation);
                } else if (stageTool_ == StageTool::Move) {
                    const auto pose = CaptureShotPoseFor(
                        scene, sequence, *selected, CameraFromPose(camera, edit.transform.position, rotation), local);
                    WriteShotPose(sequence, *selected, pose, kPosePosition);
                } else if (bound) {
                    // 相手に結んだ镜头は注視の偏移を動かす。
                    selected->camera.targetOffset =
                        selected->camera.targetOffset + edit.transform.position - camera.GetTarget();
                    previewChanged_ = true;
                } else {
                    // 注視点の方へ向け直し（傾きは保つ）、環繞の中心までの距離も合わせる。
                    const MATH::Vec3 forward = edit.transform.position - camera.GetPosition();
                    if (MATH::Length(forward) > 1.0e-4f) {
                        selected->camera.pivotDistance = MATH::Length(forward);
                        Camera3D aimed = camera;
                        aimed.SetLookAt(camera.GetPosition(), edit.transform.position, camera.GetUp());
                        auto pose = CaptureShotPoseFor(scene, sequence, *selected, aimed, local);
                        pose.rotation =
                            SEQUENCER::ShotRotationLookingAlong(SEQUENCER::ShotPoseForward(pose), current.rotation);
                        WriteShotPose(sequence, *selected, pose, kPoseRotation);
                    }
                }
                cursor_.Pause();
                changed = true;
            }
        }
    }
    if (enabled && !pilot_ && selectionKind_ == SelectionKind::Object)
        changed |= DrawObjectTrackGizmo(scene, sequence, viewport, interacting);
    for (const auto& shot : sequence.cameraCutTrack.clips) {
        const bool isSelected = selectionKind_ == SelectionKind::Shot && selectedId_ == shot.id;
        const float local = ShotLocalTime(shot);
        const auto camera = ResolveShotCamera(scene, sequence, shot, local);
        const auto basis = MATH::BuildViewBasisRH(camera.GetTarget() - camera.GetPosition(), camera.GetUp());
        const ImU32 color = isSelected ? IM_COL32(255, 204, 104, 255) : IM_COL32(94, 199, 222, 220);
        const auto position = camera.GetPosition(), front = position + basis.forward * 2;
        const float h = std::tan(camera.GetFovYRad() * 0.5f) * 2, w = h * 16 / 9;
        const MATH::Vec3 corners[]{front - basis.right * w - basis.up * h, front + basis.right * w - basis.up * h,
                                   front + basis.right * w + basis.up * h, front - basis.right * w + basis.up * h};
        if (!(isSelected && pilot_)) {
            for (int i = 0; i < 4; ++i) {
                line(position, corners[i], color);
                line(corners[i], corners[(i + 1) % 4], color);
            }
            // 上向きの印（傾きが分かるように）。
            line((corners[2] + corners[3]) * 0.5f, (corners[2] + corners[3]) * 0.5f + basis.up * (h * 0.4f), color);
        }
        if (isSelected)
            line(position, camera.GetTarget(), IM_COL32(255, 204, 104, 110));
        ImVec2 screen;
        if (project(position, screen) && !(isSelected && pilot_)) {
            draw->AddCircleFilled(screen, isSelected ? 5.0f : 4.0f, color);
            draw->AddText({screen.x + 8, screen.y - 6}, color, shot.camera.name.c_str());
            const auto mouse = ImGui::GetIO().MousePos;
            if (enabled && !interacting && !ImGui::GetIO().KeyAlt && ImGui::IsWindowHovered() &&
                std::abs(mouse.x - screen.x) < 12 && std::abs(mouse.y - screen.y) < 12 && ImGui::IsMouseClicked(0))
                clicked = shot.id;
        }
        if (showShotPaths_) {
            auto last = ResolveShotCamera(scene, sequence, shot, 0).GetPosition();
            for (int i = 1; i <= 48; ++i) {
                const auto next = ResolveShotCamera(scene, sequence, shot, shot.durationSeconds * i / 48).GetPosition();
                line(last, next, color);
                last = next;
            }
            // 姿勢键の位置。
            for (const auto& key : shot.camera.keys) {
                ImVec2 keyScreen;
                if (shot.camera.timing.keys.empty() && key.time >= shot.sourceOffsetSeconds &&
                    key.time <= shot.sourceOffsetSeconds + shot.durationSeconds &&
                    project(ResolveShotCamera(scene, sequence, shot, key.time - shot.sourceOffsetSeconds, false)
                                .GetPosition(),
                            keyScreen))
                    draw->AddRectFilled({keyScreen.x - 3, keyScreen.y - 3}, {keyScreen.x + 3, keyScreen.y + 3}, color);
            }
        }
    }
    draw->PopClipRect();
    if (clicked && clicked != selectedId_) {
        selectedId_ = clicked;
        selectionKind_ = SelectionKind::Shot;
        selectedRailPoint_ = -1;
        curveView_ = {};
    }
    return changed;
}
} // namespace HIKARI::EDITOR
