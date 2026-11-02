#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "Core/Math/HIKARI_MathValidation.h"

// 光照贴图の降噪（texel ごとの雑音の大きさで混ぜる量を決める à-trous）。積算の検査、扩边、DDS の書き出しは
// HIKARI_DxrLightmapBakeUtilities.cpp。
namespace HIKARI::TOOLS::BAKING::INTERNAL {

bool FilterLightmapIrradiance(std::span<MATH::Vec4> pixels, std::span<float> variance,
                              std::span<const DxrLightmapSurfaceTexel> surfaces, std::span<const uint8_t> validTexels,
                              std::span<const uint32_t> ownerIndices,
                              std::span<const ASSETS::LIGHTING::LightmapAtlasAllocation> allocations, uint32_t width,
                              uint32_t height) {
    const size_t count = static_cast<size_t>(width) * height;
    if (width == 0 || height == 0 || pixels.size() != count || variance.size() != count || surfaces.size() != count ||
        validTexels.size() != count || ownerIndices.size() != count)
        return false;
    // à-trous（5x5 の B-spline 核を 1, 2, 4 texel 間隔で三回）。同じ allocation の有効な texel だけを、法線と接平面が
    // 揃う近傍から混ぜる。輝度の差は中心の雑音の大きさ（平均の標準偏差の 4 倍）と比べ、雑音より大きい差（影や照度の
    // 境界）は混ぜない。分散も同じ重みで伝え、回を重ねるほど狭く混ぜる。作業領域は一 allocation 分。alpha（空の見える
    // 割合）も同じ重みで混ぜる（一 texel の標本で数えた割合は雑音が大きく、そのままだと鏡面の遮蔽が斑になる）。
    constexpr float kKernel[3]{3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};
    const auto luminance = [](const MATH::Vec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
    std::vector<MATH::Vec4> color, nextColor;
    std::vector<float> noise, nextNoise;
    std::vector<DxrLightmapSurfaceTexel> guides;
    std::vector<uint8_t> admitted;
    for (uint32_t owner = 0; owner < allocations.size(); ++owner) {
        const auto& rect = allocations[owner].outerRect;
        if (!rect.width || !rect.height || uint64_t(rect.x) + rect.width > width ||
            uint64_t(rect.y) + rect.height > height)
            return false;
        const size_t tileCount = static_cast<size_t>(rect.width) * rect.height;
        color.resize(tileCount);
        noise.resize(tileCount);
        guides.resize(tileCount);
        admitted.assign(tileCount, 0u);
        for (uint32_t y = 0; y < rect.height; ++y)
            for (uint32_t x = 0; x < rect.width; ++x) {
                const size_t local = size_t(y) * rect.width + x, global = size_t(y + rect.y) * width + x + rect.x;
                color[local] = pixels[global];
                noise[local] = variance[global];
                guides[local] = surfaces[global];
                admitted[local] = validTexels[global] != 0 && ownerIndices[global] == owner ? 1u : 0u;
                if (admitted[local] && (!MATH::IsFinite(guides[local].position) || guides[local].position.w <= 0))
                    return false;
            }
        nextColor = color;
        nextNoise = noise;
        for (const int step : {1, 2, 4}) {
            for (uint32_t y = 0; y < rect.height; ++y)
                for (uint32_t x = 0; x < rect.width; ++x) {
                    const size_t local = size_t(y) * rect.width + x;
                    if (!admitted[local])
                        continue;
                    const auto& center = guides[local];
                    const MATH::Vec3 n{center.normal.x, center.normal.y, center.normal.z};
                    const float centerLuminance = luminance(color[local]);
                    // 雑音が無い（収束した、または一様な）texel は自分だけを使う。
                    const float sigma = 4.0f * std::sqrt((std::max)(noise[local], 0.0f)) + 1.0e-4f * centerLuminance;
                    MATH::Vec4 sum{0, 0, 0, 0};
                    float total = 0, noiseSum = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int tx = int(x) + dx * step, ty = int(y) + dy * step;
                            if (tx < 0 || ty < 0 || tx >= int(rect.width) || ty >= int(rect.height))
                                continue;
                            const size_t tapLocal = size_t(ty) * rect.width + tx;
                            if (!admitted[tapLocal])
                                continue;
                            const auto& tap = guides[tapLocal];
                            const MATH::Vec3 tn{tap.normal.x, tap.normal.y, tap.normal.z};
                            const MATH::Vec3 delta{tap.position.x - center.position.x,
                                                   tap.position.y - center.position.y,
                                                   tap.position.z - center.position.z};
                            // 穴や chart の余白、隣の面を飛び越えて混ぜない（UV で近くても world で離れた texel、
                            // 向きの違う面、段差）。
                            const float size = (std::max)(center.position.w, tap.position.w);
                            const float reach = float(step * (std::max)(std::abs(dx), std::abs(dy)));
                            if (MATH::Dot(n, tn) < 0.9f ||
                                MATH::Dot(delta, delta) > (1.5f * reach + 1.0f) * (1.5f * reach + 1.0f) * size * size ||
                                (std::max)(std::abs(MATH::Dot(delta, n)), std::abs(MATH::Dot(delta, tn))) >
                                    0.25f * (std::max)(reach, 1.0f) * size)
                                continue;
                            const float difference = std::abs(luminance(color[tapLocal]) - centerLuminance);
                            const float similarity =
                                sigma > 0 ? std::exp(-difference / sigma) : float(dx == 0 && dy == 0);
                            const float weight = kKernel[std::abs(dx)] * kKernel[std::abs(dy)] * similarity;
                            const MATH::Vec4& tapColor = color[tapLocal];
                            sum = {sum.x + tapColor.x * weight, sum.y + tapColor.y * weight,
                                   sum.z + tapColor.z * weight, sum.w + tapColor.w * weight};
                            total += weight;
                            noiseSum += weight * weight * noise[tapLocal];
                        }
                    if (total > 0) {
                        nextColor[local] = {sum.x / total, sum.y / total, sum.z / total, sum.w / total};
                        nextNoise[local] = noiseSum / (total * total);
                    }
                }
            color = nextColor;
            noise = nextNoise;
        }
        for (uint32_t y = 0; y < rect.height; ++y)
            for (uint32_t x = 0; x < rect.width; ++x)
                if (admitted[size_t(y) * rect.width + x])
                    pixels[size_t(y + rect.y) * width + x + rect.x] = color[size_t(y) * rect.width + x];
    }
    return true;
}

} // namespace HIKARI::TOOLS::BAKING::INTERNAL
