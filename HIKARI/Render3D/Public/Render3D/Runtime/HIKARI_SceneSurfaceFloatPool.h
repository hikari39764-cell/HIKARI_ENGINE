#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HIKARI::RENDER3D::RUNTIME {

// gSurfaceFeatureFloats は面の機能の field（今は水面の coverage）を一つの pool に並べる。
// offset=0/gridResolution=1 を安全な可視 fallback にするため 4 zero を予約する。
constexpr std::size_t kSceneSurfaceNeutralFloatCount = 4u;
// scene surface float pool の容量。
constexpr std::size_t kSurfaceFeatureFloatPoolCapacity = 1280u * 1024u;

// GPU upload へ渡す float pool の現在値。identity か version が変われば全量を転送する。
struct SurfaceFeatureFloatPoolSource {
    const std::vector<float>* values = nullptr;
    uint64_t identity = 0u;
    uint64_t version = 0u;
};

// Scene surface float pool 唯一の deterministic first-fit owner。入らない field は offset zero へ縮退する。
class SceneSurfaceFloatPoolPacker final {
public:
    SceneSurfaceFloatPoolPacker(std::vector<float>& destination, std::size_t capacity);

    // (gridResolution + 1)^2 の正方形 field を末尾へ足す。入らなければ false で outOffset は 0。
    bool TryPackSquareField(const std::vector<float>& values, uint32_t gridResolution, uint32_t& outOffset);

private:
    std::size_t GetRemaining() const noexcept;
    bool IsUsable() const noexcept;

    std::vector<float>& destination_;
    std::size_t capacity_ = 0u;
};

} // namespace HIKARI::RENDER3D::RUNTIME
