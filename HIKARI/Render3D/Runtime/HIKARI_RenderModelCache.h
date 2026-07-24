#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

namespace HIKARI::RENDER3D::RUNTIME {

    class RenderModelCache {
    public:
        struct Stats {
            uint32_t requestCount = 0;
            uint32_t hitCount = 0;
            uint32_t missCount = 0;
            uint32_t invalidModelCount = 0;
            uint32_t cachedModelCount = 0;
            uint32_t cachedSurfaceCount = 0;
        };

        const RenderModelAsset* GetOrCreate(const ModelAsset& source);

        void Invalidate(const ModelAsset& source);
        void Clear();

        const Stats& GetStats() const;

    private:
        void RefreshCacheStats();

        std::unordered_map<const ModelAsset*, std::unique_ptr<RenderModelAsset>> cache_;
        Stats stats_{};
    };

} // namespace HIKARI::RENDER3D::RUNTIME
