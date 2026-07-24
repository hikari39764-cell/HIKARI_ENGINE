#include "Scene/HIKARI_CinematicSequence.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_set>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        MATH::Vec3 ExtractAxis(
            const MATH::Mat4& matrix,
            int column) noexcept {

            return {
                matrix.m[column][0],
                matrix.m[column][1],
                matrix.m[column][2]
            };
        }

        MATH::Vec3 SafeUp(
            const MATH::Vec3& forward,
            MATH::Vec3 up) noexcept {

            up = MATH::Normalize(up);
            if (MATH::Length(up) <= 0.00001f ||
                std::abs(MATH::Dot(forward, up)) >= 0.999f) {
                up = { 0.0f, 1.0f, 0.0f };
            }
            if (std::abs(MATH::Dot(forward, up)) >= 0.999f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            return up;
        }
    }

    void NormalizeCinematicSequence(CinematicSequence& sequence) {
        SEQUENCER::NormalizeSequenceBindings(sequence.bindings);
        SEQUENCER::NormalizeCameraCutTrack(sequence.cameraCutTrack);
        SEQUENCER::NormalizeCameraTransformTrack(
            sequence.cameraTransformTrack);
        SEQUENCER::NormalizeCameraLensTrack(sequence.cameraLensTrack);
        sequence.durationSeconds = std::clamp(
            (std::max)(
                FiniteOr(
                    sequence.durationSeconds,
                    kMinCinematicSequenceDurationSeconds),
                GetCinematicSequenceContentEnd(sequence)),
            kMinCinematicSequenceDurationSeconds,
            kMaxCinematicSequenceDurationSeconds);
    }

    void NormalizeSceneCinematicsSettings(
        SceneCinematicsSettings& settings) {

        if (settings.sequences.empty()) {
            settings.sequences.push_back(CinematicSequence{});
        }

        std::unordered_set<uint64_t> usedIds{};
        CinematicSequenceId nextId = AllocateCinematicSequenceId(settings);
        for (CinematicSequence& sequence : settings.sequences) {
            if (!sequence.id.IsValid() ||
                !usedIds.insert(sequence.id.value).second) {
                while (!usedIds.insert(nextId.value).second) {
                    ++nextId.value;
                    if (nextId.value == 0) {
                        nextId.value = 1;
                    }
                }
                sequence.id = nextId;
                ++nextId.value;
                if (nextId.value == 0) {
                    nextId.value = 1;
                }
            }
            if (sequence.name.empty()) {
                sequence.name = "Sequence " +
                    std::to_string(sequence.id.value);
            }
            NormalizeCinematicSequence(sequence);
        }

        if (FindCinematicSequence(
                settings,
                settings.defaultSequenceId) == nullptr) {
            settings.defaultSequenceId = settings.sequences.front().id;
        }
    }

    CinematicSequenceId AllocateCinematicSequenceId(
        const SceneCinematicsSettings& settings) {

        CinematicSequenceId nextId{ 1 };
        for (const CinematicSequence& sequence : settings.sequences) {
            if (sequence.id.value < nextId.value) {
                continue;
            }
            if (sequence.id.value ==
                (std::numeric_limits<uint64_t>::max)()) {
                nextId.value = 1;
                break;
            }
            nextId.value = sequence.id.value + 1;
        }
        while (FindCinematicSequence(settings, nextId) != nullptr) {
            ++nextId.value;
            if (nextId.value == 0) {
                nextId.value = 1;
            }
        }
        return nextId;
    }

    CinematicSequence* FindCinematicSequence(
        SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept {

        const auto found = std::find_if(
            settings.sequences.begin(),
            settings.sequences.end(),
            [sequenceId](const CinematicSequence& sequence) {
                return sequence.id == sequenceId;
            });
        return found != settings.sequences.end() ? &*found : nullptr;
    }

    const CinematicSequence* FindCinematicSequence(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId) noexcept {

        const auto found = std::find_if(
            settings.sequences.begin(),
            settings.sequences.end(),
            [sequenceId](const CinematicSequence& sequence) {
                return sequence.id == sequenceId;
            });
        return found != settings.sequences.end() ? &*found : nullptr;
    }

    float GetCinematicSequenceContentEnd(
        const CinematicSequence& sequence) noexcept {

        return (std::max)({
            SEQUENCER::GetCameraCutTrackContentEnd(
                sequence.cameraCutTrack),
            SEQUENCER::GetCameraTransformTrackContentEnd(
                sequence.cameraTransformTrack),
            SEQUENCER::GetCameraLensTrackContentEnd(
                sequence.cameraLensTrack)
        });
    }

    namespace {
        CinematicCameraEvaluation EvaluateCinematicCameraTrackInternal(
        const CinematicSequence& sequence,
        float timeSeconds,
        const SEQUENCER::SequenceBindingContext* bindingContext) noexcept {

        CinematicCameraEvaluation result{};
        const SEQUENCER::CameraCutTrackEvaluation trackEvaluation =
            SEQUENCER::EvaluateCameraCutTrack(
                sequence.cameraCutTrack,
                timeSeconds);
        if (!trackEvaluation.IsValid()) {
            return result;
        }

        SceneObjectId cameraObjectId{};
        const bool resolved = bindingContext != nullptr
            ? SEQUENCER::ResolveSceneObjectBinding(
                sequence.bindings,
                trackEvaluation.cameraBindingId,
                *bindingContext,
                cameraObjectId)
            : SEQUENCER::ResolveSceneObjectBinding(
                sequence.bindings,
                trackEvaluation.cameraBindingId,
                cameraObjectId);
        if (!resolved) {
            return result;
        }

        result.shotId = trackEvaluation.clipId;
        result.cameraBindingId = trackEvaluation.cameraBindingId;
        result.cameraObjectId = cameraObjectId;
        result.transition = trackEvaluation.transition;
        result.transform = SEQUENCER::EvaluateCameraTransformTrack(
            sequence.cameraTransformTrack,
            trackEvaluation.cameraBindingId,
            timeSeconds);
        result.lens = SEQUENCER::EvaluateCameraLensTrack(
            sequence.cameraLensTrack,
            trackEvaluation.cameraBindingId,
            timeSeconds);
        result.sequenceTimeSeconds = trackEvaluation.sequenceTimeSeconds;
        result.localTimeSeconds = trackEvaluation.localTimeSeconds;
        return result;
        }
    }

    CinematicCameraEvaluation EvaluateCinematicCameraTrack(
        const CinematicSequence& sequence,
        float timeSeconds) noexcept {

        return EvaluateCinematicCameraTrackInternal(
            sequence,
            timeSeconds,
            nullptr);
    }

    CinematicCameraEvaluation EvaluateCinematicCameraTrack(
        const CinematicSequence& sequence,
        float timeSeconds,
        const SEQUENCER::SequenceBindingContext& bindingContext) noexcept {

        return EvaluateCinematicCameraTrackInternal(
            sequence,
            timeSeconds,
            &bindingContext);
    }

    bool BuildEvaluatedCinematicCamera(
        const CinematicCameraEvaluation& evaluation,
        const Camera3D& sourceCamera,
        float aspect,
        Camera3D& outCamera) noexcept {

        if (!evaluation.IsValid()) {
            return false;
        }
        outCamera = sourceCamera;
        const float safeAspect = std::isfinite(aspect) && aspect > 0.0001f
            ? aspect
            : sourceCamera.GetAspect();
        const float fovYRadians = evaluation.lens.valid
            ? std::clamp(
                evaluation.lens.verticalFovDegrees,
                1.0f,
                179.0f) * std::numbers::pi_v<float> / 180.0f
            : sourceCamera.GetFovYRad();
        const float nearClip = evaluation.lens.valid
            ? evaluation.lens.nearClip
            : sourceCamera.GetNearZ();
        const float farClip = evaluation.lens.valid
            ? evaluation.lens.farClip
            : sourceCamera.GetFarZ();
        outCamera.SetPerspective(
            fovYRadians,
            safeAspect,
            nearClip,
            farClip);

        if (evaluation.transform.valid) {
            const MATH::Mat4 rotationMatrix = MATH::Mat4::Rotate(
                evaluation.transform.rotation);
            MATH::Vec3 forward = MATH::Normalize(
                ExtractAxis(rotationMatrix, 2));
            if (MATH::Length(forward) <= 0.00001f) {
                forward = { 0.0f, 0.0f, 1.0f };
            }
            const MATH::Vec3 up = SafeUp(
                forward,
                ExtractAxis(rotationMatrix, 1));
            outCamera.SetLookAt(
                evaluation.transform.position,
                evaluation.transform.position + forward,
                up);
        }
        return true;
    }

} // namespace HIKARI
