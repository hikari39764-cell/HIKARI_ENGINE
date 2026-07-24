#pragma once

#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_CinematicSequencePlayer.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackTypes.h"

namespace HIKARI {

    class World;

    using CinematicPlaybackHandle =
        SEQUENCER::SequencePlaybackHandle;

    class CinematicCameraPlayback {
    public:
        CinematicPlaybackHandle Play(
            const SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId,
            CameraDirector& cameraDirector,
            const CinematicPlaybackOptions& options = {},
            float startTimeSeconds = 0.0f);
        bool Stop(
            CinematicPlaybackHandle handle,
            CameraDirector& cameraDirector);
        bool Pause(CinematicPlaybackHandle handle);
        bool Resume(CinematicPlaybackHandle handle);
        bool Seek(
            const SceneCinematicsSettings& settings,
            CinematicPlaybackHandle handle,
            float timeSeconds);

        void Update(
            const SceneCinematicsSettings& settings,
            CameraDirector& cameraDirector,
            const World& world,
            float aspect,
            float deltaTime);
        void Reset(CameraDirector& cameraDirector);

        bool IsCurrentHandle(CinematicPlaybackHandle handle) const noexcept;
        bool IsPlaying() const noexcept;
        CinematicPlaybackHandle GetCurrentHandle() const noexcept;
        const CinematicSequencePlayer& GetPlayer() const noexcept;

    private:
        void ReleaseCameraOverride(CameraDirector& cameraDirector);
        void AdvanceHandleEpoch();

        CinematicSequencePlayer player_{};
        CinematicPlaybackHandle currentHandle_{};
        CameraOverrideToken cameraOverrideToken_{};
        SceneObjectId overriddenCameraObjectId_{};
        uint64_t overriddenShotId_ = 0;
        uint64_t nextHandleValue_ = 1;
        uint64_t epoch_ = 1;
    };

} // namespace HIKARI
