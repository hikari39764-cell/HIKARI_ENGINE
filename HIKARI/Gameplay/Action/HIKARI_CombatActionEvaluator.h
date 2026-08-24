#pragma once

#include <cstdint>
#include <vector>

#include "Assets/Action/HIKARI_CombatActionAsset.h"

namespace HIKARI::ACTION {

    struct CombatActionEvaluation {
        float timeSeconds = 0.0f;
        float normalizedTime = 0.0f;
        CombatActionPhaseKind phase = CombatActionPhaseKind::Startup;
        const CombatActionAnimationSegment* animation = nullptr;
        std::vector<const CombatActionHitWindow*> hitWindows{};
        std::vector<const CombatActionCancelWindow*> cancelWindows{};
        std::vector<const CombatActionMotionWindow*> motionWindows{};
        std::vector<const CombatActionDefenseWindow*> defenseWindows{};
        bool invulnerable = false;
        bool superArmor = false;
        float damageReduction = 0.0f;
    };

    bool IsCombatActionTimeInWindow(
        float timeSeconds,
        float startTimeSeconds,
        float endTimeSeconds) noexcept;

    CombatActionEvaluation EvaluateCombatAction(
        const CombatActionDefinition& definition,
        float timeSeconds);

    std::vector<const CombatActionVfxCue*> CollectCombatActionVfxCues(
        const CombatActionDefinition& definition,
        float previousTimeSeconds,
        float currentTimeSeconds,
        bool looped);

} // namespace HIKARI::ACTION
