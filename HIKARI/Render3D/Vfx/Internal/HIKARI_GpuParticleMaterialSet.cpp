#include "Render3D/Vfx/Internal/HIKARI_GpuParticleMaterialSet.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/Program/HIKARI_GpuMaterialProgramBinding.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Vfx/Internal/HIKARI_GpuParticleUploadBuffer.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        using Microsoft::WRL::ComPtr;

        struct CachedParticleMaterial {
            uint64_t assetRegistryInstanceId = 0u;
            uint64_t assetRegistryRevision = 0u;
            std::unique_ptr<Material> material{};
            bool domainRejected = false;
            uint64_t failedProjectionInstanceId = 0u;
            uint64_t failedProjectionRevision = 0u;
            bool projectionRejected = false;
        };
    }

    struct GpuParticleMaterialSet::Impl {
        std::array<
            ComPtr<ID3D12Resource>,
            GFX::kFrameResourceCount> materialUploads{};
        std::array<
            MATERIAL::GpuMaterialData*,
            GFX::kFrameResourceCount> mappedMaterials{};
        std::array<
            ComPtr<ID3D12Resource>,
            GFX::kFrameResourceCount> parameterUploads{};
        std::array<
            MATH::Vec4*,
            GFX::kFrameResourceCount> mappedParameters{};
        std::array<
            ComPtr<ID3D12Resource>,
            GFX::kFrameResourceCount> resourceUploads{};
        std::array<
            uint32_t*,
            GFX::kFrameResourceCount> mappedResources{};
        std::unordered_map<std::string, CachedParticleMaterial> cache{};
        uint32_t capacity = 0u;
        uint32_t uploadCursor = 0u;
    };

    GpuParticleMaterialSet::GpuParticleMaterialSet()
        : impl_(std::make_unique<Impl>()) {}

    GpuParticleMaterialSet::~GpuParticleMaterialSet() {
        Shutdown();
    }

    bool GpuParticleMaterialSet::Initialize(
        ID3D12Device* device,
        uint32_t materialCapacity) {
        Shutdown();
        if (device == nullptr || materialCapacity == 0u) {
            return false;
        }
        impl_->capacity = materialCapacity;
        for (uint32_t index = 0u;
             index < GFX::kFrameResourceCount;
             ++index) {
            if (!CreateGpuParticleMappedUploadBuffer(
                    device,
                    materialCapacity,
                    "GpuParticleRenderer::CreateMaterialUpload",
                    impl_->materialUploads[index],
                    impl_->mappedMaterials[index]) ||
                !CreateGpuParticleMappedUploadBuffer(
                    device,
                    static_cast<size_t>(materialCapacity) *
                        MATERIAL::kGpuMaterialProgramParameterSlotCount,
                    "GpuParticleRenderer::CreateMaterialParameterUpload",
                    impl_->parameterUploads[index],
                    impl_->mappedParameters[index]) ||
                !CreateGpuParticleMappedUploadBuffer(
                    device,
                    static_cast<size_t>(materialCapacity) *
                        MATERIAL::kGpuMaterialProgramResourceSlotCount,
                    "GpuParticleRenderer::CreateMaterialResourceUpload",
                    impl_->resourceUploads[index],
                    impl_->mappedResources[index])) {
                return false;
            }
        }
        return true;
    }

    void GpuParticleMaterialSet::Shutdown() noexcept {
        if (impl_ == nullptr) {
            return;
        }
        for (uint32_t index = 0u;
             index < GFX::kFrameResourceCount;
             ++index) {
            if (impl_->materialUploads[index] != nullptr &&
                impl_->mappedMaterials[index] != nullptr) {
                impl_->materialUploads[index]->Unmap(0u, nullptr);
            }
            impl_->mappedMaterials[index] = nullptr;
            impl_->materialUploads[index].Reset();
            if (impl_->parameterUploads[index] != nullptr &&
                impl_->mappedParameters[index] != nullptr) {
                impl_->parameterUploads[index]->Unmap(0u, nullptr);
            }
            impl_->mappedParameters[index] = nullptr;
            impl_->parameterUploads[index].Reset();
            if (impl_->resourceUploads[index] != nullptr &&
                impl_->mappedResources[index] != nullptr) {
                impl_->resourceUploads[index]->Unmap(0u, nullptr);
            }
            impl_->mappedResources[index] = nullptr;
            impl_->resourceUploads[index].Reset();
        }
        impl_->cache.clear();
        impl_->capacity = 0u;
        impl_->uploadCursor = 0u;
    }

    void GpuParticleMaterialSet::BeginFrame() noexcept {
        if (impl_ != nullptr) {
            impl_->uploadCursor = 0u;
        }
    }

    ResolvedParticleMaterial GpuParticleMaterialSet::Resolve(
        std::string_view materialAssetGuid,
        const Material* runtimeMaterialOverride,
        const AssetRegistry* assetRegistry,
        const MATERIAL::MaterialExternalValueSnapshot* externalValues) {
        ResolvedParticleMaterial result{};
        if (runtimeMaterialOverride != nullptr) {
            const MATERIAL::RuntimeMaterialProgram* program =
                MATERIAL::GetRuntimeMaterialProgramRegistry().Find(
                    runtimeMaterialOverride->GetMaterialProgramId());
            if (program == nullptr ||
                program->ir.domain != MaterialDomain::Particle) {
                HIKARI_LOG_ERROR(
                    "[GpuParticleRenderer][RuntimeMaterialDomainMismatch] "
                    "runtime override is not a registered Particle-domain "
                    "material; using the visible diagnostic magenta fallback.");
                result.rejected = true;
                return result;
            }
            result.material = runtimeMaterialOverride;
            return result;
        }
        if (materialAssetGuid.empty() || assetRegistry == nullptr) {
            return result;
        }
        const auto* descriptor =
            assetRegistry->FindAs<MaterialAssetDescriptor>(
                AssetId{std::string(materialAssetGuid)});
        if (descriptor == nullptr) {
            return result;
        }

        CachedParticleMaterial& cached =
            impl_->cache[std::string(materialAssetGuid)];
        const bool cacheInvalid =
            cached.assetRegistryInstanceId !=
                assetRegistry->GetInstanceId() ||
            cached.assetRegistryRevision != assetRegistry->GetRevision() ||
            (cached.material == nullptr && !cached.domainRejected);
        if (cacheInvalid) {
            cached = {};
            cached.assetRegistryInstanceId = assetRegistry->GetInstanceId();
            cached.assetRegistryRevision = assetRegistry->GetRevision();
            if (!descriptor->compileResult.success ||
                descriptor->compileResult.program.domain !=
                    MaterialDomain::Particle) {
                cached.domainRejected = true;
                const std::string domain = descriptor->compileResult.success
                    ? std::string(ToString(
                        descriptor->compileResult.program.domain))
                    : std::string{"CompileFailed"};
                HIKARI_LOG_ERROR(
                    "[GpuParticleRenderer][MaterialDomainMismatch] "
                    "material " + std::string(materialAssetGuid) +
                    " resolves as " + domain +
                    "; particles require MaterialDomain::Particle. "
                    "Using the visible diagnostic magenta fallback.");
                result.rejected = true;
                return result;
            }
            auto material = std::make_unique<Material>();
            MaterialRuntimeBuilder builder{};
            if (!builder.BuildRuntimeMaterial(
                    descriptor->sourceData,
                    descriptor->compileResult,
                    *assetRegistry,
                    MATERIAL::GetRuntimeMaterialProgramRegistry(),
                    *material,
                    materialAssetGuid)) {
                impl_->cache.erase(std::string(materialAssetGuid));
                HIKARI_LOG_ERROR(
                    "[GpuParticleRenderer][MaterialBuildFailed] "
                    "could not build particle material " +
                    std::string(materialAssetGuid) +
                    "; using the deterministic white fallback.");
                return result;
            }
            cached.material = std::move(material);
        }

        if (cached.domainRejected || cached.material == nullptr) {
            result.rejected = cached.domainRejected;
            return result;
        }
        const MATERIAL::MaterialExternalValueSnapshot emptySnapshot{};
        const auto& snapshot = externalValues != nullptr
            ? *externalValues
            : emptySnapshot;
        const bool repeatedProjectionFailure =
            cached.projectionRejected &&
            cached.failedProjectionInstanceId == snapshot.sourceInstanceId &&
            cached.failedProjectionRevision == snapshot.revision;
        if (!repeatedProjectionFailure) {
            std::string projectionError{};
            (void)MATERIAL::ApplyMaterialExternalValues(
                *cached.material,
                snapshot,
                projectionError);
            cached.projectionRejected = !projectionError.empty();
            cached.failedProjectionInstanceId = snapshot.sourceInstanceId;
            cached.failedProjectionRevision = snapshot.revision;
            if (!projectionError.empty()) {
                HIKARI_LOG_ERROR(
                    "[GpuParticleRenderer][MaterialProjectionFailed] " +
                    projectionError);
            }
        }
        result.material = cached.material.get();
        return result;
    }

    bool GpuParticleMaterialSet::Upload(
        const ResolvedParticleMaterial& resolved,
        uint32_t frameSlot,
        uint32_t& outMaterialIndex) {
        outMaterialIndex = 0u;
        if (frameSlot >= GFX::kFrameResourceCount ||
            impl_->uploadCursor >= impl_->capacity ||
            impl_->mappedMaterials[frameSlot] == nullptr ||
            impl_->mappedParameters[frameSlot] == nullptr ||
            impl_->mappedResources[frameSlot] == nullptr) {
            return false;
        }

        const uint32_t materialIndex = impl_->uploadCursor++;
        MATH::Vec4* parameterTarget = impl_->mappedParameters[frameSlot] +
            static_cast<size_t>(materialIndex) *
                MATERIAL::kGpuMaterialProgramParameterSlotCount;
        uint32_t* resourceTarget = impl_->mappedResources[frameSlot] +
            static_cast<size_t>(materialIndex) *
                MATERIAL::kGpuMaterialProgramResourceSlotCount;
        std::fill_n(
            parameterTarget,
            MATERIAL::kGpuMaterialProgramParameterSlotCount,
            MATH::Vec4{});
        std::fill_n(
            resourceTarget,
            MATERIAL::kGpuMaterialProgramResourceSlotCount,
            MATERIAL::kInvalidTextureDescriptorIndex);

        MATERIAL::GpuMaterialProjection fallbackProjection{};
        if (resolved.rejected) {
            fallbackProjection.baseColor = {1.0f, 0.0f, 1.0f, 1.0f};
        }
        MATERIAL::GpuMaterialData materialData =
            MATERIAL::PackGpuMaterialData(fallbackProjection);
        if (resolved.material != nullptr) {
            const Material& material = *resolved.material;
            const std::span<const MATH::Vec4> parameters =
                material.GetMaterialProgramParameterBlock();
            if (parameters.size() >
                MATERIAL::kGpuMaterialProgramParameterSlotCount) {
                return false;
            }
            std::copy(
                parameters.begin(),
                parameters.end(),
                parameterTarget);
            const std::vector<uint32_t> resources =
                MATERIAL::ResolveMaterialProgramResourceDescriptorIndices(
                    material);
            if (resources.size() >
                MATERIAL::kGpuMaterialProgramResourceSlotCount) {
                return false;
            }
            std::copy(
                resources.begin(),
                resources.end(),
                resourceTarget);

            materialData = MATERIAL::PackGpuMaterialData(
                MATERIAL::BuildRuntimeMaterialGpuProjection(material));
            if (!MATERIAL::ApplyGpuMaterialProgramBinding(
                    materialData,
                    material,
                    MATERIAL::GpuMaterialProgramBlockLocation{
                        materialIndex *
                            MATERIAL::kGpuMaterialProgramParameterSlotCount,
                        materialIndex *
                            MATERIAL::kGpuMaterialProgramResourceSlotCount
                    })) {
                return false;
            }
        }
        impl_->mappedMaterials[frameSlot][materialIndex] = materialData;
        outMaterialIndex = materialIndex;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        GpuParticleMaterialSet::GetMaterialGpuAddress(
            uint32_t frameSlot) const noexcept {
        return frameSlot < GFX::kFrameResourceCount &&
            impl_->materialUploads[frameSlot] != nullptr
            ? impl_->materialUploads[frameSlot]->GetGPUVirtualAddress()
            : 0u;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        GpuParticleMaterialSet::GetParameterGpuAddress(
            uint32_t frameSlot) const noexcept {
        return frameSlot < GFX::kFrameResourceCount &&
            impl_->parameterUploads[frameSlot] != nullptr
            ? impl_->parameterUploads[frameSlot]->GetGPUVirtualAddress()
            : 0u;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
        GpuParticleMaterialSet::GetResourceGpuAddress(
            uint32_t frameSlot) const noexcept {
        return frameSlot < GFX::kFrameResourceCount &&
            impl_->resourceUploads[frameSlot] != nullptr
            ? impl_->resourceUploads[frameSlot]->GetGPUVirtualAddress()
            : 0u;
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
