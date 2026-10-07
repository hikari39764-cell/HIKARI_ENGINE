#pragma once
#include "Core/Math/HIKARI_Math3D.h"
#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
namespace HIKARI::RENDER3D::PARTICLES {
inline constexpr uint32_t kMaximumParticleSpatialEffects = 8;
struct ParticleSpatialEffect {
    uint32_t ownerEmitterIndex = UINT32_MAX;
    MaterialEffectKind kind{};
    float mix = 1;
    MaterialEffectMask mask = MaterialEffectMask::None;
    bool maskInvert = false;
    MATH::Vec4 parameters{}, parameters2{}, maskParameters{};
    MATH::Vec3 origin{}, scale{1, 1, 1};
    MATH::Quat rotation = MATH::Quat::Identity();
};
} // namespace HIKARI::RENDER3D::PARTICLES
