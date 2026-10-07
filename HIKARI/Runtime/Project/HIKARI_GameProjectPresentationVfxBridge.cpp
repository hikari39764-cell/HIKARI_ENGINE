#include "Core/HIKARI_TimeService.h"
#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;
namespace {
bool ToCombatEffectKind(GameProjectCombatEffectType source, VFX::CombatWorldEffectKind& outKind) noexcept {
    switch (source) {
    case GameProjectCombatEffectType::LightImpact: outKind = VFX::CombatWorldEffectKind::LightImpact; return true;
    case GameProjectCombatEffectType::HeavyImpact: outKind = VFX::CombatWorldEffectKind::HeavyImpact; return true;
    case GameProjectCombatEffectType::SkillImpact: outKind = VFX::CombatWorldEffectKind::SkillImpact; return true;
    case GameProjectCombatEffectType::PerfectGuardBurst:
        outKind = VFX::CombatWorldEffectKind::PerfectGuardBurst;
        return true;
    case GameProjectCombatEffectType::DodgeAfterimage:
        outKind = VFX::CombatWorldEffectKind::DodgeAfterimage;
        return true;
    case GameProjectCombatEffectType::DangerTelegraph:
        outKind = VFX::CombatWorldEffectKind::DangerTelegraph;
        return true;
    default: return false;
    }
}
} // namespace

PresentationVfxView GameProjectSdkAccess::MakePresentationVfxView(World& world) noexcept {
    try {
        return PresentationVfxView(&world);
    } catch (...) {
        return {};
    }
}

PresentationVfxView WorldView::GetPresentationVfx() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakePresentationVfxView(*world) : PresentationVfxView{};
}

bool PresentationVfxView::PlayWorldEffect(const GameProjectCombatWorldEffectRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u ||
            HasNonZeroReserved(request.reserved, sizeof(request.reserved)) || !IsValidTransform(request.transform) ||
            !IsFiniteNonZeroDirection(request.direction) || !IsFinitePositive(request.scale) ||
            !IsFinitePositive(request.intensity)) {
            return false;
        }

        World* world = static_cast<World*>(world_);
        const RuntimeObjectHandle owner = RuntimeObjectHandle::FromValue(request.ownerObjectHandle);
        if (!owner.IsValid() || world->FindObject(owner) == nullptr) {
            return false;
        }

        VFX::CombatWorldEffectKind kind{};
        if (!ToCombatEffectKind(request.effect, kind)) {
            return false;
        }
        VFX::CombatPresentationService* combat = world->Services().Find<VFX::CombatPresentationService>();
        VFX::VfxPlaybackService* playback = world->Services().Find<VFX::VfxPlaybackService>();
        if (combat == nullptr || playback == nullptr) {
            return false;
        }

        VFX::CombatWorldEffectRequest runtimeRequest{};
        runtimeRequest.ownerObject = owner;
        runtimeRequest.sourceId = request.sourceId;
        runtimeRequest.position = ToRuntime(request.transform.position);
        runtimeRequest.rotation = ToRuntime(request.transform.rotation);
        runtimeRequest.scale = ToRuntime(request.transform.scale);
        runtimeRequest.direction = ToRuntime(request.direction);
        runtimeRequest.uniformScale = request.scale;
        runtimeRequest.intensity = request.intensity;
        runtimeRequest.randomSeed = request.randomSeed;
        runtimeRequest.kind = kind;
        const bool particleAccepted = combat->PlayWorldEffect(*playback, runtimeRequest);
        if (kind != VFX::CombatWorldEffectKind::DodgeAfterimage) {
            return particleAccepted;
        }

        // DodgeAfterimage is a presentation intent, not a particle
        // implementation.  Particles provide only mist/droplets; the
        // engine-private service freezes the owner's current skinned pose
        // and ignores the old particle-box transform offset.
        VFX::CombatAfterimageService* afterimages = world->Services().Find<VFX::CombatAfterimageService>();
        if (afterimages == nullptr) {
            return false;
        }
        const uint64_t frameIndex = TIME::GetFrameContext().frameIndex;
        VFX::CombatAfterimageRequest afterimageRequest{};
        afterimageRequest.ownerObject = owner;
        afterimageRequest.sourceId = request.sourceId;
        afterimageRequest.sequence = static_cast<uint64_t>(request.randomSeed);
        afterimageRequest.randomSeed = request.randomSeed;
        afterimageRequest.frameIndex = frameIndex;
        afterimageRequest.intensity = request.intensity;
        afterimageRequest.uniformScale = request.scale;
        afterimageRequest.silhouetteCount = request.intensity >= 1.35f ? 5u : 4u;
        afterimageRequest.lifetimeUnscaledSeconds = std::clamp(0.24f + request.intensity * 0.055f, 0.22f, 0.36f);
        afterimageRequest.opacity = std::clamp(0.50f + request.intensity * 0.11f, 0.46f, 0.78f);
        afterimageRequest.color = {0.08f, 0.66f, 1.0f};
        const bool silhouetteAccepted = afterimages->Capture(*world, afterimageRequest);
        return particleAccepted && silhouetteAccepted;
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitGroundTelegraph(const GameProjectGroundTelegraphRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u ||
            HasNonZeroReserved(request.reserved, sizeof(request.reserved)) || !IsFinite(request.centerWorld) ||
            !IsFiniteNonZeroDirection(request.directionWorld) || !IsFiniteNonNegative(request.fillColor) ||
            !IsFiniteNonNegative(request.outlineColor) || !IsFinitePositive(request.radius) ||
            !std::isfinite(request.sectorHalfAngleDegrees) || !IsFinitePositive(request.outlineWidthWorld) ||
            !IsFiniteNonNegative(request.fillOpacity) || request.fillOpacity > 1.0f ||
            !IsFiniteNonNegative(request.outlineOpacity) || request.outlineOpacity > 1.0f ||
            !IsFinitePositive(request.intensity) || !IsFinitePositive(request.lifetimeUnscaledSeconds) ||
            (request.shape != GameProjectGroundTelegraphShape::Circle &&
             request.shape != GameProjectGroundTelegraphShape::Sector)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        const RuntimeObjectHandle owner = RuntimeObjectHandle::FromValue(request.ownerObjectHandle);
        if (!owner.IsValid() || world->FindObject(owner) == nullptr) {
            return false;
        }
        VFX::CombatPresentationService* combat = world->Services().Find<VFX::CombatPresentationService>();
        if (combat == nullptr) {
            return false;
        }
        VFX::GroundTelegraphRequest runtimeRequest{};
        runtimeRequest.ownerObject = owner;
        runtimeRequest.sourceId = request.sourceId;
        runtimeRequest.centerWorld = ToRuntime(request.centerWorld);
        runtimeRequest.directionWorld = ToRuntime(request.directionWorld);
        runtimeRequest.fillColor = ToRuntime(request.fillColor);
        runtimeRequest.outlineColor = ToRuntime(request.outlineColor);
        runtimeRequest.radius = request.radius;
        runtimeRequest.sectorHalfAngleDegrees = request.sectorHalfAngleDegrees;
        runtimeRequest.outlineWidthWorld = request.outlineWidthWorld;
        runtimeRequest.fillOpacity = request.fillOpacity;
        runtimeRequest.outlineOpacity = request.outlineOpacity;
        runtimeRequest.intensity = request.intensity;
        runtimeRequest.lifetimeUnscaledSeconds = request.lifetimeUnscaledSeconds;
        runtimeRequest.shape = request.shape == GameProjectGroundTelegraphShape::Sector
                                   ? VFX::GroundTelegraphShape::Sector
                                   : VFX::GroundTelegraphShape::Circle;
        return combat->SubmitGroundTelegraph(runtimeRequest, TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitWorldBarrier(const GameProjectWorldBarrierRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u || request.reserved != 0u ||
            !IsValidTransform(request.transform) || !IsFiniteNonNegative(request.halfExtents) ||
            request.halfExtents.x <= 0.0f || request.halfExtents.y <= 0.0f || request.halfExtents.z <= 0.0f ||
            !IsFiniteNonNegative(request.color) || !IsFiniteNonNegative(request.opacity) || request.opacity > 1.0f ||
            !IsFiniteNonNegative(request.interaction) || request.interaction > 1.0f ||
            !IsFinitePositive(request.lifetimeUnscaledSeconds)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        const RuntimeObjectHandle owner = RuntimeObjectHandle::FromValue(request.ownerObjectHandle);
        if (!owner.IsValid() || world->FindObject(owner) == nullptr) {
            return false;
        }
        VFX::CombatPresentationService* combat = world->Services().Find<VFX::CombatPresentationService>();
        if (combat == nullptr) {
            return false;
        }
        VFX::WorldBarrierRequest runtimeRequest{};
        runtimeRequest.ownerObject = owner;
        runtimeRequest.sourceId = request.sourceId;
        runtimeRequest.centerWorld = ToRuntime(request.transform.position);
        runtimeRequest.rotationWorld = ToRuntime(request.transform.rotation);
        runtimeRequest.halfExtents = {request.halfExtents.x * std::abs(request.transform.scale.x),
                                      request.halfExtents.y * std::abs(request.transform.scale.y),
                                      request.halfExtents.z * std::abs(request.transform.scale.z)};
        runtimeRequest.color = ToRuntime(request.color);
        runtimeRequest.opacity = request.opacity;
        runtimeRequest.interaction = request.interaction;
        runtimeRequest.lifetimeUnscaledSeconds = request.lifetimeUnscaledSeconds;
        return combat->SubmitWorldBarrier(runtimeRequest, TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitPostProfileParameter(
    std::string_view effectId, const GameProjectPostProfileParameterRequest& request) const noexcept {
    try {
        if (!world_ || effectId.empty() || effectId.size() > 128u)
            return false;
        World* world = static_cast<World*>(world_);
        const auto* camera = world->Services().Find<GameplayCameraService>();
        if (!camera || !camera->IsRuntimeCameraActive())
            return false;
        const DirectX::XMFLOAT4 value{request.value.x, request.value.y, request.value.z, request.value.w};
        return POST::PostSystem::SubmitEffectParameter(effectId, request.parameterIndex, request.channelMask, value,
                                                       request.priority, request.sourceId);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitActionPostProfile(std::string_view profileId,
                                                  const GameProjectActionPostFrameRequest& request) const noexcept {
    try {
        World* world = CurrentWorld(world_);
        if (!world)
            return false;
        const auto* play = world->Services().Find<RuntimePlayStateService>();
        const auto* camera = world->Services().Find<GameplayCameraService>();
        auto* reactions = world->Services().Find<VFX::ActionPostReactionService>();
        return play && play->IsActive() && !TIME::IsPaused() && camera && camera->IsRuntimeCameraActive() &&
               reactions && reactions->SubmitPostProfile(profileId, request, TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::ClearActionPost(uint64_t sourceId) const noexcept {
    try {
        World* world = CurrentWorld(world_);
        auto* reactions = world ? world->Services().Find<VFX::ActionPostReactionService>() : nullptr;
        return reactions && reactions->ClearPostProfile(sourceId, TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitActionCinematicGrade(
    const GameProjectActionCinematicGradeRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u || request.reserved != 0u || !std::isfinite(request.weight) ||
            !std::isfinite(request.monochromeStrength) || !std::isfinite(request.chromaticSeparationPixels) ||
            !std::isfinite(request.contrastStrength) || request.weight < 0.0f || request.weight > 1.0f ||
            request.monochromeStrength < 0.0f || request.monochromeStrength > 1.0f ||
            request.chromaticSeparationPixels < 0.0f || request.chromaticSeparationPixels > 24.0f ||
            request.contrastStrength < 0.0f || request.contrastStrength > 2.0f) {
            return false;
        }

        World* world = static_cast<World*>(world_);
        VFX::ActionPostReactionService* reaction = world->Services().Find<VFX::ActionPostReactionService>();
        if (reaction == nullptr) {
            return false;
        }

        VFX::ActionPostReactionService::ActionCinematicGradeRequest runtimeRequest{};
        runtimeRequest.sourceId = request.sourceId;
        runtimeRequest.frameIndex = TIME::GetFrameContext().frameIndex;
        runtimeRequest.priority = request.priority;
        runtimeRequest.weight = request.weight;
        runtimeRequest.monochromeStrength = request.monochromeStrength;
        runtimeRequest.chromaticSeparationPixels = request.chromaticSeparationPixels;
        runtimeRequest.contrastStrength = request.contrastStrength;
        return reaction->SubmitActionCinematicGrade(runtimeRequest);
    } catch (...) {
        return false;
    }
}

bool PresentationVfxView::SubmitActionBoostGrade(const GameProjectActionBoostGradeRequest& request) const noexcept {
    try {
        if (world_ == nullptr || request.sourceId == 0u || request.reserved != 0u || !std::isfinite(request.weight) ||
            !std::isfinite(request.edgeIntensity) || !std::isfinite(request.edgeWidth) ||
            !std::isfinite(request.chromaticSeparationPixels) || request.weight < 0.0f || request.weight > 1.0f ||
            request.edgeIntensity < 0.0f || request.edgeIntensity > 4.0f || request.edgeWidth < 0.01f ||
            request.edgeWidth > 0.80f || request.chromaticSeparationPixels < 0.0f ||
            request.chromaticSeparationPixels > 16.0f) {
            return false;
        }

        World* world = static_cast<World*>(world_);
        VFX::ActionPostReactionService* reaction = world->Services().Find<VFX::ActionPostReactionService>();
        if (reaction == nullptr) {
            return false;
        }

        VFX::ActionPostReactionService::ActionBoostGradeRequest runtimeRequest{};
        runtimeRequest.sourceId = request.sourceId;
        runtimeRequest.frameIndex = TIME::GetFrameContext().frameIndex;
        runtimeRequest.priority = request.priority;
        runtimeRequest.weight = request.weight;
        runtimeRequest.edgeIntensity = request.edgeIntensity;
        runtimeRequest.edgeWidth = request.edgeWidth;
        runtimeRequest.chromaticSeparationPixels = request.chromaticSeparationPixels;
        return reaction->SubmitActionBoostGrade(runtimeRequest);
    } catch (...) {
        return false;
    }
}
} // namespace HIKARI
