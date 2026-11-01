#include "Render3D/Runtime/HIKARI_RenderModelCache.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace HIKARI::RENDER3D::RUNTIME {

const RenderModelAsset* RenderModelCache::GetOrCreate(const ModelAsset& source) {
    ++stats_.requestCount;

    const auto found = cache_.find(&source);
    if (found != cache_.end() && found->second.sourceRevision == source.sourceRevision &&
        found->second.asset != nullptr) {
        ++stats_.hitCount;
        return found->second.asset.get();
    }

    ++stats_.missCount;
    auto runtimeAsset = std::make_unique<RenderModelAsset>();
    (void)BuildRenderModelAsset(source, *runtimeAsset, nullptr);

    const RenderModelAsset* raw = runtimeAsset.get();
    cache_[&source] = Entry{source.sourceRevision, std::move(runtimeAsset)};
    RefreshCacheStats();
    return raw;
}

void RenderModelCache::Invalidate(const ModelAsset& source) {
    cache_.erase(&source);
    RefreshCacheStats();
}

void RenderModelCache::Clear() {
    cache_.clear();
    stats_ = {};
}

const RenderModelCache::Stats& RenderModelCache::GetStats() const { return stats_; }

void RenderModelCache::RefreshCacheStats() {
    stats_.cachedModelCount =
        static_cast<uint32_t>((std::min)(cache_.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
    uint64_t surfaceCount = 0;
    uint64_t invalidModelCount = 0;
    for (const auto& entry : cache_) {
        if (entry.second.asset != nullptr) {
            surfaceCount += entry.second.asset->surfaces.size();
            if (!entry.second.asset->valid) {
                ++invalidModelCount;
            }
        }
    }
    stats_.cachedSurfaceCount =
        static_cast<uint32_t>((std::min)(surfaceCount, static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
    stats_.invalidModelCount = static_cast<uint32_t>(
        (std::min)(invalidModelCount, static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
}

} // namespace HIKARI::RENDER3D::RUNTIME
