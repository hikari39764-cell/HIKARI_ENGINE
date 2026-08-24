#include "Assets/Action/HIKARI_CombatActionAsset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace HIKARI::ACTION {
    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float ClampTime(float value, float duration) noexcept {
            return std::clamp(FiniteOr(value, 0.0f), 0.0f, duration);
        }

        template<class T>
        void NormalizeIds(std::vector<T>& values, uint64_t& nextId) {
            std::unordered_set<uint64_t> used{};
            for (T& value : values) {
                if (value.id != 0u && used.insert(value.id).second) {
                    nextId = (std::max)(nextId, value.id + 1u);
                    continue;
                }
                while (nextId == 0u || !used.insert(nextId).second) {
                    ++nextId;
                }
                value.id = nextId++;
            }
        }

        template<class T>
        void NormalizeWindow(T& value, float duration) noexcept {
            value.startTimeSeconds = ClampTime(
                value.startTimeSeconds, duration);
            value.endTimeSeconds = ClampTime(
                value.endTimeSeconds, duration);
            if (value.endTimeSeconds < value.startTimeSeconds) {
                std::swap(value.startTimeSeconds, value.endTimeSeconds);
            }
        }

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        template<class T>
        void SortByStart(std::vector<T>& values) {
            std::stable_sort(
                values.begin(), values.end(),
                [](const T& lhs, const T& rhs) {
                    if (lhs.startTimeSeconds != rhs.startTimeSeconds) {
                        return lhs.startTimeSeconds < rhs.startTimeSeconds;
                    }
                    return lhs.id < rhs.id;
                });
        }

        const char* ToPhaseText(CombatActionPhaseKind kind) noexcept {
            switch (kind) {
            case CombatActionPhaseKind::Active: return "active";
            case CombatActionPhaseKind::Recovery: return "recovery";
            case CombatActionPhaseKind::Startup:
            default: return "startup";
            }
        }

        const char* ToHitShapeText(CombatHitShapeKind kind) noexcept {
            switch (kind) {
            case CombatHitShapeKind::Sphere: return "sphere";
            case CombatHitShapeKind::Box: return "box";
            case CombatHitShapeKind::Capsule:
            default: return "capsule";
            }
        }

        const char* ToMotionText(CombatMotionMode mode) noexcept {
            switch (mode) {
            case CombatMotionMode::AuthoredVelocity: return "velocity";
            case CombatMotionMode::RootMotion: return "rootMotion";
            case CombatMotionMode::MotionWarp: return "motionWarp";
            case CombatMotionMode::None:
            default: return "none";
            }
        }
    }

    void NormalizeCombatAction(CombatActionDefinition& definition) {
        definition.durationSeconds = std::clamp(
            FiniteOr(definition.durationSeconds, 1.0f),
            kMinimumCombatActionDurationSeconds,
            kMaximumCombatActionDurationSeconds);
        definition.displayFramesPerSecond = std::clamp(
            definition.displayFramesPerSecond,
            kMinimumCombatActionDisplayFps,
            kMaximumCombatActionDisplayFps);
        definition.staminaCost = (std::max)(
            0.0f, FiniteOr(definition.staminaCost, 0.0f));
        definition.cooldownSeconds = (std::max)(
            0.0f, FiniteOr(definition.cooldownSeconds, 0.0f));
        if (definition.actionId.empty()) definition.actionId = "Action.New";
        if (definition.displayName.empty()) {
            definition.displayName = definition.actionId;
        }

        uint64_t nextId = 1u;
        NormalizeIds(definition.animationSegments, nextId);
        NormalizeIds(definition.phases, nextId);
        NormalizeIds(definition.hitWindows, nextId);
        NormalizeIds(definition.cancelWindows, nextId);
        NormalizeIds(definition.motionWindows, nextId);
        NormalizeIds(definition.defenseWindows, nextId);
        NormalizeIds(definition.presentation.vfxCues, nextId);

        for (auto& value : definition.animationSegments) {
            value.startTimeSeconds = ClampTime(value.startTimeSeconds, definition.durationSeconds);
            value.durationSeconds = std::clamp(
                FiniteOr(value.durationSeconds, kMinimumCombatActionDurationSeconds),
                kMinimumCombatActionDurationSeconds,
                definition.durationSeconds - value.startTimeSeconds +
                    kMinimumCombatActionDurationSeconds);
            if (value.startTimeSeconds + value.durationSeconds > definition.durationSeconds) {
                value.durationSeconds = definition.durationSeconds - value.startTimeSeconds;
            }
            value.durationSeconds = (std::max)(
                value.durationSeconds, kMinimumCombatActionDurationSeconds);
            value.sourceStartSeconds = (std::max)(0.0f, FiniteOr(value.sourceStartSeconds, 0.0f));
            value.playbackRate = std::clamp(FiniteOr(value.playbackRate, 1.0f), 0.01f, 8.0f);
            value.blendInSeconds = std::clamp(FiniteOr(value.blendInSeconds, 0.0f), 0.0f, value.durationSeconds);
            value.blendOutSeconds = std::clamp(FiniteOr(value.blendOutSeconds, 0.0f), 0.0f, value.durationSeconds);
        }
        for (auto& value : definition.phases) NormalizeWindow(value, definition.durationSeconds);
        for (auto& value : definition.hitWindows) {
            NormalizeWindow(value, definition.durationSeconds);
            if (value.label.empty()) value.label = "Hit";
            if (!IsFinite(value.localOffset)) value.localOffset = {};
            if (!IsFinite(value.halfExtents)) value.halfExtents = { 0.35f, 0.35f, 0.65f };
            value.halfExtents.x = (std::max)(0.0f, value.halfExtents.x);
            value.halfExtents.y = (std::max)(0.0f, value.halfExtents.y);
            value.halfExtents.z = (std::max)(0.0f, value.halfExtents.z);
            value.radius = (std::max)(0.001f, FiniteOr(value.radius, 0.35f));
            value.halfHeight = (std::max)(value.radius, FiniteOr(value.halfHeight, 0.65f));
            value.damageScale = (std::max)(0.0f, FiniteOr(value.damageScale, 1.0f));
            value.postureDamageScale = (std::max)(0.0f, FiniteOr(value.postureDamageScale, 1.0f));
        }
        for (auto& value : definition.cancelWindows) {
            NormalizeWindow(value, definition.durationSeconds);
            if (value.label.empty()) value.label = "Follow Up";
        }
        for (auto& value : definition.motionWindows) {
            NormalizeWindow(value, definition.durationSeconds);
            if (!IsFinite(value.velocity)) value.velocity = {};
            value.translationScale = std::clamp(FiniteOr(value.translationScale, 1.0f), 0.0f, 8.0f);
            value.rotationWeight = std::clamp(FiniteOr(value.rotationWeight, 1.0f), 0.0f, 1.0f);
            if (value.mode == CombatMotionMode::MotionWarp && value.warpTargetSlot.empty()) {
                value.warpTargetSlot = "Target";
            }
        }
        for (auto& value : definition.defenseWindows) {
            NormalizeWindow(value, definition.durationSeconds);
            value.damageReduction = std::clamp(FiniteOr(value.damageReduction, 0.0f), 0.0f, 1.0f);
        }
        for (auto& value : definition.presentation.vfxCues) {
            value.timeSeconds = ClampTime(value.timeSeconds, definition.durationSeconds);
            value.scale = std::clamp(FiniteOr(value.scale, 1.0f), 0.0f, 100.0f);
        }

        SortByStart(definition.animationSegments);
        SortByStart(definition.phases);
        SortByStart(definition.hitWindows);
        SortByStart(definition.cancelWindows);
        SortByStart(definition.motionWindows);
        SortByStart(definition.defenseWindows);
        std::stable_sort(
            definition.presentation.vfxCues.begin(),
            definition.presentation.vfxCues.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.timeSeconds != rhs.timeSeconds) {
                    return lhs.timeSeconds < rhs.timeSeconds;
                }
                return lhs.id < rhs.id;
            });
    }

    std::vector<CombatActionDiagnostic> ValidateCombatAction(
        const CombatActionDefinition& definition) {
        std::vector<CombatActionDiagnostic> diagnostics{};
        const auto error = [&diagnostics](std::string path, std::string message) {
            diagnostics.push_back({
                CombatActionDiagnosticSeverity::Error,
                std::move(path), std::move(message)
            });
        };
        const auto warning = [&diagnostics](std::string path, std::string message) {
            diagnostics.push_back({
                CombatActionDiagnosticSeverity::Warning,
                std::move(path), std::move(message)
            });
        };
        if (definition.actionId.empty()) error("actionId", "Action id is required");
        if (!std::isfinite(definition.durationSeconds) ||
            definition.durationSeconds < kMinimumCombatActionDurationSeconds ||
            definition.durationSeconds > kMaximumCombatActionDurationSeconds) {
            error("durationSeconds", "Action duration is outside the supported range");
        }
        if (definition.animationSegments.empty()) {
            warning("animationSegments", "Action has no animation segment");
        }
        for (size_t index = 0u; index < definition.animationSegments.size(); ++index) {
            const auto& value = definition.animationSegments[index];
            const std::string path = "animationSegments[" + std::to_string(index) + "]";
            if (value.clipName.empty()) error(path + ".clipName", "Animation clip name is required");
            if (value.startTimeSeconds < 0.0f || value.durationSeconds <= 0.0f ||
                value.startTimeSeconds + value.durationSeconds > definition.durationSeconds + 1.0e-4f) {
                error(path, "Animation segment is outside the action duration");
            }
        }
        const auto validateWindows = [&definition, &error](const auto& values, std::string_view name) {
            for (size_t index = 0u; index < values.size(); ++index) {
                const auto& value = values[index];
                if (!std::isfinite(value.startTimeSeconds) ||
                    !std::isfinite(value.endTimeSeconds) ||
                    value.startTimeSeconds < 0.0f ||
                    value.endTimeSeconds < value.startTimeSeconds ||
                    value.endTimeSeconds > definition.durationSeconds + 1.0e-4f) {
                    error(std::string(name) + "[" + std::to_string(index) + "]",
                        "Window is outside the action duration");
                }
            }
        };
        validateWindows(definition.phases, "phases");
        validateWindows(definition.hitWindows, "hitWindows");
        validateWindows(definition.cancelWindows, "cancelWindows");
        validateWindows(definition.motionWindows, "motionWindows");
        validateWindows(definition.defenseWindows, "defenseWindows");
        if (!definition.presentation.sequenceAssetGuid.IsValid() &&
            definition.presentation.cameraReactionProfile.empty()) {
            diagnostics.push_back({
                CombatActionDiagnosticSeverity::Info,
                "presentation",
                "Action uses no authored camera presentation"
            });
        }
        return diagnostics;
    }

    bool HasCombatActionErrors(
        const std::vector<CombatActionDiagnostic>& diagnostics) noexcept {
        return std::any_of(
            diagnostics.begin(), diagnostics.end(),
            [](const CombatActionDiagnostic& value) {
                return value.severity == CombatActionDiagnosticSeverity::Error;
            });
    }

    uint64_t AllocateCombatActionItemId(
        const CombatActionDefinition& definition) noexcept {
        uint64_t maximum = 0u;
        const auto scan = [&maximum](const auto& values) {
            for (const auto& value : values) maximum = (std::max)(maximum, value.id);
        };
        scan(definition.animationSegments);
        scan(definition.phases);
        scan(definition.hitWindows);
        scan(definition.cancelWindows);
        scan(definition.motionWindows);
        scan(definition.defenseWindows);
        scan(definition.presentation.vfxCues);
        return maximum == (std::numeric_limits<uint64_t>::max)()
            ? 1u : maximum + 1u;
    }

    std::string_view ToString(CombatActionPhaseKind kind) noexcept { return ToPhaseText(kind); }
    std::string_view ToString(CombatHitShapeKind kind) noexcept { return ToHitShapeText(kind); }
    std::string_view ToString(CombatMotionMode mode) noexcept { return ToMotionText(mode); }


} // namespace HIKARI::ACTION
