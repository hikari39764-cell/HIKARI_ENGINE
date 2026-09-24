#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::VFX {

inline constexpr uint32_t kVfxProgramVersion = 16u;
inline constexpr uint32_t kMaximumVfxProgramTableItems = 1u << 20u;
inline constexpr uint32_t kMaximumVfxProgramEmitters = 1024u;
inline constexpr uint32_t kMaximumVfxProgramInputs = 4096u;
inline constexpr uint32_t kMaximumVfxProgramLinksPerEmitter = 4096u;
inline constexpr uint32_t kMaximumVfxProgramDependencies = 4096u;
inline constexpr uint32_t kMaximumVfxProgramBurstKeys = 1024u;
inline constexpr uint32_t kMaximumVfxProgramSpecialOutputs = 256u;
inline constexpr uint32_t kMaximumVfxProgramStringBytes = 1u << 20u;
inline constexpr uint32_t kInvalidVfxProgramIndex = (std::numeric_limits<uint32_t>::max)();

struct VfxRuntimePropertyLayout {
    uint32_t byteOffset = 0u;
    RENDER3D::PARTICLES::ParticleRuntimePropertyType storageType =
        RENDER3D::PARTICLES::ParticleRuntimePropertyType::Float;
};

inline VfxRuntimePropertyLayout ResolveVfxRuntimePropertyLayout(VfxRuntimeBindingSemantic semantic) noexcept {
    using Overrides = RENDER3D::PARTICLES::ParticleEmitterRuntimeOverrides;
    using Storage = RENDER3D::PARTICLES::ParticleRuntimePropertyType;
    switch (semantic) {
    case VfxRuntimeBindingSemantic::EmitterEnabled:
        return {
            static_cast<uint32_t>(offsetof(Overrides, enabled)),
            Storage::Bool,
        };
    case VfxRuntimeBindingSemantic::SpawnRateMultiplier:
        return {
            static_cast<uint32_t>(offsetof(Overrides, spawnRateMultiplier)),
            Storage::Float,
        };
    case VfxRuntimeBindingSemantic::RadialSpeedMultiplier:
        return {
            static_cast<uint32_t>(offsetof(Overrides, radialSpeedMultiplier)),
            Storage::Float,
        };
    case VfxRuntimeBindingSemantic::ScaleMultiplier:
        return {
            static_cast<uint32_t>(offsetof(Overrides, scaleMultiplier)),
            Storage::Float,
        };
    case VfxRuntimeBindingSemantic::GravityMultiplier:
        return {
            static_cast<uint32_t>(offsetof(Overrides, gravityMultiplier)),
            Storage::Float,
        };
    case VfxRuntimeBindingSemantic::ColorMultiplier:
        return {
            static_cast<uint32_t>(offsetof(Overrides, colorMultiplier)),
            Storage::Float4,
        };
    default: return {};
    }
}

struct VfxProgramInput {
    std::string id{};
    VfxValueType type = VfxValueType::Float;
    VfxValue defaultValue{};
};

struct VfxProgramInputLink {
    uint32_t inputIndex = kInvalidVfxProgramIndex;
    VfxRuntimeBindingSemantic semantic = VfxRuntimeBindingSemantic::EmitterEnabled;
    uint32_t byteOffset = 0u;
    RENDER3D::PARTICLES::ParticleRuntimePropertyType storageType =
        RENDER3D::PARTICLES::ParticleRuntimePropertyType::Bool;
    VfxValueType inputType = VfxValueType::Bool;
    float scale = 1.0f;
    float bias = 0.0f;
};

struct VfxProgramEmitter {
    std::string sourceEmitterId{};
    VfxLayerKind kind = VfxLayerKind::Particles;
    uint32_t matteSourceIndex = UINT32_MAX;
    bool matteLuminance = false;
    bool matteInverted = false;
    bool matteOnly = false;
    bool orientAlongPath = false;
    VfxHistoryData history{};
    VfxAnimationTiming animationTiming{};
    std::vector<VfxProgramLayerCurve> layerCurves;
    // 自身が所有する合成風格と空間変形。材質 VM の予算とは独立する。
    std::vector<MaterialEffectStep> runtimeEffects;
    std::vector<uint32_t> spatialOwners;
    uint32_t emitterIndex = 0u;
    uint32_t parentEmitterIndex = kInvalidVfxProgramIndex;
    VfxTransformData transform{};
    float startDelay = 0.0f;
    float activeDuration = 0.0f;
    VfxAttachmentData attachment{};
    float spawnRatePerSecond = 0.0f;
    std::vector<VfxBurstKeyData> burstTimeline{};
    RENDER3D::PARTICLES::ParticleEmitterRuntimeOverrides authoredOverrides{};
    RENDER3D::PARTICLES::ParticleEmitterSimulationParameters simulation{};
    RENDER3D::PARTICLES::ParticleEmitterRenderParameters render{};
    std::vector<VfxSpecialOutputData> specialOutputs{};
    std::vector<VfxProgramInputLink> inputLinks{};
};

struct VfxProgram {
    uint32_t version = kVfxProgramVersion;
    uint32_t sourceVersion = kVfxSourceVersion;
    uint64_t sourceHash = 0u;
    std::string programId{};
    std::string name{};
    float durationSeconds = 1.0f;
    float maximumParticleLifetimeSeconds = 1.0f;
    bool loop = false;
    std::vector<VfxProgramInput> inputs{};
    std::vector<VfxProgramEmitter> emitters{};
    std::vector<AssetGuid> dependencyGuids{};
};

bool ValidateVfxProgram(const VfxProgram& program, std::string& outError);

} // namespace HIKARI::VFX
