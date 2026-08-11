#include "Render3D/Vfx/Internal/HIKARI_ParticleMaterialResolver.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        struct CachedParticleMaterial {
            uint64_t assetRegistryInstanceId = 0u;
            uint64_t assetRegistryRevision = 0u;
            std::unique_ptr<Material> material{};
            bool sourceRejected = false;
            bool resourceRejected = false;
            uint64_t failedProjectionInstanceId = 0u;
            uint64_t failedProjectionRevision = 0u;
            bool projectionRejected = false;
        };
    }

    struct ParticleMaterialResolver::Impl {
        std::unordered_map<std::string, CachedParticleMaterial> cache{};
    };

    ParticleMaterialResolver::ParticleMaterialResolver()
        : impl_(std::make_unique<Impl>()) {}

    ParticleMaterialResolver::~ParticleMaterialResolver() = default;

    void ParticleMaterialResolver::Clear() {
        if (impl_ != nullptr) {
            impl_->cache.clear();
        }
    }

    ResolvedParticleMaterial ParticleMaterialResolver::Resolve(
        const ParticleEmitterRenderParameters& render,
        const AssetRegistry* assetRegistry,
        const MATERIAL::MaterialExternalValueSnapshot* externalValues) {
        ResolvedParticleMaterial result{};
        if (assetRegistry == nullptr) {
            return result;
        }

        const bool standardUnlit = render.materialMode ==
            ParticleMaterialMode::StandardUnlit;
        const std::string_view resourceGuid = standardUnlit
            ? std::string_view(render.colorTextureAssetGuid)
            : std::string_view(render.materialAssetGuid);
        if (!standardUnlit && resourceGuid.empty()) {
            return result;
        }

        const std::string cacheKey =
            (standardUnlit ? "standard:" : "custom:") +
            (resourceGuid.empty()
                ? std::string("untextured")
                : std::string(resourceGuid));
        CachedParticleMaterial& cached = impl_->cache[cacheKey];
        const bool cacheInvalid =
            cached.assetRegistryInstanceId !=
                assetRegistry->GetInstanceId() ||
            cached.assetRegistryRevision != assetRegistry->GetRevision() ||
            (cached.material == nullptr && !cached.sourceRejected);
        if (cacheInvalid) {
            cached = {};
            cached.assetRegistryInstanceId = assetRegistry->GetInstanceId();
            cached.assetRegistryRevision = assetRegistry->GetRevision();

            if (standardUnlit) {
                const bool textured = !resourceGuid.empty();
                const TextureAssetDescriptor* textureDescriptor = textured
                    ? assetRegistry->FindAs<TextureAssetDescriptor>(
                        AssetId{ std::string(resourceGuid) })
                    : nullptr;
                if (textured &&
                    (textureDescriptor == nullptr ||
                     textureDescriptor->sourcePath.empty())) {
                    cached.sourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR(
                        "[ParticleGpuDrivenPass][TextureMissing] " +
                        std::string(resourceGuid) +
                        " is not a registered Texture asset.");
                    return result;
                }

                // Direct VFX textures are projected through the canonical
                // standard material runtime. ParticlePS then uses the same
                // compact base-color texture path as ordinary Surface
                // materials instead of invoking the generic node-expression
                // interpreter for every overdrawn particle pixel.
                PbrMaterialAssetData source{};
                source.materialName = textured
                    ? "Built-in VFX Unlit (Textured)"
                    : "Built-in VFX Unlit";
                source.unlit = true;
                source.doubleSided = true;
                source.emissiveStrength = 0.0f;
                if (textured) {
                    source.baseColorTexture.useTexture = true;
                    source.baseColorTexture.textureAssetGuid = AssetGuid{
                        std::string(resourceGuid) };
                }
                auto material = std::make_unique<Material>();
                MaterialRuntimeBuilder builder{};
                if (!builder.BuildRuntimeMaterial(
                        source,
                        *assetRegistry,
                        *material,
                        cacheKey)) {
                    cached.resourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR(
                        "[ParticleGpuDrivenPass]"
                        "[StandardMaterialBuildFailed] " +
                        (resourceGuid.empty()
                            ? std::string("untextured")
                            : std::string(resourceGuid)));
                    return result;
                }
                if (textured) {
                    const RuntimeTextureSlot& textureSlot =
                        material->GetTextureSlot(
                            MaterialTextureUsage::BaseColor);
                    if (!textureSlot.IsActive() ||
                        textureSlot.sourcePath !=
                            textureDescriptor->sourcePath) {
                        cached.resourceRejected = true;
                        result.rejected = true;
                        HIKARI_LOG_ERROR(
                            "[ParticleGpuDrivenPass]"
                            "[StandardTextureResidencyFailed] " +
                            std::string(resourceGuid));
                        return result;
                    }
                }
                material->SetUnlit(true);
                material->SetDoubleSided(true);
                material->SetMaterialAlphaMode(
                    RuntimeMaterialAlphaMode::Blend);
                cached.material = std::move(material);
            } else {
                const auto* descriptor =
                    assetRegistry->FindAs<MaterialAssetDescriptor>(
                        AssetId{ std::string(resourceGuid) });
                if (descriptor == nullptr) {
                    cached.resourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR(
                        "[ParticleGpuDrivenPass][MaterialMissing] " +
                        std::string(resourceGuid));
                    return result;
                }
                if (!descriptor->compileResult.success ||
                    !IsRenderableMaterialAssetKind(
                        descriptor->sourceData.kind)) {
                    cached.sourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR(
                        "[ParticleGpuDrivenPass][MaterialSourceInvalid] " +
                        std::string(resourceGuid) +
                        " is not a compiled renderable Material asset.");
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
                        resourceGuid)) {
                    cached.resourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR(
                        "[ParticleGpuDrivenPass][MaterialBuildFailed] " +
                        std::string(resourceGuid));
                    return result;
                }
                cached.material = std::move(material);
            }
        }

        if (cached.sourceRejected || cached.resourceRejected ||
            cached.material == nullptr) {
            result.rejected = cached.sourceRejected ||
                cached.resourceRejected;
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
            std::string error{};
            (void)MATERIAL::ApplyMaterialExternalValues(
                *cached.material,
                snapshot,
                error);
            cached.projectionRejected = !error.empty();
            cached.failedProjectionInstanceId = snapshot.sourceInstanceId;
            cached.failedProjectionRevision = snapshot.revision;
            if (!error.empty()) {
                HIKARI_LOG_ERROR(
                    "[ParticleGpuDrivenPass][MaterialProjectionFailed] " +
                    error);
            }
        }
        result.material = cached.material.get();
        return result;
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
