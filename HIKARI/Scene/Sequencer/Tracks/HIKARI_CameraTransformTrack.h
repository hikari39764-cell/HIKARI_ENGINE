#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {

    struct CameraTransformKeyframe {
        uint64_t id = 0;
        float timeSeconds = 0.0f;
        MATH::Vec3 position{};
        MATH::Vec3 rotationEulerDeg{};
        SequenceInterpolationMode interpolation =
            SequenceInterpolationMode::Smooth;
    };

    struct CameraTransformChannel {
        SequenceBindingId cameraBindingId{};
        std::vector<CameraTransformKeyframe> keyframes{};
    };

    struct CameraTransformTrack {
        SequenceTrackId id{ 2 };
        bool enabled = true;
        std::vector<CameraTransformChannel> channels{};
    };

    struct CameraTransformTrackEvaluation {
        SequenceBindingId cameraBindingId{};
        MATH::Vec3 position{};
        MATH::Quat rotation = MATH::Quat::Identity();
        bool valid = false;
    };

    void NormalizeCameraTransformTrack(CameraTransformTrack& track);
    uint64_t AllocateCameraTransformKeyframeId(
        const CameraTransformTrack& track);
    CameraTransformChannel* FindCameraTransformChannel(
        CameraTransformTrack& track,
        SequenceBindingId bindingId) noexcept;
    const CameraTransformChannel* FindCameraTransformChannel(
        const CameraTransformTrack& track,
        SequenceBindingId bindingId) noexcept;
    CameraTransformKeyframe* FindCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept;
    bool MoveCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId,
        float timeSeconds);
    uint64_t SetCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds,
        const MATH::Vec3& position,
        const MATH::Vec3& rotationEulerDeg);
    float GetCameraTransformTrackContentEnd(
        const CameraTransformTrack& track) noexcept;
    CameraTransformTrackEvaluation EvaluateCameraTransformTrack(
        const CameraTransformTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds) noexcept;

} // namespace HIKARI::SEQUENCER
