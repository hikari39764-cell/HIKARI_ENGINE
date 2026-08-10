#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <d3d12.h>

namespace HIKARI {
    class AssetRegistry;
    class Material;

    namespace RENDER3D::MATERIAL {
        struct MaterialExternalValueSnapshot;
    }

    namespace RENDER3D::PARTICLES::INTERNAL {

        struct ResolvedParticleMaterial {
            Material* material = nullptr;
            bool rejected = false;
        };

        class GpuParticleMaterialSet final {
        public:
            GpuParticleMaterialSet();
            ~GpuParticleMaterialSet();

            GpuParticleMaterialSet(
                const GpuParticleMaterialSet&) = delete;
            GpuParticleMaterialSet& operator=(
                const GpuParticleMaterialSet&) = delete;

            bool Initialize(
                ID3D12Device* device,
                uint32_t materialCapacity);
            void Shutdown() noexcept;
            void BeginFrame() noexcept;

            ResolvedParticleMaterial Resolve(
                std::string_view materialAssetGuid,
                const AssetRegistry* assetRegistry,
                const MATERIAL::MaterialExternalValueSnapshot*
                    externalValues);
            bool Upload(
                const ResolvedParticleMaterial& resolved,
                uint32_t frameSlot,
                uint32_t& outMaterialIndex);

            D3D12_GPU_VIRTUAL_ADDRESS GetMaterialGpuAddress(
                uint32_t frameSlot) const noexcept;
            D3D12_GPU_VIRTUAL_ADDRESS GetParameterGpuAddress(
                uint32_t frameSlot) const noexcept;
            D3D12_GPU_VIRTUAL_ADDRESS GetResourceGpuAddress(
                uint32_t frameSlot) const noexcept;

        private:
            struct Impl;
            std::unique_ptr<Impl> impl_{};
        };

    } // namespace RENDER3D::PARTICLES::INTERNAL
} // namespace HIKARI
