#pragma once

#include <array>
#include <span>
#include <unordered_set>

#include "HIKARI/Project/LocalFogTypes.h"
#include "Render3D/Lighting/HIKARI_LocalFogVolume.h"

namespace HIKARI {
class World;
struct SceneDocument;

// The document runtime owns storage; World exposes a borrowed service.
// Bounded frame submissions, no independent persistent instance lifecycle.
class LocalFogService final {
public:
    GameProjectWorldResultCode Submit(uint64_t owner, uint64_t instance, const GameProjectLocalFogVolume& volume,
                                      uint64_t frameIndex) noexcept;
    void Remove(uint64_t owner, uint64_t instance) noexcept;
    void Clear() noexcept;
    std::span<const RENDER3D::VOLUMETRIC::LocalFogVolumeGpu> PrepareFrame(const World& world, uint64_t frameIndex,
                                                                          const SceneDocument* document = nullptr,
                                                                          uint64_t documentRevision = 0u);
    void DrawSelectedBounds(const World& world, uint64_t sceneObjectId) const;

private:
    struct Entry {
        uint64_t owner = 0u;
        uint64_t instance = 0u;
        GameProjectLocalFogVolume volume{};
    };
    std::array<Entry, kGameProjectLocalFogMaximumVolumes> entries_{};
    std::array<RENDER3D::VOLUMETRIC::LocalFogVolumeGpu, kGameProjectLocalFogMaximumVolumes> projected_{};
    uint32_t count_ = 0u;
    uint64_t frameIndex_ = ~uint64_t{0};
    const SceneDocument* authoredDocument_ = nullptr;
    uint64_t authoredRevision_ = 0u;
    std::unordered_set<uint64_t> disabledObjects_{};
};

} // namespace HIKARI
