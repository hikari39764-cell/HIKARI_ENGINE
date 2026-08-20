#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Scene/Vfx/HIKARI_VfxPlaybackTypes.h"

namespace HIKARI::VFX::PLAYBACK {

    // Pure per-emitter simulation state. VfxPlaybackService owns the
    // lifetime of this state; this header owns only deterministic schedule
    // and attachment evaluation.
    struct EmitterScheduleState {
        float spawnAccumulator = 0.0f;
        bool burstPending = true;
        bool resetSimulation = true;
        size_t nextBurstIndex = 0u;
        bool attachmentCaptured = false;
        VfxWorldTransform attachmentTransform{};
    };

    inline uint32_t AccumulateSpawnCount(
        float& accumulator,
        float spawnRatePerSecond,
        float multiplier,
        double deltaSeconds,
        uint32_t capacity) noexcept {

        if (!std::isfinite(accumulator) || accumulator < 0.0f) {
            accumulator = 0.0f;
        }
        if (capacity == 0u || !std::isfinite(spawnRatePerSecond) ||
            !std::isfinite(multiplier) || !std::isfinite(deltaSeconds) ||
            spawnRatePerSecond <= 0.0f || multiplier <= 0.0f ||
            deltaSeconds <= 0.0) {
            return 0u;
        }

        const double total = static_cast<double>(accumulator) +
            static_cast<double>(spawnRatePerSecond) *
            static_cast<double>(multiplier) *
            static_cast<double>(deltaSeconds);
        if (!std::isfinite(total) || total < 0.0) {
            accumulator = 0.0f;
            return 0u;
        }
        double integral = 0.0;
        accumulator = static_cast<float>(std::modf(total, &integral));
        return integral >= static_cast<double>(capacity)
            ? capacity
            : static_cast<uint32_t>(integral);
    }

    inline VfxWorldTransform ComposeTransforms(
        const VfxWorldTransform& parent,
        const VfxWorldTransform& local) {

        const MATH::Mat4 matrix = MATH::Mat4::TRS(
            parent.position,
            parent.rotation,
            parent.scale) * MATH::Mat4::TRS(
            local.position,
            local.rotation,
            local.scale);
        VfxWorldTransform result{};
        if (!MATH::DecomposeTRS(
                matrix,
                result.position,
                result.rotation,
                result.scale)) {
            result.position = parent.position + local.position;
            result.rotation = parent.rotation;
            result.scale = {
                parent.scale.x * local.scale.x,
                parent.scale.y * local.scale.y,
                parent.scale.z * local.scale.z,
            };
        }
        return result;
    }

    inline VfxWorldTransform ComposeTransformData(
        const VfxWorldTransform& parent,
        const VfxTransformData& local) {

        constexpr float kDegreesToRadians =
            3.14159265358979323846f / 180.0f;
        const MATH::Quat localRotation = MATH::Quat::FromEulerXYZ(
            local.rotationEulerDegrees.x * kDegreesToRadians,
            local.rotationEulerDegrees.y * kDegreesToRadians,
            local.rotationEulerDegrees.z * kDegreesToRadians);
        return ComposeTransforms(
            parent,
            VfxWorldTransform{
                local.position,
                localRotation,
                local.scale });
    }

    inline VfxWorldTransform SelectFollowedTransform(
        const VfxWorldTransform& previous,
        const VfxWorldTransform& current,
        VfxFollowMode follow) noexcept {

        switch (follow) {
        case VfxFollowMode::Position:
            return {
                current.position,
                previous.rotation,
                previous.scale };
        case VfxFollowMode::Rotation:
            return {
                previous.position,
                current.rotation,
                previous.scale };
        case VfxFollowMode::Transform:
            return current;
        case VfxFollowMode::None:
        default:
            return previous;
        }
    }

    inline float EmitterStartDelay(
        const VfxProgram& program,
        const VfxProgramEmitter& emitter) noexcept {

        if (!std::isfinite(emitter.startDelay)) {
            return 0.0f;
        }
        return std::clamp(
            emitter.startDelay,
            0.0f,
            (std::max)(0.0f, program.durationSeconds));
    }

    inline float EmitterActiveEnd(
        const VfxProgram& program,
        const VfxProgramEmitter& emitter) noexcept {

        const float start = EmitterStartDelay(program, emitter);
        const float authored = emitter.activeDuration;
        const float duration = std::isfinite(authored) && authored > 0.0f
            ? authored
            : program.durationSeconds;
        return (std::min)(
            (std::max)(0.0f, program.durationSeconds),
            start + (std::max)(0.0f, duration));
    }

    inline float OverlapSeconds(
        float begin,
        float end,
        float activeBegin,
        float activeEnd) noexcept {

        const float overlapBegin = (std::max)(begin, activeBegin);
        const float overlapEnd = (std::min)(end, activeEnd);
        return (std::max)(0.0f, overlapEnd - overlapBegin);
    }

    inline uint32_t SaturatingAdd(
        uint32_t value,
        uint32_t add,
        uint32_t capacity) noexcept {

        if (value >= capacity) {
            return capacity;
        }
        return add >= capacity - value
            ? capacity
            : value + add;
    }

    inline bool HasOrderedBurstTimeline(
        const VfxProgram& program) noexcept {

        constexpr float epsilon = 1.0e-6f;
        for (const VfxProgramEmitter& emitter : program.emitters) {
            float previousTime = 0.0f;
            bool hasPrevious = false;
            for (const VfxBurstKeyData& key : emitter.burstTimeline) {
                if (!std::isfinite(key.time) ||
                    (hasPrevious && key.time + epsilon < previousTime)) {
                    return false;
                }
                previousTime = key.time;
                hasPrevious = true;
            }
        }
        return true;
    }

    inline bool HasUniqueSpecialOutputIds(
        const VfxProgram& program) noexcept {

        for (size_t emitterIndex = 0u;
             emitterIndex < program.emitters.size();
             ++emitterIndex) {
            const auto& outputs =
                program.emitters[emitterIndex].specialOutputs;
            for (size_t outputIndex = 0u;
                 outputIndex < outputs.size();
                 ++outputIndex) {
                for (size_t laterOutputIndex = outputIndex + 1u;
                     laterOutputIndex < outputs.size();
                     ++laterOutputIndex) {
                    if (outputs[outputIndex].id ==
                        outputs[laterOutputIndex].id) {
                        return false;
                    }
                }
                for (size_t laterEmitterIndex = emitterIndex + 1u;
                     laterEmitterIndex < program.emitters.size();
                     ++laterEmitterIndex) {
                    for (const auto& laterOutput :
                         program.emitters[laterEmitterIndex]
                             .specialOutputs) {
                        if (outputs[outputIndex].id == laterOutput.id) {
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    inline uint32_t ComputeEmitterSpawnCount(
        EmitterScheduleState& state,
        const VfxProgram& program,
        const VfxProgramEmitter& emitter,
        float elapsedSeconds,
        float deltaSeconds,
        float spawnRateMultiplier,
        bool loop,
        bool wrapLoop) noexcept {

        const float programDuration = (std::max)(
            0.0f,
            program.durationSeconds);
        const float activeStart = EmitterStartDelay(program, emitter);
        const float activeEnd = EmitterActiveEnd(program, emitter);
        const uint32_t capacity = emitter.simulation.capacity;
        constexpr float epsilon = 1.0e-6f;
        uint32_t spawnCount = 0u;

        const auto appendBurstRange =
            [&](float rangeBegin,
                float rangeEnd,
                bool includeRangeBegin) {
            if (rangeEnd + epsilon < rangeBegin) {
                return;
            }
            const float activeSeconds = OverlapSeconds(
                rangeBegin,
                rangeEnd,
                activeStart,
                activeEnd);
            spawnCount = SaturatingAdd(
                spawnCount,
                AccumulateSpawnCount(
                    state.spawnAccumulator,
                    emitter.spawnRatePerSecond,
                    spawnRateMultiplier,
                    activeSeconds,
                    capacity),
                capacity);

            if (!emitter.burstTimeline.empty()) {
                while (state.nextBurstIndex <
                        emitter.burstTimeline.size()) {
                    const VfxBurstKeyData& key =
                        emitter.burstTimeline[state.nextBurstIndex];
                    const float keyTime = activeStart +
                        (std::isfinite(key.time)
                            ? (std::max)(0.0f, key.time)
                            : 0.0f);
                    if (keyTime > rangeEnd + epsilon) {
                        break;
                    }
                    ++state.nextBurstIndex;
                    const bool crossed =
                        keyTime >= activeStart - epsilon &&
                        keyTime <= activeEnd + epsilon &&
                        (keyTime > rangeBegin + epsilon ||
                         includeRangeBegin);
                    if (crossed) {
                        spawnCount = SaturatingAdd(
                            spawnCount,
                            key.count,
                            capacity);
                    }
                }
                return;
            }

            if (state.burstPending &&
                activeStart <= activeEnd + epsilon &&
                activeStart <= rangeEnd + epsilon &&
                (activeStart > rangeBegin + epsilon ||
                 includeRangeBegin)) {
                state.burstPending = false;
                spawnCount = SaturatingAdd(
                    spawnCount,
                    emitter.burstCount,
                    capacity);
                if (emitter.burstCount == 0u && spawnCount == 0u &&
                    emitter.spawnRatePerSecond > 0.0f &&
                    spawnRateMultiplier > 0.0f && activeSeconds > 0.0f) {
                    spawnCount = 1u;
                }
            }
        };

        const auto appendScaled =
            [&](uint32_t value, uint64_t repeatCount) {
            if (capacity == 0u || value == 0u || repeatCount == 0u ||
                spawnCount >= capacity) {
                return;
            }
            const uint64_t scaled =
                static_cast<uint64_t>(value) * repeatCount;
            const uint64_t available =
                static_cast<uint64_t>(capacity - spawnCount);
            spawnCount = scaled >= available
                ? capacity
                : spawnCount + static_cast<uint32_t>(scaled);
        };

        const auto resetLoopState = [&]() {
            state.nextBurstIndex = 0u;
            state.burstPending = true;
        };

        const auto appendFullLoops =
            [&](uint64_t loopCount) {
            if (loopCount == 0u || programDuration <= 0.0f) {
                return;
            }
            resetLoopState();
            const float activeSeconds = OverlapSeconds(
                0.0f,
                programDuration,
                activeStart,
                activeEnd);
            spawnCount = SaturatingAdd(
                spawnCount,
                AccumulateSpawnCount(
                    state.spawnAccumulator,
                    emitter.spawnRatePerSecond,
                    spawnRateMultiplier,
                    static_cast<double>(activeSeconds) *
                        static_cast<double>(loopCount),
                    capacity),
                capacity);

            if (!emitter.burstTimeline.empty()) {
                for (const VfxBurstKeyData& key :
                    emitter.burstTimeline) {
                    const float keyTime = activeStart +
                        (std::isfinite(key.time)
                            ? (std::max)(0.0f, key.time)
                            : 0.0f);
                    if (keyTime >= activeStart - epsilon &&
                        keyTime <= activeEnd + epsilon) {
                        appendScaled(key.count, loopCount);
                    }
                }
                state.nextBurstIndex = emitter.burstTimeline.size();
                return;
            }

            if (activeStart <= activeEnd + epsilon) {
                appendScaled(emitter.burstCount, loopCount);
                state.burstPending = false;
                if (emitter.burstCount == 0u &&
                    emitter.spawnRatePerSecond > 0.0f &&
                    spawnRateMultiplier > 0.0f &&
                    activeSeconds > 0.0f) {
                    appendScaled(1u, loopCount);
                }
            }
        };

        double cursor = (std::max)(
            0.0,
            static_cast<double>(elapsedSeconds));
        double remaining = (std::max)(
            0.0,
            static_cast<double>(deltaSeconds));
        bool includeRangeBegin = state.resetSimulation;
        bool endedAtLoopBoundary = false;
        if (wrapLoop && loop && programDuration > 0.0f) {
            if (cursor >= static_cast<double>(programDuration) - epsilon) {
                cursor = 0.0;
                resetLoopState();
                includeRangeBegin = true;
            }

            const double firstSegment = (std::min)(
                remaining,
                (std::max)(
                    0.0,
                    static_cast<double>(programDuration) - cursor));
            if (firstSegment > 0.0) {
                appendBurstRange(
                    static_cast<float>(cursor),
                    static_cast<float>(cursor + firstSegment),
                    includeRangeBegin);
                cursor += firstSegment;
                remaining -= firstSegment;
                includeRangeBegin = false;
                endedAtLoopBoundary = remaining <= epsilon &&
                    cursor >= static_cast<double>(programDuration) -
                        epsilon;
            }

            if (remaining > epsilon) {
                if (cursor >= static_cast<double>(programDuration) -
                    epsilon) {
                    cursor = 0.0;
                    resetLoopState();
                    includeRangeBegin = true;
                }
                const double exactLoopCount =
                    remaining / static_cast<double>(programDuration);
                const uint64_t loopCount = exactLoopCount >=
                        static_cast<double>(
                            (std::numeric_limits<uint64_t>::max)())
                    ? (std::numeric_limits<uint64_t>::max)()
                    : static_cast<uint64_t>(
                        std::floor(exactLoopCount + epsilon));
                if (loopCount > 0u) {
                    const double consumed = (std::min)(
                        remaining,
                        static_cast<double>(programDuration) *
                            static_cast<double>(loopCount));
                    appendFullLoops(loopCount);
                    remaining -= consumed;
                    cursor = 0.0;
                    includeRangeBegin = true;
                    endedAtLoopBoundary = remaining <= epsilon;
                }
                if (remaining > epsilon) {
                    resetLoopState();
                    appendBurstRange(
                        0.0f,
                        static_cast<float>(remaining),
                        true);
                    cursor = remaining;
                    remaining = 0.0;
                    endedAtLoopBoundary = false;
                }
            }
        }
        else if (remaining > 0.0) {
            appendBurstRange(
                static_cast<float>(cursor),
                static_cast<float>(cursor + remaining),
                includeRangeBegin);
            remaining = 0.0;
        }

        if (endedAtLoopBoundary) {
            cursor = 0.0;
            resetLoopState();
        }

        // Seek(target=0) and the first zero-delta frame still need to emit
        // time-zero keys/legacy bursts once. Keep resetSimulation untouched;
        // the owner consumes it after submitting this frame, which prevents
        // a second zero-delta evaluation from duplicating the burst.
        if (remaining <= epsilon && state.resetSimulation &&
            cursor <= epsilon) {
            appendBurstRange(
                static_cast<float>(cursor),
                static_cast<float>(cursor),
                true);
        }
        return spawnCount;
    }

    inline VfxWorldTransform ResolveEmitterTransform(
        const VfxWorldTransform& ownerTransform,
        RuntimeObjectHandle ownerObject,
        const VfxProgramEmitter& emitter,
        const VfxWorldTransform& parent,
        EmitterScheduleState& state,
        const VfxAttachmentResolver& resolver) {

        const VfxAttachmentData& attachment = emitter.attachment;
        VfxWorldTransform target = parent;
        bool targetResolved = true;
        switch (attachment.mode) {
        case VfxAttachmentMode::Owner:
            targetResolved = resolver != nullptr &&
                resolver(ownerObject, {}, target);
            if (!targetResolved) {
                target = ownerTransform;
            }
            break;
        case VfxAttachmentMode::OwnerSocket:
            targetResolved = resolver != nullptr &&
                !attachment.socketName.empty() &&
                resolver(ownerObject, attachment.socketName, target);
            if (!targetResolved) {
                target = parent;
            }
            break;
        case VfxAttachmentMode::World:
        default:
            target = parent;
            break;
        }

        const bool targetCanFollow =
            attachment.mode == VfxAttachmentMode::Owner ||
            attachment.mode == VfxAttachmentMode::OwnerSocket;
        const bool dynamicFollow = targetCanFollow &&
            attachment.follow != VfxFollowMode::None;
        if (!targetCanFollow) {
            return ComposeTransformData(
                ComposeTransformData(target, attachment.offset),
                emitter.transform);
        }

        if (targetResolved) {
            const VfxWorldTransform resolvedWithOffset =
                ComposeTransformData(target, attachment.offset);
            target = state.attachmentCaptured && dynamicFollow
                ? SelectFollowedTransform(
                    state.attachmentTransform,
                    resolvedWithOffset,
                    attachment.follow)
                : resolvedWithOffset;
            state.attachmentTransform = target;
            state.attachmentCaptured = true;
        }
        else if (state.attachmentCaptured) {
            // Keep the last-known-good socket/owner pose during a transient
            // pose/model gap. Do not overwrite it with the parent fallback.
            target = state.attachmentTransform;
        }
        else {
            // A first-frame resolver miss is a non-capturing fallback. The
            // next frame must retry so late model/pose installation can still
            // acquire the requested owner/socket attachment.
            const VfxWorldTransform& fallback =
                attachment.mode == VfxAttachmentMode::Owner
                ? target
                : parent;
            target = ComposeTransformData(fallback, attachment.offset);
        }
        return ComposeTransformData(target, emitter.transform);
    }

} // namespace HIKARI::VFX::PLAYBACK
