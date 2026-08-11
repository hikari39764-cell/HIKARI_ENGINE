#pragma once

#include <memory>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI {
    class AssetRegistry;
    class Material;

    namespace RENDER3D::MATERIAL {
        struct MaterialExternalValueSnapshot;
    }

    namespace RENDER3D::PARTICLES::INTERNAL {

        struct ResolvedParticleMaterial {
            const Material* material = nullptr;
            bool rejected = false;
        };

        class ParticleMaterialResolver final {
        public:
            ParticleMaterialResolver();
            ~ParticleMaterialResolver();
            ParticleMaterialResolver(const ParticleMaterialResolver&) =
                delete;
            ParticleMaterialResolver& operator=(
                const ParticleMaterialResolver&) = delete;

            void Clear();
            ResolvedParticleMaterial Resolve(
                const ParticleEmitterRenderParameters& render,
                const AssetRegistry* assetRegistry,
                const MATERIAL::MaterialExternalValueSnapshot*
                    externalValues);

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_{};
        };

    } // namespace RENDER3D::PARTICLES::INTERNAL
} // namespace HIKARI
