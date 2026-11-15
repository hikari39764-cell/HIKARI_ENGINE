#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI {

class AssetRegistry;
class ModelManager;
class World;

namespace RENDER3D::RUNTIME {
class RenderModelCache;
class SceneRenderCache;
} // namespace RENDER3D::RUNTIME

class SceneRenderCacheSync {
public:
    SceneRenderCacheSync();
    ~SceneRenderCacheSync();
    SceneRenderCacheSync(const SceneRenderCacheSync&) = delete;
    SceneRenderCacheSync& operator=(const SceneRenderCacheSync&) = delete;

    void Clear() noexcept;

    void Sync(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
              RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache, uint64_t frameIndex,
              const AssetRegistry* assetRegistry = nullptr, std::filesystem::path projectRoot = {},
              RENDER3D::RenderViewPurpose purpose = RENDER3D::RenderViewPurpose::Game,
              ModelManager* modelManager = nullptr);

    void SyncDirty(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                   RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache, uint64_t frameIndex,
                   const AssetRegistry* assetRegistry = nullptr, std::filesystem::path projectRoot = {},
                   RENDER3D::RenderViewPurpose purpose = RENDER3D::RenderViewPurpose::Game,
                   ModelManager* modelManager = nullptr);

private:
    struct State;
    std::unique_ptr<State> state_{};
};

} // namespace HIKARI
