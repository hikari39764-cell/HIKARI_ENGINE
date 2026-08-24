#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::ACTION {

    enum class CombatActionPhaseKind : uint8_t {
        Startup,
        Active,
        Recovery,
    };

    enum class CombatHitShapeKind : uint8_t {
        Sphere,
        Capsule,
        Box,
    };

    enum class CombatMotionMode : uint8_t {
        None,
        AuthoredVelocity,
        RootMotion,
        MotionWarp,
    };

    enum class CombatActionDiagnosticSeverity : uint8_t {
        Info,
        Warning,
        Error,
    };

    struct CombatActionDiagnostic {
        CombatActionDiagnosticSeverity severity =
            CombatActionDiagnosticSeverity::Info;
        std::string path{};
        std::string message{};
    };

    struct CombatActionAnimationSegment {
        uint64_t id = 0u;
        std::string clipName{};
        float startTimeSeconds = 0.0f;
        float durationSeconds = 1.0f;
        float sourceStartSeconds = 0.0f;
        float playbackRate = 1.0f;
        float blendInSeconds = 0.08f;
        float blendOutSeconds = 0.08f;
        bool loop = false;
    };

    struct CombatActionPhaseWindow {
        uint64_t id = 0u;
        CombatActionPhaseKind kind = CombatActionPhaseKind::Startup;
        float startTimeSeconds = 0.0f;
        float endTimeSeconds = 0.2f;
    };

    struct CombatActionHitWindow {
        uint64_t id = 0u;
        std::string label{ "Hit" };
        float startTimeSeconds = 0.2f;
        float endTimeSeconds = 0.35f;
        std::string socketName{};
        CombatHitShapeKind shape = CombatHitShapeKind::Capsule;
        MATH::Vec3 localOffset{};
        MATH::Vec3 halfExtents{ 0.35f, 0.35f, 0.65f };
        float radius = 0.35f;
        float halfHeight = 0.65f;
        float damageScale = 1.0f;
        float postureDamageScale = 1.0f;
    };

    struct CombatActionCancelWindow {
        uint64_t id = 0u;
        std::string label{ "Follow Up" };
        float startTimeSeconds = 0.45f;
        float endTimeSeconds = 0.75f;
        std::vector<std::string> allowedTags{};
    };

    struct CombatActionMotionWindow {
        uint64_t id = 0u;
        CombatMotionMode mode = CombatMotionMode::None;
        float startTimeSeconds = 0.0f;
        float endTimeSeconds = 0.0f;
        std::string warpTargetSlot{ "Target" };
        MATH::Vec3 velocity{};
        float translationScale = 1.0f;
        float rotationWeight = 1.0f;
    };

    struct CombatActionDefenseWindow {
        uint64_t id = 0u;
        float startTimeSeconds = 0.0f;
        float endTimeSeconds = 0.0f;
        float damageReduction = 0.0f;
        bool invulnerable = false;
        bool superArmor = false;
    };

    struct CombatActionVfxCue {
        uint64_t id = 0u;
        AssetGuid assetGuid{};
        std::string socketName{};
        float timeSeconds = 0.0f;
        float scale = 1.0f;
    };

    struct CombatActionPresentation {
        AssetGuid sequenceAssetGuid{};
        std::string cameraReactionProfile{};
        std::vector<CombatActionVfxCue> vfxCues{};
    };

    struct CombatActionDefinition {
        std::string actionId{ "Action.New" };
        std::string displayName{ "New Combat Action" };
        float durationSeconds = 1.0f;
        uint32_t displayFramesPerSecond = 30u;
        float staminaCost = 0.0f;
        float cooldownSeconds = 0.0f;
        std::vector<std::string> tags{};
        AssetGuid previewAttackerModelGuid{};
        AssetGuid previewVictimModelGuid{};
        std::vector<CombatActionAnimationSegment> animationSegments{};
        std::vector<CombatActionPhaseWindow> phases{};
        std::vector<CombatActionHitWindow> hitWindows{};
        std::vector<CombatActionCancelWindow> cancelWindows{};
        std::vector<CombatActionMotionWindow> motionWindows{};
        std::vector<CombatActionDefenseWindow> defenseWindows{};
        CombatActionPresentation presentation{};
    };

    struct CombatActionAsset {
        static constexpr uint32_t kFormatVersion = 1u;

        AssetGuid guid{};
        std::string displayName{};
        CombatActionDefinition definition{};
    };

    inline constexpr float kMinimumCombatActionDurationSeconds = 1.0f / 240.0f;
    inline constexpr float kMaximumCombatActionDurationSeconds = 120.0f;
    inline constexpr uint32_t kMinimumCombatActionDisplayFps = 12u;
    inline constexpr uint32_t kMaximumCombatActionDisplayFps = 240u;

    void NormalizeCombatAction(CombatActionDefinition& definition);
    std::vector<CombatActionDiagnostic> ValidateCombatAction(
        const CombatActionDefinition& definition);
    bool HasCombatActionErrors(
        const std::vector<CombatActionDiagnostic>& diagnostics) noexcept;

    uint64_t AllocateCombatActionItemId(
        const CombatActionDefinition& definition) noexcept;

    std::string_view ToString(CombatActionPhaseKind kind) noexcept;
    std::string_view ToString(CombatHitShapeKind kind) noexcept;
    std::string_view ToString(CombatMotionMode mode) noexcept;

    bool LoadCombatActionAsset(
        const std::filesystem::path& path,
        const AssetGuid& authoritativeGuid,
        CombatActionAsset& outAsset,
        std::string* outError = nullptr);
    bool SaveCombatActionAsset(
        const std::filesystem::path& path,
        const CombatActionAsset& asset,
        std::string* outError = nullptr);

} // namespace HIKARI::ACTION
