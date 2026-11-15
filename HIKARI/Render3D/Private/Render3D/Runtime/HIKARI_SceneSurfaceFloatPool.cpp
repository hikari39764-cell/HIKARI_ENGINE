#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"

#include <algorithm>
#include <limits>

namespace HIKARI::RENDER3D::RUNTIME {

SceneSurfaceFloatPoolPacker::SceneSurfaceFloatPoolPacker(std::vector<float>& destination, std::size_t capacity)
    : destination_(destination), capacity_((std::min)(capacity, std::size_t{(std::numeric_limits<uint32_t>::max)()})) {
    destination_.clear();
    if (capacity_ >= kSceneSurfaceNeutralFloatCount) {
        destination_.assign(kSceneSurfaceNeutralFloatCount, 0.0f);
    }
}

std::size_t SceneSurfaceFloatPoolPacker::GetRemaining() const noexcept {
    return destination_.size() <= capacity_ ? capacity_ - destination_.size() : 0u;
}

bool SceneSurfaceFloatPoolPacker::IsUsable() const noexcept {
    return destination_.size() >= kSceneSurfaceNeutralFloatCount && destination_.size() <= capacity_;
}

bool SceneSurfaceFloatPoolPacker::TryPackSquareField(const std::vector<float>& values, uint32_t gridResolution,
                                                     uint32_t& outOffset) {
    outOffset = 0u;
    const uint32_t grid = (std::max)(gridResolution, 1u);
    if (!IsUsable() || grid > 512u) {
        return false;
    }
    const std::size_t side = static_cast<std::size_t>(grid) + 1u;
    const std::size_t count = side * side;
    if (values.size() < count || count > GetRemaining()) {
        return false;
    }
    outOffset = static_cast<uint32_t>(destination_.size());
    destination_.insert(destination_.end(), values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    return true;
}

} // namespace HIKARI::RENDER3D::RUNTIME
