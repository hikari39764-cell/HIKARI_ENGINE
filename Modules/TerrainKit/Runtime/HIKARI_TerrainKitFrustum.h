#pragma once

#include <array>

namespace HIKARI::TERRAINKIT {

// 視錐台の 6 面（描く節と河の帯を選ぶ）。
struct FrustumPlanes {
    std::array<std::array<float, 4>, 6> planes{};

    explicit FrustumPlanes(const float m[16]) {
        // 列優先 m[col][row]：行 r は (m[0+r], m[4+r], m[8+r], m[12+r])。D3D の深度は 0..w。
        const auto row = [&](int r) { return std::array<float, 4>{m[r], m[4 + r], m[8 + r], m[12 + r]}; };
        const auto r0 = row(0);
        const auto r1 = row(1);
        const auto r2 = row(2);
        const auto r3 = row(3);
        for (int i = 0; i < 4; ++i) {
            planes[0][i] = r3[i] + r0[i];
            planes[1][i] = r3[i] - r0[i];
            planes[2][i] = r3[i] + r1[i];
            planes[3][i] = r3[i] - r1[i];
            planes[4][i] = r2[i];
            planes[5][i] = r3[i] - r2[i];
        }
    }

    bool IntersectsBox(const float minimum[3], const float maximum[3]) const noexcept {
        for (const auto& plane : planes) {
            const float x = plane[0] >= 0.0f ? maximum[0] : minimum[0];
            const float y = plane[1] >= 0.0f ? maximum[1] : minimum[1];
            const float z = plane[2] >= 0.0f ? maximum[2] : minimum[2];
            if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace HIKARI::TERRAINKIT
