#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWalk.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <unordered_set>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Workspaces/LookDev/HIKARI_LookDevPlacement.h"
#include "Scene/Camera/HIKARI_CameraVolume.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
// 軌跡を残す長さ [s]、歩く・走る速さ [m/s]、一歩で登れる段差 [m]。
constexpr float kTrailSeconds = 8.0f;
constexpr float kWalkSpeed = 3.0f;
constexpr float kRunSpeed = 6.0f;
constexpr float kStepHeight = 0.6f;

ImU32 ZoneColor(SceneObjectId zone, int alpha) {
    if (!zone.value)
        return IM_COL32(120, 128, 140, alpha);
    // 区域ごとに色相を散らす（黄金角）。
    const float hue = std::fmod(static_cast<float>(zone.value) * 0.618034f, 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.95f, r, g, b);
    return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), alpha);
}

// 区域の名前（区域の外は既定の相机）。
std::string ZoneLabel(const World& world, SceneObjectId zone) {
    if (const GameObject* object = zone.value ? world.FindObject(zone) : nullptr)
        return object->GetName();
    return TEXT::Localize("gc.defaultCameraShort");
}

void SetWorldPose(GameObject& object, const MATH::Vec3& position, float yawDegrees) {
    Transform3D transform = object.GetTransform();
    const MATH::Vec3 current = CAMERA::WorldPosition(object);
    // 親があっても局所の位置へ世界での差を足す（替身は普通は親を持たない）。
    transform.position = transform.position + (position - current);
    transform.rotation = MATH::Quat::FromEulerXYZ(0.0f, yawDegrees * std::numbers::pi_v<float> / 180.0f, 0.0f);
    (void)object.SetLocalTransform(transform);
}
} // namespace

void GameplayCameraWalk::Start(World& world, SceneObjectId subject) {
    GameObject* object = world.FindObject(subject);
    if (object == nullptr)
        return;
    if (subject != subject_)
        take_.clear();
    subject_ = subject;
    origin_ = object->GetTransform();
    const MATH::Vec3 position = CAMERA::WorldPosition(*object);
    footOffset_ = position.y - FindLookDevGroundHeight(position, {subject.value});
    time_ = 0.0f;
    trail_.clear();
    recording_ = replaying_ = scrubbing_ = blending_ = false;
}

void GameplayCameraWalk::Stop(World& world) {
    if (GameObject* object = world.FindObject(subject_); object != nullptr && origin_)
        (void)object->SetLocalTransform(*origin_);
    subject_ = {};
    origin_.reset();
    trail_.clear();
    recording_ = replaying_ = scrubbing_ = blending_ = false;
}

void GameplayCameraWalk::Step(World& world, const Input& input, const MATH::Vec3& cameraForward, float deltaTime) {
    GameObject* object = world.FindObject(subject_);
    if (object == nullptr || !std::isfinite(deltaTime) || deltaTime <= 0.0f)
        return;
    time_ += deltaTime;
    // 時間条を動かしている間はその時刻、再生中は録った時刻の位置へ替身を置く。
    if (scrubbing_ || replaying_) {
        if (replaying_)
            takeTime_ += deltaTime;
        if (const Sample* sample = TakeAt(takeTime_)) {
            yawDegrees_ = sample->yawDegrees;
            SetWorldPose(*object, sample->subject, yawDegrees_);
        }
        if (take_.empty() || takeTime_ >= take_.back().time)
            replaying_ = false;
        return;
    }
    MATH::Vec3 forward{cameraForward.x, 0.0f, cameraForward.z};
    const float length = MATH::Length(forward);
    if (length < 1.0e-4f || (input.forward == 0.0f && input.right == 0.0f))
        return;
    forward = forward * (1.0f / length);
    // 右は前と上の外積（yaw 0 の相机が +Z を見る時、画面の右は -X）。
    const MATH::Vec3 right = MATH::Cross(forward, MATH::Vec3{0.0f, 1.0f, 0.0f});
    MATH::Vec3 move = forward * input.forward + right * input.right;
    move = MATH::Normalize(move) * ((input.run ? kRunSpeed : kWalkSpeed) * deltaTime);
    MATH::Vec3 position = CAMERA::WorldPosition(*object) + move;
    // 足元から一歩分上の高さで、真下の一番高い面に足を置く（段差は登り、落ちる所はそのまま下りる）。
    const float foot = position.y - footOffset_;
    position.y = FindLookDevGroundHeight({position.x, foot + kStepHeight, position.z}, {subject_.value}) + footOffset_;
    yawDegrees_ = std::atan2(move.x, move.z) * 180.0f / std::numbers::pi_v<float>;
    SetWorldPose(*object, position, yawDegrees_);
}

void GameplayCameraWalk::BeginBlend(const Camera3D& from, float seconds) {
    blending_ = std::isfinite(seconds) && seconds > 0.0f;
    blendFrom_ = from;
    blendElapsed_ = 0.0f;
    blendSeconds_ = blending_ ? seconds : 0.0f;
}

Camera3D GameplayCameraWalk::Resolve(const Camera3D& live, float deltaTime) {
    if (scrubbing_) {
        if (const Sample* sample = TakeAt(takeTime_)) {
            Camera3D recorded = live;
            recorded.SetLookAt(sample->eye, sample->target, live.GetUp());
            return recorded;
        }
    }
    if (!blending_)
        return live;
    blendElapsed_ += std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
    const float t = std::clamp(blendElapsed_ / blendSeconds_, 0.0f, 1.0f);
    if (t >= 1.0f) {
        blending_ = false;
        return live;
    }
    // CameraDirector::AdvanceBlend と同じ SmoothStep。
    return CameraDirector::BlendViews(blendFrom_, live, t * t * (3.0f - 2.0f * t));
}

void GameplayCameraWalk::Observe(const World& world, const Camera3D& camera, SceneObjectId zone) {
    const GameObject* object = world.FindObject(subject_);
    if (object == nullptr || scrubbing_ || (!trail_.empty() && trail_.back().time == time_))
        return;
    const Sample sample{time_, CAMERA::WorldPosition(*object), yawDegrees_, camera.GetPosition(), camera.GetTarget(),
                        zone};
    trail_.push_back(sample);
    while (!trail_.empty() && time_ - trail_.front().time > kTrailSeconds)
        trail_.pop_front();
    if (recording_) {
        Sample recorded = sample;
        recorded.time = time_ - recordStart_;
        take_.push_back(recorded);
    }
}

void GameplayCameraWalk::SetRecording(bool recording) {
    if (recording && !recording_) {
        take_.clear();
        recordStart_ = time_;
        replaying_ = scrubbing_ = false;
    }
    recording_ = recording;
}

void GameplayCameraWalk::SetReplaying(bool replaying) {
    replaying_ = replaying && !recording_ && !take_.empty();
    takeTime_ = 0.0f;
    scrubbing_ = false;
}

const GameplayCameraWalk::Sample* GameplayCameraWalk::TakeAt(float time) const {
    if (take_.empty())
        return nullptr;
    const auto found = std::lower_bound(take_.begin(), take_.end(), time,
                                        [](const Sample& sample, float value) { return sample.time < value; });
    return found == take_.end() ? &take_.back() : &*found;
}

void GameplayCameraWalk::DrawOverlay(ImDrawList& draw, const World& world, const Camera3D& view,
                                     const EditorViewportRect& rect, const Camera3D* current) const {
    if (!Active())
        return;
    const ImVec2 origin{rect.x, rect.y}, size{rect.width, rect.height};
    const auto project = [&](const MATH::Vec3& point, ImVec2& out) {
        return DIRECTOR_VIEW::ProjectWorldToViewport(view, point, origin, size, out);
    };
    draw.PushClipRect(origin, {rect.x + rect.width, rect.y + rect.height}, true);
    const auto polyline = [&](const auto& samples, auto pick, ImU32 color, float thickness) {
        ImVec2 previous{};
        bool hasPrevious = false;
        for (const Sample& sample : samples) {
            ImVec2 point;
            const bool visible = project(pick(sample), point);
            if (visible && hasPrevious)
                draw.AddLine(previous, point, color, thickness);
            previous = point;
            hasPrevious = visible;
        }
    };
    // 録った走位（再生・比較の基準）は薄く、最近の軌跡は濃く描く。
    polyline(take_, [](const Sample& s) { return s.subject; }, IM_COL32(255, 170, 80, 90), 1.5f);
    polyline(take_, [](const Sample& s) { return s.eye; }, IM_COL32(80, 220, 235, 90), 1.5f);
    polyline(trail_, [](const Sample& s) { return s.subject; }, IM_COL32(255, 170, 80, 230), 2.5f);
    polyline(trail_, [](const Sample& s) { return s.eye; }, IM_COL32(80, 220, 235, 230), 2.5f);
    // 区域が切り替わった所に印と区域の名前。
    for (size_t index = 1; index < trail_.size(); ++index) {
        if (trail_[index].zone == trail_[index - 1].zone)
            continue;
        ImVec2 point;
        if (!project(trail_[index].subject, point))
            continue;
        const ImU32 color = ZoneColor(trail_[index].zone, 255);
        draw.AddQuadFilled({point.x, point.y - 7}, {point.x + 7, point.y}, {point.x, point.y + 7},
                           {point.x - 7, point.y}, color);
        const std::string name = ZoneLabel(world, trail_[index].zone);
        draw.AddText({point.x + 9, point.y - 8}, color, name.c_str());
    }
    // 今の相机の視錐（2 m 先までの四角錐）。
    if (current != nullptr) {
        const MATH::Vec3 eye = current->GetPosition();
        const MATH::Vec3 forward = MATH::Normalize(current->GetTarget() - eye);
        const MATH::Vec3 side = MATH::Normalize(MATH::Cross(forward, current->GetUp()));
        const MATH::Vec3 up = MATH::Cross(side, forward);
        const float halfHeight = std::tan(current->GetFovYRad() * 0.5f) * 2.0f;
        const float halfWidth = halfHeight * current->GetAspect();
        const MATH::Vec3 center = eye + forward * 2.0f;
        const MATH::Vec3 corners[4] = {
            center + side * halfWidth + up * halfHeight, center - side * halfWidth + up * halfHeight,
            center - side * halfWidth - up * halfHeight, center + side * halfWidth - up * halfHeight};
        ImVec2 apex, screen[4];
        bool visible = project(eye, apex);
        for (int i = 0; i < 4; ++i)
            visible = project(corners[i], screen[i]) && visible;
        if (visible) {
            const ImU32 color = IM_COL32(80, 220, 235, 255);
            for (int i = 0; i < 4; ++i) {
                draw.AddLine(apex, screen[i], color, 1.5f);
                draw.AddLine(screen[i], screen[(i + 1) % 4], color, 1.5f);
            }
        }
    }
    draw.PopClipRect();
}

void GameplayCameraWalk::DrawControls(const World& world) {
    if (!Active())
        return;
    ImGui::TextDisabled("%s", TEXT::Localize("gc.walkKeys").c_str());
    if (IconTextButton(recording_ ? EditorGlyph::Stop : EditorGlyph::Dirty,
                       TEXT::Localize(recording_ ? "gc.stopRecord" : "gc.record").c_str(), "walkRecord"))
        SetRecording(!recording_);
    ImGui::SameLine();
    ImGui::BeginDisabled(take_.empty() || recording_);
    if (IconTextButton(replaying_ ? EditorGlyph::Stop : EditorGlyph::Play,
                       TEXT::Localize(replaying_ ? "gc.stopReplay" : "gc.replay").c_str(), "walkReplay"))
        SetReplaying(!replaying_);
    ImGui::EndDisabled();
    if (take_.empty()) {
        ImGui::TextDisabled("%s", TEXT::Localize("gc.noTake").c_str());
        return;
    }
    // 時間条：区域ごとに色を分け、つまみを動かしている間はその時刻の相机を見る。
    const float duration = (std::max)(take_.back().time, 0.001f);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 bar = ImGui::GetCursorScreenPos();
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    for (size_t index = 0; index + 1 < take_.size(); ++index) {
        const float x0 = bar.x + width * take_[index].time / duration;
        const float x1 = bar.x + width * take_[index + 1].time / duration;
        draw.AddRectFilled({x0, bar.y}, {x1 + 1.0f, bar.y + 6.0f}, ZoneColor(take_[index].zone, 200));
    }
    ImGui::Dummy({width, 8.0f});
    float shown = replaying_ || scrubbing_ ? takeTime_ : duration;
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##walkTime", &shown, 0.0f, duration, "%.2f s"))
        takeTime_ = shown;
    scrubbing_ = ImGui::IsItemActive();
    if (scrubbing_)
        replaying_ = false;
    if (const Sample* sample = TakeAt(takeTime_); sample && (replaying_ || scrubbing_))
        ImGui::TextDisabled("%s", ZoneLabel(world, sample->zone).c_str());
}

} // namespace HIKARI::EDITOR
