#pragma once

#include <cstdint>
#include <filesystem>

#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI {

    class AssetRegistry;
    class World;

    namespace RENDER3D::RUNTIME {
        class RenderModelCache;
        class SceneRenderCache;
    }

    class SceneRenderCacheSync {
    public:
        void Sync(
            World& world,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
            uint64_t frameIndex,
            const AssetRegistry* assetRegistry = nullptr,
            std::filesystem::path projectRoot = {},
            RENDER3D::RenderViewPurpose purpose =
                RENDER3D::RenderViewPurpose::Game);

        void SyncDirty(
            World& world,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
            uint64_t frameIndex,
            const AssetRegistry* assetRegistry = nullptr,
            std::filesystem::path projectRoot = {},
            RENDER3D::RenderViewPurpose purpose =
                RENDER3D::RenderViewPurpose::Game);
    };

}
