#include "Scene/Vfx/HIKARI_VfxFluidShapeCache.h"

#include "Assets/Models/HIKARI_ModelManager.h"
#include "Core/HIKARI_Logger.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"

namespace HIKARI::VFX {

void VfxFluidShapeCache::Bind(const AssetRegistry* assetRegistry, ModelManager* modelManager) noexcept {
    const uint64_t id = assetRegistry != nullptr ? assetRegistry->GetInstanceId() : 0u;
    const uint64_t revision = assetRegistry != nullptr ? assetRegistry->GetRevision() : 0u;
    if (assetRegistry_ != assetRegistry || modelManager_ != modelManager || registryId_ != id ||
        registryRevision_ != revision)
        volumes_.clear();
    assetRegistry_ = assetRegistry;
    modelManager_ = modelManager;
    registryId_ = id;
    registryRevision_ = revision;
}

std::shared_ptr<const RENDER3D::FLUID::FluidShapeVolume> VfxFluidShapeCache::Resolve(const std::string& modelGuid) {
    if (modelGuid.empty() || assetRegistry_ == nullptr || modelManager_ == nullptr)
        return nullptr;
    if (const auto found = volumes_.find(modelGuid); found != volumes_.end())
        return found->second;
    std::shared_ptr<const RENDER3D::FLUID::FluidShapeVolume> volume{};
    const ModelAsset* model = ResolveModelAssetNow(*modelManager_, *assetRegistry_, modelGuid);
    if (model != nullptr && model->GetState() == ModelAsset::State::Loaded)
        volume = RENDER3D::FLUID::BakeFluidShapeVolume(*model);
    if (!volume)
        HIKARI_LOG_WARN(
            "[FluidSmoke] Model shape could not be built (model missing, not loaded or without triangles): " +
            modelGuid);
    volumes_.emplace(modelGuid, volume);
    return volume;
}

void VfxFluidShapeCache::Clear() noexcept { volumes_.clear(); }

} // namespace HIKARI::VFX
