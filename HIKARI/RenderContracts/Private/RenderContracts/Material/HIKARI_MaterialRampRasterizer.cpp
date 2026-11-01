#include "RenderContracts/Material/HIKARI_MaterialRampRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Core/Hashing/HIKARI_FnvHash.h"

namespace HIKARI::ASSETS::MATERIAL {

namespace {

struct RampPointView {
    const MaterialRampPointData* point = nullptr;
};

struct NearestPoint {
    float distanceSquared = (std::numeric_limits<float>::max)();
    const MaterialRampPointData* point = nullptr;
};

float ClampUnit(float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }

MATH::Vec4 ClampColor(const MATH::Vec4& value) noexcept {
    return {ClampUnit(value.x), ClampUnit(value.y), ClampUnit(value.z), ClampUnit(value.w)};
}

MATH::Vec4 LerpColor(const MATH::Vec4& left, const MATH::Vec4& right, float amount) noexcept {
    return {left.x + (right.x - left.x) * amount, left.y + (right.y - left.y) * amount,
            left.z + (right.z - left.z) * amount, left.w + (right.w - left.w) * amount};
}

float ResolveInterpolationAmount(MaterialRampInterpolation interpolation, float amount) noexcept {
    const float value = ClampUnit(amount);
    switch (interpolation) {
    case MaterialRampInterpolation::Constant: return value >= 1.0f ? 1.0f : 0.0f;
    case MaterialRampInterpolation::Smooth: return value * value * (3.0f - 2.0f * value);
    case MaterialRampInterpolation::Linear:
    default: return value;
    }
}

bool IsFinitePoint(const MaterialRampPointData& point) noexcept {
    return std::isfinite(point.position.x) && std::isfinite(point.position.y) && std::isfinite(point.color.x) &&
           std::isfinite(point.color.y) && std::isfinite(point.color.z) && std::isfinite(point.color.w);
}

std::vector<RampPointView> BuildSortedPoints(const MaterialRampData& ramp) {
    std::vector<RampPointView> points{};
    points.reserve(ramp.points.size());
    for (const MaterialRampPointData& point : ramp.points) {
        points.push_back({&point});
    }
    std::sort(points.begin(), points.end(), [](const RampPointView& left, const RampPointView& right) {
        if (left.point->position.x != right.point->position.x) {
            return left.point->position.x < right.point->position.x;
        }
        if (left.point->position.y != right.point->position.y) {
            return left.point->position.y < right.point->position.y;
        }
        return left.point->id < right.point->id;
    });
    return points;
}

MATH::Vec4 SampleOneDimensional(const std::vector<RampPointView>& points, MaterialRampInterpolation interpolation,
                                float coordinate) noexcept {
    const float x = ClampUnit(coordinate);
    if (x <= points.front().point->position.x) {
        return ClampColor(points.front().point->color);
    }
    if (x >= points.back().point->position.x) {
        return ClampColor(points.back().point->color);
    }
    const auto right =
        std::upper_bound(points.begin(), points.end(), x, [](float value, const RampPointView& candidate) {
            return value < candidate.point->position.x;
        });
    const RampPointView& left = *(right - 1);
    const float span = (std::max)(right->point->position.x - left.point->position.x, 1.0e-6f);
    const float amount = ResolveInterpolationAmount(interpolation, (x - left.point->position.x) / span);
    return ClampColor(LerpColor(left.point->color, right->point->color, amount));
}

void InsertNearest(std::array<NearestPoint, 4u>& nearest, const MaterialRampPointData& point,
                   float distanceSquared) noexcept {
    if (distanceSquared >= nearest.back().distanceSquared) {
        return;
    }
    size_t position = nearest.size() - 1u;
    while (position > 0u && distanceSquared < nearest[position - 1u].distanceSquared) {
        nearest[position] = nearest[position - 1u];
        --position;
    }
    nearest[position] = {distanceSquared, &point};
}

MATH::Vec4 SampleTwoDimensional(const MaterialRampData& ramp, float x, float y) noexcept {
    std::array<NearestPoint, 4u> nearest{};
    for (const MaterialRampPointData& point : ramp.points) {
        const float dx = x - point.position.x;
        const float dy = y - point.position.y;
        InsertNearest(nearest, point, dx * dx + dy * dy);
    }
    if (nearest.front().point == nullptr) {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    if (ramp.interpolation == MaterialRampInterpolation::Constant || nearest.front().distanceSquared <= 1.0e-12f) {
        return ClampColor(nearest.front().point->color);
    }

    MATH::Vec4 weighted{};
    float totalWeight = 0.0f;
    for (const NearestPoint& candidate : nearest) {
        if (candidate.point == nullptr) {
            continue;
        }
        const float distance = std::sqrt((std::max)(candidate.distanceSquared, 1.0e-12f));
        float weight = 1.0f / distance;
        if (ramp.interpolation == MaterialRampInterpolation::Smooth) {
            weight *= weight;
        }
        const MATH::Vec4 color = ClampColor(candidate.point->color);
        weighted.x += color.x * weight;
        weighted.y += color.y * weight;
        weighted.z += color.z * weight;
        weighted.w += color.w * weight;
        totalWeight += weight;
    }
    if (totalWeight <= 0.0f) {
        return ClampColor(nearest.front().point->color);
    }
    const float reciprocal = 1.0f / totalWeight;
    return ClampColor(
        {weighted.x * reciprocal, weighted.y * reciprocal, weighted.z * reciprocal, weighted.w * reciprocal});
}

uint8_t ToByte(float value) noexcept { return static_cast<uint8_t>(std::lround(ClampUnit(value) * 255.0f)); }

void StoreColor(const MATH::Vec4& color, uint8_t* destination) noexcept {
    destination[0] = ToByte(color.x);
    destination[1] = ToByte(color.y);
    destination[2] = ToByte(color.z);
    destination[3] = ToByte(color.w);
}

} // namespace

bool RasterizeMaterialRamp(const MaterialRampData& ramp, MaterialRampRasterImage& outImage, std::string& outError) {
    outImage = {};
    const bool oneDimensional = ramp.dimension == MaterialRampDimension::OneD;
    const uint32_t maximumResolution =
        oneDimensional ? kMaximumMaterialRamp1DResolution : kMaximumMaterialRamp2DResolution;
    if (ramp.width < 2u || ramp.width > maximumResolution || ramp.height < 1u || ramp.height > maximumResolution ||
        (oneDimensional && ramp.height != 1u) || (!oneDimensional && ramp.height < 2u) || ramp.points.size() < 2u ||
        ramp.points.size() > 256u) {
        outError = "Material Ramp resolution or control-point count is outside the runtime contract";
        return false;
    }
    for (const MaterialRampPointData& point : ramp.points) {
        if (!IsFinitePoint(point) || point.position.x < 0.0f || point.position.x > 1.0f || point.position.y < 0.0f ||
            point.position.y > 1.0f) {
            outError = "Material Ramp contains a non-finite or out-of-range control point";
            return false;
        }
    }

    outImage.width = ramp.width;
    outImage.height = ramp.height;
    outImage.rgba8.resize(static_cast<size_t>(ramp.width) * ramp.height * 4u);
    const std::vector<RampPointView> sorted = BuildSortedPoints(ramp);
    for (uint32_t y = 0u; y < ramp.height; ++y) {
        const float v = ramp.height > 1u ? static_cast<float>(y) / static_cast<float>(ramp.height - 1u) : 0.0f;
        for (uint32_t x = 0u; x < ramp.width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(ramp.width - 1u);
            const MATH::Vec4 color =
                oneDimensional ? SampleOneDimensional(sorted, ramp.interpolation, u) : SampleTwoDimensional(ramp, u, v);
            const size_t offset = (static_cast<size_t>(y) * ramp.width + x) * 4u;
            StoreColor(color, outImage.rgba8.data() + offset);
        }
    }

    uint64_t hash = HASH::kFnv1a64OffsetBasis;
    hash = HASH::Fnv1aBytes(hash, &outImage.width, sizeof(outImage.width));
    hash = HASH::Fnv1aBytes(hash, &outImage.height, sizeof(outImage.height));
    const auto interpolation = static_cast<uint8_t>(ramp.interpolation);
    hash = HASH::Fnv1aBytes(hash, &interpolation, sizeof(interpolation));
    hash = HASH::Fnv1aBytes(hash, outImage.rgba8.data(), outImage.rgba8.size());
    outImage.contentHash = hash;
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::MATERIAL
