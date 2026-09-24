#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Vfx/HIKARI_CombatPresentationFrameSource.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::VFX {

class VfxPlaybackService;
struct VfxProgram;

enum class CombatWorldEffectKind : uint8_t {
    LightImpact = 0,
    HeavyImpact = 1,
    SkillImpact = 2,
    PerfectGuardBurst = 3,
    DodgeAfterimage = 4,
    DangerTelegraph = 5,
    Count,
};

struct CombatWorldEffectRequest {
    RuntimeObjectHandle ownerObject{};
    uint64_t sourceId = 0u;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    MATH::Vec3 direction{0.0f, 1.0f, 0.0f};
    float uniformScale = 1.0f;
    float intensity = 1.0f;
    uint32_t randomSeed = 0u;
    CombatWorldEffectKind kind = CombatWorldEffectKind::LightImpact;
};

struct WeaponTrailSampleRequest {
    RuntimeObjectHandle ownerObject{};
    uint64_t sourceId = 0u;
    MATH::Vec3 bladeBaseWorld{};
    MATH::Vec3 bladeTipWorld{};
    MATH::Vec3 bodyColor{0.08f, 0.52f, 0.92f};
    MATH::Vec3 edgeColor{0.72f, 0.96f, 1.0f};
    float bodyAlpha = 1.0f;
    float edgeAlpha = 1.0f;
    float lifetimeUnscaledSeconds = 0.14f;
    float opacity = 0.72f;
    float emissiveIntensity = 1.35f;
    float flowStrength = 0.65f;
    // Typed HVFX weapon-path profiles override these frame-local
    // renderer controls. Legacy callers retain the neutral defaults.
    float width = 1.0f;
    float distortionStrength = 1.0f;
    float blurScale = 1.0f;
};

enum class GroundTelegraphShape : uint8_t {
    Circle = 0u,
    Sector = 1u,
};

struct GroundTelegraphRequest {
    RuntimeObjectHandle ownerObject{};
    uint64_t sourceId = 0u;
    MATH::Vec3 centerWorld{};
    MATH::Vec3 directionWorld{0.0f, 0.0f, 1.0f};
    MATH::Vec3 fillColor{0.72f, 0.01f, 0.0f};
    MATH::Vec3 outlineColor{1.0f, 0.08f, 0.01f};
    float radius = 1.0f;
    float sectorHalfAngleDegrees = 30.0f;
    float outlineWidthWorld = 0.10f;
    float fillOpacity = 0.22f;
    float outlineOpacity = 0.95f;
    float intensity = 1.0f;
    float lifetimeUnscaledSeconds = 0.12f;
    GroundTelegraphShape shape = GroundTelegraphShape::Circle;
};

struct WorldBarrierRequest {
    RuntimeObjectHandle ownerObject{};
    uint64_t sourceId = 0u;
    MATH::Vec3 centerWorld{};
    MATH::Quat rotationWorld = MATH::Quat::Identity();
    MATH::Vec3 halfExtents{2.0f, 1.5f, 0.20f};
    MATH::Vec3 color{0.10f, 0.48f, 0.82f};
    float opacity = 0.10f;
    float interaction = 0.0f;
    float lifetimeUnscaledSeconds = 0.12f;
};

class CombatPresentationService final {
public:
    CombatPresentationService();
    ~CombatPresentationService();

    bool Prewarm() noexcept;
    bool PlayWorldEffect(VfxPlaybackService& playback, const CombatWorldEffectRequest& request);
    bool SubmitGroundTelegraph(const GroundTelegraphRequest& request, uint64_t frameIndex) noexcept;
    bool SubmitWorldBarrier(const WorldBarrierRequest& request, uint64_t frameIndex) noexcept;
    bool SubmitWeaponTrail(const WeaponTrailSampleRequest& request, uint64_t frameIndex) noexcept;
    bool SubmitWeaponTrail(const WeaponTrailSampleRequest& request, const VfxSpecialOutputData& output,
                           uint64_t frameIndex) noexcept;
    bool SubmitWeaponTrail(const WeaponTrailSampleRequest& request, const VfxProgram& program,
                           uint64_t frameIndex) noexcept;
    bool SubmitWeaponTrail(const WeaponTrailSampleRequest& request, const VfxProgram& program,
                           std::string_view outputId, uint64_t frameIndex) noexcept;
    void Tick(uint64_t frameIndex, float unscaledDeltaSeconds) noexcept;
    void Reset() noexcept;

    const RENDER3D::COMBAT_VFX::CombatPresentationFrameSource& GetFrameSource() const noexcept;

private:
    struct TrailSample;
    struct TrailState;
    struct GroundTelegraphState;
    struct WorldBarrierState;

    std::shared_ptr<const VfxProgram> ResolveProgram(CombatWorldEffectKind kind);
    void BuildWeaponTrailFrame() noexcept;
    void BuildGroundTelegraphFrame() noexcept;
    void BuildWorldBarrierFrame() noexcept;

    std::array<std::shared_ptr<const VfxProgram>, static_cast<size_t>(CombatWorldEffectKind::Count)> programs_{};
    std::vector<TrailState> trails_{};
    std::vector<RENDER3D::COMBAT_VFX::WeaponTrailVertex> weaponTrailVertices_{};
    std::vector<GroundTelegraphState> groundTelegraphs_{};
    std::vector<RENDER3D::COMBAT_VFX::GroundTelegraphVertex> groundTelegraphVertices_{};
    std::vector<WorldBarrierState> worldBarriers_{};
    RENDER3D::COMBAT_VFX::CombatPresentationFrameSource frameSource_{};
    uint64_t revision_ = 0u;
    uint64_t lastTickFrame_ = 0u;
    bool hasTickedFrame_ = false;
};

} // namespace HIKARI::VFX
