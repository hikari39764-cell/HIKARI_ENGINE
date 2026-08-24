#include "Gameplay/Action/HIKARI_CombatActionEvaluator.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ACTION {

    namespace {
        template<class T>
        void CollectActive(
            const std::vector<T>& source,
            float timeSeconds,
            std::vector<const T*>& output) {
            for (const T& value : source) {
                if (IsCombatActionTimeInWindow(
                        timeSeconds,
                        value.startTimeSeconds,
                        value.endTimeSeconds)) {
                    output.push_back(&value);
                }
            }
        }
    }

    bool IsCombatActionTimeInWindow(
        float timeSeconds,
        float startTimeSeconds,
        float endTimeSeconds) noexcept {
        return std::isfinite(timeSeconds) &&
            std::isfinite(startTimeSeconds) &&
            std::isfinite(endTimeSeconds) &&
            timeSeconds >= startTimeSeconds &&
            timeSeconds <= endTimeSeconds;
    }

    CombatActionEvaluation EvaluateCombatAction(
        const CombatActionDefinition& definition,
        float timeSeconds) {
        CombatActionEvaluation result{};
        const float duration = std::isfinite(definition.durationSeconds) &&
            definition.durationSeconds > 0.0f
            ? definition.durationSeconds
            : 1.0f;
        result.timeSeconds = std::clamp(
            std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
            0.0f,
            duration);
        result.normalizedTime = result.timeSeconds / duration;

        for (const CombatActionPhaseWindow& value : definition.phases) {
            if (IsCombatActionTimeInWindow(
                    result.timeSeconds,
                    value.startTimeSeconds,
                    value.endTimeSeconds)) {
                result.phase = value.kind;
            }
        }
        for (const CombatActionAnimationSegment& value :
                definition.animationSegments) {
            if (result.timeSeconds >= value.startTimeSeconds &&
                result.timeSeconds <=
                    value.startTimeSeconds + value.durationSeconds) {
                result.animation = &value;
            }
        }
        CollectActive(definition.hitWindows, result.timeSeconds, result.hitWindows);
        CollectActive(definition.cancelWindows, result.timeSeconds, result.cancelWindows);
        CollectActive(definition.motionWindows, result.timeSeconds, result.motionWindows);
        CollectActive(definition.defenseWindows, result.timeSeconds, result.defenseWindows);
        for (const CombatActionDefenseWindow* value : result.defenseWindows) {
            result.invulnerable |= value->invulnerable;
            result.superArmor |= value->superArmor;
            result.damageReduction = (std::max)(
                result.damageReduction,
                value->damageReduction);
        }
        return result;
    }

    std::vector<const CombatActionVfxCue*> CollectCombatActionVfxCues(
        const CombatActionDefinition& definition,
        float previousTimeSeconds,
        float currentTimeSeconds,
        bool looped) {
        std::vector<const CombatActionVfxCue*> result{};
        if (!std::isfinite(previousTimeSeconds) ||
            !std::isfinite(currentTimeSeconds)) {
            return result;
        }
        for (const CombatActionVfxCue& cue : definition.presentation.vfxCues) {
            const bool crossed = looped
                ? cue.timeSeconds > previousTimeSeconds ||
                    cue.timeSeconds <= currentTimeSeconds
                : cue.timeSeconds > previousTimeSeconds &&
                    cue.timeSeconds <= currentTimeSeconds;
            if (crossed) result.push_back(&cue);
        }
        return result;
    }

} // namespace HIKARI::ACTION
