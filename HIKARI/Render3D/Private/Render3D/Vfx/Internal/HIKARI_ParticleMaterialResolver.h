#pragma once

#include <memory>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"

namespace HIKARI {
class AssetRegistry;
class Material;

namespace RENDER3D::MATERIAL {
struct MaterialExternalValueSnapshot;
}

namespace RENDER3D::PARTICLES::INTERNAL {

MATERIAL::GpuMaterialSourceKey BuildParticleMaterialSourceKey(const Material* material,
                                                              const ParticleEmitterRenderParameters& render,
                                                              uint64_t instanceIdentity, uintptr_t sourceIdentity,
                                                              bool rejected);

struct ResolvedParticleMaterial {
    const Material* material = nullptr;
    bool rejected = false;
};

class ParticleMaterialResolver final {
public:
    ParticleMaterialResolver();
    ~ParticleMaterialResolver();
    ParticleMaterialResolver(const ParticleMaterialResolver&) = delete;
    ParticleMaterialResolver& operator=(const ParticleMaterialResolver&) = delete;

    void Clear();
    ResolvedParticleMaterial Resolve(const ParticleEmitterRenderParameters& render, const AssetRegistry* assetRegistry,
                                     const MATERIAL::MaterialExternalValueSnapshot* externalValues);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace RENDER3D::PARTICLES::INTERNAL
} // namespace HIKARI
