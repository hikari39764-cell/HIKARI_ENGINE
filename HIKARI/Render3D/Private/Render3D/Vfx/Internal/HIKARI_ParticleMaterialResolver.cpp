#include "Render3D/Vfx/Internal/HIKARI_ParticleMaterialResolver.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Render3D/Material/HIKARI_MaterialTextureUsage.h"
#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
MATERIAL::GpuMaterialSourceKey BuildParticleMaterialSourceKey(const Material* material,
                                                              const ParticleEmitterRenderParameters& render,
                                                              uint64_t instanceIdentity, uintptr_t sourceIdentity,
                                                              bool rejected) {
    MATERIAL::GpuMaterialSourceKey key{};
    const std::string_view resource = !IsCustomParticleMaterial(render.materialMode)
                                          ? std::string_view(render.colorTextureAssetGuid)
                                          : std::string_view(render.materialAssetGuid);
    const auto combine = [](uint64_t seed, uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
    };
    key.stableMaterialKey =
        material ? combine(std::hash<std::string_view>{}(resource), reinterpret_cast<uintptr_t>(material))
                 : (rejected ? 2u : 1u);
    // スロット同一性はstableMaterialKeyで決まる。動的な値だけインスタンス専有にする。
    if (!render.animatedParameters.empty())
        key.stableMaterialKey = combine(key.stableMaterialKey, instanceIdentity);
    key.modelIdentity = sourceIdentity;
    key.materialRevision = material ? material->GetRevision() : 0u;
    return key;
}

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
    bool embedded = false;
    std::weak_ptr<const MaterialAssetDescriptor> effectSource{};
};
} // namespace

struct ParticleMaterialResolver::Impl {
    std::unordered_map<std::string, CachedParticleMaterial> cache{};
};

ParticleMaterialResolver::ParticleMaterialResolver() : impl_(std::make_unique<Impl>()) {}

ParticleMaterialResolver::~ParticleMaterialResolver() = default;

void ParticleMaterialResolver::Clear() {
    if (impl_ != nullptr) {
        impl_->cache.clear();
    }
}

ResolvedParticleMaterial ParticleMaterialResolver::Resolve(
    const ParticleEmitterRenderParameters& render, const AssetRegistry* assetRegistry,
    const MATERIAL::MaterialExternalValueSnapshot* externalValues) {
    ResolvedParticleMaterial result{};
    if (assetRegistry == nullptr) {
        return result;
    }

    const bool embeddedMaterial = render.effectMaterial != nullptr;
    const bool standardMaterial = !embeddedMaterial && !IsCustomParticleMaterial(render.materialMode);
    const bool lit = IsLitParticleMaterial(render.materialMode);
    const std::string_view resourceGuid =
        standardMaterial ? std::string_view(render.colorTextureAssetGuid) : std::string_view(render.materialAssetGuid);
    if (!standardMaterial && !embeddedMaterial && resourceGuid.empty()) {
        return result;
    }

    const std::string cacheKey =
        embeddedMaterial ? "effect:" + render.effectMaterialKey
                         : std::string(standardMaterial ? (lit ? "standard-lit:" : "standard:") : "custom:") +
                               (resourceGuid.empty() ? std::string("untextured") : std::string(resourceGuid));
    if (impl_->cache.size() > 64u) {
        // DrawItem が保持する参照が生きている間は削除しない。
        std::erase_if(impl_->cache,
                      [](const auto& entry) { return entry.second.embedded && entry.second.effectSource.expired(); });
    }
    CachedParticleMaterial& cached = impl_->cache[cacheKey];
    const bool cacheInvalid = cached.assetRegistryInstanceId != assetRegistry->GetInstanceId() ||
                              cached.assetRegistryRevision != assetRegistry->GetRevision() ||
                              (cached.material == nullptr && !cached.sourceRejected);
    if (cacheInvalid) {
        cached = {};
        cached.embedded = embeddedMaterial;
        cached.effectSource = render.effectMaterial;
        cached.assetRegistryInstanceId = assetRegistry->GetInstanceId();
        cached.assetRegistryRevision = assetRegistry->GetRevision();

        if (standardMaterial) {
            const bool textured = !resourceGuid.empty();
            const TextureAssetDescriptor* textureDescriptor =
                textured ? assetRegistry->FindAs<TextureAssetDescriptor>(AssetGuid{std::string(resourceGuid)})
                         : nullptr;
            if (textured && (textureDescriptor == nullptr || textureDescriptor->sourcePath.empty())) {
                cached.sourceRejected = true;
                result.rejected = true;
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][TextureMissing] " + std::string(resourceGuid) +
                                 " is not a registered Texture asset.");
                return result;
            }

            // Direct VFX textures are projected through the canonical
            // standard material runtime. ParticlePS then uses the same
            // compact base-color texture path as ordinary Surface
            // materials instead of invoking the generic node-expression
            // interpreter for every overdrawn particle pixel.
            PbrMaterialAssetData source{};
            source.materialName = lit ? "Built-in VFX Lit" : "Built-in VFX Unlit";
            source.unlit = !lit;
            source.doubleSided = true;
            source.emissiveStrength = 0.0f;
            if (textured) {
                source.baseColorTexture.useTexture = true;
                source.baseColorTexture.textureAssetGuid = AssetGuid{std::string(resourceGuid)};
            }
            auto material = std::make_unique<Material>();
            MaterialRuntimeBuilder builder{};
            if (!builder.BuildRuntimeMaterial(source, *assetRegistry, MATERIAL::GetRuntimeMaterialProgramRegistry(),
                                              *material, cacheKey)) {
                cached.resourceRejected = true;
                result.rejected = true;
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass]"
                                 "[StandardMaterialBuildFailed] " +
                                 (resourceGuid.empty() ? std::string("untextured") : std::string(resourceGuid)));
                return result;
            }
            if (textured) {
                const RuntimeTextureSlot& textureSlot = material->GetTextureSlot(MaterialTextureUsage::BaseColor);
                if (!textureSlot.IsActive() || textureSlot.sourcePath != textureDescriptor->sourcePath) {
                    cached.resourceRejected = true;
                    result.rejected = true;
                    HIKARI_LOG_ERROR("[ParticleGpuDrivenPass]"
                                     "[StandardTextureResidencyFailed] " +
                                     std::string(resourceGuid));
                    return result;
                }
            }
            material->SetUnlit(true);
            material->SetDoubleSided(true);
            material->SetMaterialAlphaMode(RuntimeMaterialAlphaMode::Blend);
            cached.material = std::move(material);
        } else {
            const auto* descriptor =
                embeddedMaterial ? render.effectMaterial.get()
                                 : assetRegistry->FindAs<MaterialAssetDescriptor>(AssetGuid{std::string(resourceGuid)});
            if (descriptor == nullptr) {
                cached.resourceRejected = true;
                result.rejected = true;
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][MaterialMissing] " + std::string(resourceGuid));
                return result;
            }
            if (!descriptor->compileResult.success || !IsRenderableMaterialAssetKind(descriptor->sourceData.kind)) {
                cached.sourceRejected = true;
                result.rejected = true;
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][MaterialSourceInvalid] " + std::string(resourceGuid) +
                                 " is not a compiled renderable Material asset.");
                return result;
            }
            auto material = std::make_unique<Material>();
            MaterialRuntimeBuilder builder{};
            if (!builder.BuildRuntimeMaterial(descriptor->sourceData, descriptor->compileResult, *assetRegistry,
                                              MATERIAL::GetRuntimeMaterialProgramRegistry(), *material, cacheKey)) {
                cached.resourceRejected = true;
                result.rejected = true;
                HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][MaterialBuildFailed] " + std::string(resourceGuid));
                return result;
            }
            cached.material = std::move(material);
        }
    }

    if (cached.sourceRejected || cached.resourceRejected || cached.material == nullptr) {
        result.rejected = cached.sourceRejected || cached.resourceRejected;
        return result;
    }

    const MATERIAL::MaterialExternalValueSnapshot emptySnapshot{};
    const auto& snapshot = externalValues != nullptr ? *externalValues : emptySnapshot;
    const bool repeatedProjectionFailure = cached.projectionRejected &&
                                           cached.failedProjectionInstanceId == snapshot.sourceInstanceId &&
                                           cached.failedProjectionRevision == snapshot.revision;
    if (!repeatedProjectionFailure) {
        std::string error{};
        (void)MATERIAL::ApplyMaterialExternalValues(*cached.material, snapshot, error);
        cached.projectionRejected = !error.empty();
        cached.failedProjectionInstanceId = snapshot.sourceInstanceId;
        cached.failedProjectionRevision = snapshot.revision;
        if (!error.empty()) {
            HIKARI_LOG_ERROR("[ParticleGpuDrivenPass][MaterialProjectionFailed] " + error);
        }
    }
    result.material = cached.material.get();
    return result;
}

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
