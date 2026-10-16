#include "Scene/Sequencer/Drivers/HIKARI_CameraSequenceTrackDriver.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::SEQUENCER {
namespace {
// 参照・アンカー・光学情報の解決をプレビュー、実行、キー編集で一本化する。
struct ShotContext {
    MATH::Mat4 anchor{};
    MATH::Vec3 target{};
    bool hasTarget = false, hasOptics = false;
    Camera3D optics;
    ShotPose base;
    std::vector<ShotPose> resolved;
    bool Resolve(const CinematicSequence& sequence, const SequenceBindingContext& bindings, const CameraCutClip& clip,
                 const World& world, const CameraDirector& director, float aspect) {
        const auto resolveObject = [&](SequenceBindingId binding) -> const GameObject* {
            SceneObjectId id;
            return binding.IsValid() && bindings.Resolve(sequence.bindings, binding, id) ? world.FindObject(id)
                                                                                         : nullptr;
        };
        const auto* anchorObject = resolveObject(clip.camera.anchor);
        const auto* targetObject = resolveObject(clip.camera.lookAt);
        if ((clip.camera.anchor.IsValid() && !anchorObject) || (clip.camera.lookAt.IsValid() && !targetObject))
            return false;
        anchor = anchorObject ? anchorObject->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
        hasTarget = targetObject != nullptr;
        if (targetObject) {
            const auto& m = targetObject->GetTransform().GetWorldMatrix();
            target = {m.m[3][0], m.m[3][1], m.m[3][2]};
        }
        SceneObjectId sourceId;
        if (clip.cameraBindingId.IsValid()) {
            if (!bindings.Resolve(sequence.bindings, clip.cameraBindingId, sourceId))
                return false;
        } else
            sourceId = director.GetBaseCamera();

        hasOptics = sourceId.value && director.TryResolveCameraObject(world, sourceId, aspect, optics);
        base = clip.camera.base;
        if (clip.camera.useScenePose && clip.camera.keys.empty()) {
            if (!hasOptics)
                return false;
            base = CaptureShotPose(optics, 0, &anchor);
        }
        const auto resolvePose = [&](ShotPose& pose) {
            if (!pose.reference.IsValid())
                return true;
            SceneObjectId id;
            Camera3D reference;
            if (!bindings.Resolve(sequence.bindings, pose.reference, id) ||
                !director.TryResolveCameraObject(world, id, aspect, reference))
                return false;
            const auto shared = CaptureShotPose(reference, pose.time, &anchor);
            pose.position = shared.position + pose.position;
            pose.rotation = MATH::MultiplyQ(pose.rotation, shared.rotation);
            pose.fov += shared.fov;
            pose.reference = {};
            return true;
        };
        if (!resolvePose(base))
            return false;
        resolved.clear();
        if (std::any_of(clip.camera.keys.begin(), clip.camera.keys.end(),
                        [](const auto& key) { return key.reference.IsValid(); })) {
            resolved = clip.camera.keys;
            for (auto& key : resolved)
                if (!resolvePose(key))
                    return false;
        }
        return true;
    }
};
} // namespace
bool ResolveSequenceShotCamera(const CinematicSequence& sequence, const SequenceBindingContext& bindings,
                               const CameraCutClip& clip, const World& world, const CameraDirector& director,
                               ShotCamera& out) {
    ShotContext context;
    if (!context.Resolve(sequence, bindings, clip, world, director, 16.f / 9))
        return false;
    out = clip.camera;
    out.base = context.base;
    if (!context.resolved.empty())
        out.keys = std::move(context.resolved);
    return true;
}
bool EvaluateSequenceShotPose(const CinematicSequence& sequence, const SequenceBindingContext& bindings,
                              const CameraCutClip& clip, const World& world, const CameraDirector& director,
                              float materialSeconds, ShotPose& out) {
    ShotContext context;
    if (!context.Resolve(sequence, bindings, clip, world, director, 16.f / 9))
        return false;
    out = EvaluateShotPose(clip.camera, materialSeconds, context.resolved, &context.base);
    return true;
}

bool EvaluateSequenceShotView(const CinematicSequence& sequence, const SequenceBindingContext& bindings,
                              const CameraCutClip& clip, const World& world, const CameraDirector& director,
                              float localSeconds, float aspect, Camera3D& out, bool includeMotion) {
    ShotContext context;
    if (!context.Resolve(sequence, bindings, clip, world, director, aspect))
        return false;
    out = EvaluateShotCamera(clip.camera, localSeconds + clip.sourceOffsetSeconds, aspect, &context.anchor,
                             context.hasTarget ? &context.target : nullptr, context.resolved, &context.base,
                             includeMotion);
    if (context.hasOptics) {
        out.SetDepthOfFieldSettings(context.optics.GetDepthOfFieldSettings());
        out.SetMotionBlurSettings(context.optics.GetMotionBlurSettings());
        if (context.optics.IsOrthographic())
            out.SetOrthographic(context.optics.GetOrthographicHeight(), aspect, out.GetNearZ(), out.GetFarZ());
    }
    return true;
}

bool EvaluateSequenceCameraView(const CinematicSequence& sequence, const SequenceBindingContext& bindings,
                                const World& world, const CameraDirector& director, float seconds, float aspect,
                                CinematicCameraEvaluation& evaluation, Camera3D& out) {
    evaluation = EvaluateCinematicCameraTrack(sequence, seconds, bindings);
    const auto find = [&](uint64_t id) -> const CameraCutClip* {
        for (const auto& clip : sequence.cameraCutTrack.clips)
            if (clip.id == id)
                return &clip;
        return nullptr;
    };
    const auto* shot = find(evaluation.shotId);
    if (!evaluation.IsValid() || !shot ||
        !EvaluateSequenceShotView(sequence, bindings, *shot, world, director, evaluation.localTimeSeconds, aspect, out))
        return false;
    if (evaluation.transition.mode == CameraCutTransitionMode::EaseInOut && evaluation.transition.durationSeconds > 0 &&
        evaluation.localTimeSeconds < evaluation.transition.durationSeconds) {
        const auto previous =
            EvaluateCinematicCameraTrack(sequence, std::nextafter(shot->startTimeSeconds, -1.0f), bindings);
        const auto* prior = find(previous.shotId);
        Camera3D from;
        if (prior && prior != shot &&
            EvaluateSequenceShotView(sequence, bindings, *prior, world, director, previous.localTimeSeconds, aspect,
                                     from)) {
            const float t = std::clamp(evaluation.localTimeSeconds / evaluation.transition.durationSeconds, 0.0f, 1.0f);
            out = CameraDirector::BlendViews(from, out, t * t * (3 - 2 * t));
        }
    }
    return true;
}
} // namespace HIKARI::SEQUENCER
