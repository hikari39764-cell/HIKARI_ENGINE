#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Scene/HIKARI_SceneObjectId.h"

struct ImDrawList;

namespace HIKARI {
class World;
}

namespace HIKARI::EDITOR {

struct EditorViewportRect;

// 模拟走动：游戏摄像机が追う物（替身）を WASD で地面に沿って歩かせ、区域が変われば実行時と同じ過渡（定格した前の
// 画面から新しい機位の画面へ SmoothStep で混ぜる、CameraDirector と同じ BlendViews）で切り替え、相机と替身の
// 軌跡と区域の切り替わりを残す。走位を録って同じ走位で再生でき（参数を変えた前後の比較）、時間条で録った時刻の
// 相机を見られる。実行時の状態だけを動かし、場面の文書は変えない（止めると替身を元の位置へ戻す）。
class GameplayCameraWalk {
public:
    struct Input {
        float forward = 0.0f;
        float right = 0.0f;
        bool run = false;
    };
    struct Sample {
        float time = 0.0f;
        MATH::Vec3 subject{};
        float yawDegrees = 0.0f;
        MATH::Vec3 eye{};
        MATH::Vec3 target{};
        SceneObjectId zone{};
    };

    bool Active() const noexcept { return subject_.value != 0; }
    SceneObjectId Subject() const noexcept { return subject_; }
    void Start(World& world, SceneObjectId subject);
    void Stop(World& world);

    // 一帧進める。再生中は録った位置へ置き、そうでなければ入力で歩かせる（cameraForward は今の相机の向き）。
    void Step(World& world, const Input& input, const MATH::Vec3& cameraForward, float deltaTime);
    // 機位が切り替わった：今見えている画面から seconds かけて新しい機位へ混ぜる（0 以下は切り替え）。
    void BeginBlend(const Camera3D& from, float seconds);
    // 過渡中は混ぜた相机、時間条を動かしている間は録った時刻の相机を返す。
    Camera3D Resolve(const Camera3D& live, float deltaTime);
    // この帧に見えた相机と、替身の位置と入っている区域を記録する。
    void Observe(const World& world, const Camera3D& camera, SceneObjectId zone);
    // 録り始めると前の走位を捨てる。再生は録った走位を最初からなぞる（録っている間は再生しない）。
    void SetRecording(bool recording);
    void SetReplaying(bool replaying);

    // 観察视口に、最近の相机と替身の軌跡、区域の切り替わり、相机の視錐、録った走位を描く。
    void DrawOverlay(ImDrawList& draw, const World& world, const Camera3D& view, const EditorViewportRect& rect,
                     const Camera3D* current) const;
    // 録る・再生する・時間条。
    void DrawControls(const World& world);

private:
    const Sample* TakeAt(float time) const;

    SceneObjectId subject_{};
    std::optional<Transform3D> origin_{};
    float footOffset_ = 0.0f;
    float yawDegrees_ = 0.0f;
    float time_ = 0.0f;
    float recordStart_ = 0.0f;
    std::deque<Sample> trail_;
    std::vector<Sample> take_;
    bool recording_ = false;
    bool replaying_ = false;
    bool scrubbing_ = false;
    float takeTime_ = 0.0f;
    Camera3D blendFrom_{};
    float blendElapsed_ = 0.0f;
    float blendSeconds_ = 0.0f;
    bool blending_ = false;
};

} // namespace HIKARI::EDITOR
