#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace HIKARI::SEQUENCER::INTERNAL {

    inline constexpr float kCameraKeyframeTimeEpsilon = 0.0005f;

    template <typename Track, typename IsKeyframeValid, typename SanitizeKeyframe>
    void NormalizeCameraTrack(
        Track& track,
        uint64_t defaultTrackId,
        IsKeyframeValid isKeyframeValid,
        SanitizeKeyframe sanitizeKeyframe) {

        if (!track.id.IsValid()) {
            track.id = { defaultTrackId };
        }

        auto& channels = track.channels;
        channels.erase(
            std::remove_if(
                channels.begin(),
                channels.end(),
                [](const auto& channel) {
                    return !channel.cameraBindingId.IsValid();
                }),
            channels.end());
        std::sort(
            channels.begin(),
            channels.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.cameraBindingId.value < rhs.cameraBindingId.value;
            });

        for (size_t index = 1; index < channels.size();) {
            if (channels[index - 1].cameraBindingId ==
                    channels[index].cameraBindingId) {
                auto& destination = channels[index - 1].keyframes;
                auto& source = channels[index].keyframes;
                destination.insert(
                    destination.end(),
                    source.begin(),
                    source.end());
                channels.erase(channels.begin() + index);
            } else {
                ++index;
            }
        }

        std::unordered_set<uint64_t> usedIds{};
        uint64_t nextId = 1;
        for (auto& channel : channels) {
            auto& keyframes = channel.keyframes;
            keyframes.erase(
                std::remove_if(
                    keyframes.begin(),
                    keyframes.end(),
                    [&](const auto& keyframe) {
                        return !isKeyframeValid(keyframe);
                    }),
                keyframes.end());
            for (auto& keyframe : keyframes) {
                sanitizeKeyframe(keyframe);
            }
            std::sort(
                keyframes.begin(),
                keyframes.end(),
                [](const auto& lhs, const auto& rhs) {
                    if (lhs.timeSeconds != rhs.timeSeconds) {
                        return lhs.timeSeconds < rhs.timeSeconds;
                    }
                    return lhs.id < rhs.id;
                });
            for (size_t index = 1; index < keyframes.size();) {
                if (std::abs(
                        keyframes[index].timeSeconds -
                        keyframes[index - 1].timeSeconds) <=
                        kCameraKeyframeTimeEpsilon) {
                    keyframes.erase(keyframes.begin() + index - 1);
                } else {
                    ++index;
                }
            }
            for (auto& keyframe : keyframes) {
                if (keyframe.id == 0 ||
                    !usedIds.insert(keyframe.id).second) {
                    while (nextId == 0 || usedIds.contains(nextId)) {
                        ++nextId;
                    }
                    keyframe.id = nextId++;
                    usedIds.insert(keyframe.id);
                }
            }
        }

        channels.erase(
            std::remove_if(
                channels.begin(),
                channels.end(),
                [](const auto& channel) {
                    return channel.keyframes.empty();
                }),
            channels.end());
    }

} // namespace HIKARI::SEQUENCER::INTERNAL
