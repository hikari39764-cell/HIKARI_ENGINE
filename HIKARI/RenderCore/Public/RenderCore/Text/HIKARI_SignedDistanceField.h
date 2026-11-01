#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace HIKARI::TEXT_RENDERING {

// 安定した coverage raster を、Text renderer が共有する signed-distance
// 表現へ変換する。CID-keyed CFF glyph で不正な水平 segment を生成し得る
// stb の直接 SDF path は使わず、coverage から決定論的に構築する。
inline bool BuildSignedDistanceField(const uint8_t* coverage, int coverageWidth, int coverageHeight, int padding,
                                     uint8_t onEdgeValue, float pixelDistanceScale, std::vector<uint8_t>& outPixels,
                                     int& outWidth, int& outHeight) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    if (coverage == nullptr || coverageWidth <= 0 || coverageHeight <= 0 || padding < 1 ||
        !std::isfinite(pixelDistanceScale) || pixelDistanceScale <= 0.0f) {
        return false;
    }

    const int width = coverageWidth + padding * 2;
    const int height = coverageHeight + padding * 2;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    constexpr float kInfinity = 1.0e20f;
    std::vector<float> distanceToInside(pixelCount, kInfinity);
    std::vector<float> distanceToOutside(pixelCount, 0.0f);
    std::vector<uint8_t> paddedCoverage(pixelCount, 0u);
    bool hasInside = false;
    for (int y = 0; y < coverageHeight; ++y) {
        for (int x = 0; x < coverageWidth; ++x) {
            const uint8_t value =
                coverage[static_cast<size_t>(y) * static_cast<size_t>(coverageWidth) + static_cast<size_t>(x)];
            const size_t destination =
                static_cast<size_t>(y + padding) * static_cast<size_t>(width) + static_cast<size_t>(x + padding);
            paddedCoverage[destination] = value;
            if (value >= 128u) {
                distanceToInside[destination] = 0.0f;
                distanceToOutside[destination] = kInfinity;
                hasInside = true;
            }
        }
    }
    if (!hasInside)
        return false;

    const auto transform1D = [=](const float* source, int count, float* destination, std::vector<int>& sites,
                                 std::vector<float>& intersections) {
        int k = 0;
        sites[0] = 0;
        intersections[0] = -kInfinity;
        intersections[1] = kInfinity;
        for (int q = 1; q < count; ++q) {
            float boundary = 0.0f;
            for (;;) {
                const int site = sites[k];
                boundary =
                    ((source[q] + static_cast<float>(q * q)) - (source[site] + static_cast<float>(site * site))) /
                    static_cast<float>(2 * (q - site));
                if (boundary > intersections[k] || k == 0)
                    break;
                --k;
            }
            ++k;
            sites[k] = q;
            intersections[k] = boundary;
            intersections[k + 1] = kInfinity;
        }
        k = 0;
        for (int q = 0; q < count; ++q) {
            while (intersections[k + 1] < static_cast<float>(q))
                ++k;
            const float delta = static_cast<float>(q - sites[k]);
            destination[q] = delta * delta + source[sites[k]];
        }
    };

    const auto transform2D = [&](std::vector<float>& values) {
        const int maximum = (std::max)(width, height);
        std::vector<float> source(static_cast<size_t>(maximum));
        std::vector<float> destination(static_cast<size_t>(maximum));
        std::vector<int> sites(static_cast<size_t>(maximum));
        std::vector<float> intersections(static_cast<size_t>(maximum) + 1u);
        for (int y = 0; y < height; ++y) {
            const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width);
            transform1D(values.data() + row, width, destination.data(), sites, intersections);
            std::copy_n(destination.data(), width, values.data() + row);
        }
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                source[static_cast<size_t>(y)] =
                    values[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
            }
            transform1D(source.data(), height, destination.data(), sites, intersections);
            for (int y = 0; y < height; ++y) {
                values[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                    destination[static_cast<size_t>(y)];
            }
        }
    };

    transform2D(distanceToInside);
    transform2D(distanceToOutside);
    outPixels.resize(pixelCount);
    for (size_t index = 0u; index < pixelCount; ++index) {
        const bool inside = paddedCoverage[index] >= 128u;
        float signedDistance = inside ? std::sqrt(distanceToOutside[index]) : -std::sqrt(distanceToInside[index]);
        // Antialiased coverage edge を sub-pixel shift として保持する。
        signedDistance += static_cast<float>(paddedCoverage[index]) / 255.0f - 0.5f;
        const float encoded = static_cast<float>(onEdgeValue) + signedDistance * pixelDistanceScale;
        outPixels[index] = static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0f, 255.0f)));
    }
    outWidth = width;
    outHeight = height;
    return true;
}

} // namespace HIKARI::TEXT_RENDERING
