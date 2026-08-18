#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

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
        struct DebugStats {
            uint64_t terrainFullProjectionCount = 0u;
            uint64_t terrainIncrementalProjectionCount = 0u;
            uint64_t terrainProjectionReuseCount = 0u;
            uint64_t terrainProjectedVertexCount = 0u;
            uint64_t terrainProjectionAllocationCount = 0u;
            uint64_t terrainResponseWarmRequestCount = 0u;
            uint64_t terrainResponseWarmPendingCount = 0u;
        };

        SceneRenderCacheSync();
        ~SceneRenderCacheSync();
        SceneRenderCacheSync(const SceneRenderCacheSync&) = delete;
        SceneRenderCacheSync& operator=(const SceneRenderCacheSync&) = delete;

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

        const DebugStats& GetDebugStats() const noexcept;
        void ResetDebugStats() noexcept;

    private:
        struct State;
        std::unique_ptr<State> state_{};
    };

}
