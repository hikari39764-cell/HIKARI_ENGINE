#include "Render3D/ScreenSpace/HIKARI_ScreenComposite.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D {

namespace {
struct Candidate {
    uint32_t source = 0u;
    std::array<MATH::Vec2, 4> screen{};
    std::array<float, 4> depth{};
    float visible = 0.0f;
};

// 単位正方形 (s, t) から画面の四角形への射影（Heckbert の正方形→四角形）。行は (X, Y, W) を作る 3 行。
bool SquareToQuad(const std::array<MATH::Vec2, 4>& p, double m[3][3]) {
    const double dx1 = p[1].x - p[2].x, dx2 = p[3].x - p[2].x, dx3 = p[0].x - p[1].x + p[2].x - p[3].x;
    const double dy1 = p[1].y - p[2].y, dy2 = p[3].y - p[2].y, dy3 = p[0].y - p[1].y + p[2].y - p[3].y;
    double g = 0.0, h = 0.0;
    if (std::abs(dx3) > 1e-12 || std::abs(dy3) > 1e-12) {
        const double den = dx1 * dy2 - dx2 * dy1;
        if (std::abs(den) < 1e-15)
            return false;
        g = (dx3 * dy2 - dx2 * dy3) / den;
        h = (dx1 * dy3 - dx3 * dy1) / den;
    }
    m[0][0] = p[1].x - p[0].x + g * p[1].x;
    m[0][1] = p[3].x - p[0].x + h * p[3].x;
    m[0][2] = p[0].x;
    m[1][0] = p[1].y - p[0].y + g * p[1].y;
    m[1][1] = p[3].y - p[0].y + h * p[3].y;
    m[1][2] = p[0].y;
    m[2][0] = g;
    m[2][1] = h;
    m[2][2] = 1.0;
    return true;
}

bool Invert3x3(const double m[3][3], double out[3][3]) {
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (!std::isfinite(det) || std::abs(det) < 1e-18)
        return false;
    const double inv = 1.0 / det;
    out[0][0] = c00 * inv;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
    out[1][0] = c01 * inv;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
    out[2][0] = c02 * inv;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
    return true;
}

// 三隅の (u, v, 深度) から、深度 = a·u + b·v + c を解く。
bool SolveDepthPlane(const Candidate& candidate, MATH::Vec4& plane) {
    const MATH::Vec2 a = candidate.screen[0], b = candidate.screen[1], c = candidate.screen[3];
    const double det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::abs(det) < 1e-15)
        return false;
    const double dzb = candidate.depth[1] - candidate.depth[0];
    const double dzc = candidate.depth[3] - candidate.depth[0];
    const double gx = (dzb * (c.y - a.y) - dzc * (b.y - a.y)) / det;
    const double gy = (dzc * (b.x - a.x) - dzb * (c.x - a.x)) / det;
    plane = {static_cast<float>(gx), static_cast<float>(gy),
             static_cast<float>(candidate.depth[0] - gx * a.x - gy * a.y), 0.0f};
    return true;
}
} // namespace

ScreenCompositeFrame ProjectScreenComposites(const ScreenCompositeWorldFrame& source, const MATH::Mat4& viewProjection,
                                             const MATH::Vec3& cameraPosition) {
    ScreenCompositeFrame frame{};
    frame.cameraPosition = cameraPosition;
    std::array<Candidate, kScreenCompositeCandidateCount> candidates{};
    uint32_t candidateCount = 0u;
    for (uint32_t index = 0u; index < (std::min)(source.count, kScreenCompositeCandidateCount); ++index) {
        const ScreenCompositeWorldRegion& region = source.regions[index];
        if (!region.texture.IsValid())
            continue;
        Candidate candidate{};
        candidate.source = index;
        bool valid = true;
        MATH::Vec2 low{1e9f, 1e9f}, high{-1e9f, -1e9f};
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            const MATH::Vec3& p = region.corners[corner];
            const MATH::Vec4 clip = viewProjection.TransformPoint({p.x, p.y, p.z, 1.0f});
            // 隅がカメラの後ろへ回った画面は射影が裏返るので、貼り直さず拡大の結果のままにする。
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) ||
                clip.w <= 1e-4f) {
                valid = false;
                break;
            }
            const MATH::Vec2 uv{clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f};
            candidate.screen[corner] = uv;
            candidate.depth[corner] = clip.z / clip.w;
            low = {(std::min)(low.x, uv.x), (std::min)(low.y, uv.y)};
            high = {(std::max)(high.x, uv.x), (std::max)(high.y, uv.y)};
        }
        if (!valid)
            continue;
        const float width = (std::min)(high.x, 1.0f) - (std::max)(low.x, 0.0f);
        const float height = (std::min)(high.y, 1.0f) - (std::max)(low.y, 0.0f);
        if (width <= 0.0f || height <= 0.0f)
            continue;
        candidate.visible = width * height;
        candidates[candidateCount++] = candidate;
    }
    std::sort(candidates.begin(), candidates.begin() + candidateCount,
              [](const Candidate& a, const Candidate& b) { return a.visible > b.visible; });

    for (uint32_t index = 0u; index < candidateCount && frame.count < kScreenCompositeRegionCount; ++index) {
        const Candidate& candidate = candidates[index];
        const ScreenCompositeWorldRegion& region = source.regions[candidate.source];
        // 貼図は同じものをまとめ、上限を超える画面は貼り直さない（大きく見える画面から貼図の枠を使う）。
        uint32_t textureIndex = 0u;
        while (textureIndex < frame.textureCount && frame.textures[textureIndex] != region.texture)
            ++textureIndex;
        if (textureIndex == frame.textureCount) {
            if (frame.textureCount == kScreenCompositeTextureCount)
                continue;
            frame.textures[frame.textureCount++] = region.texture;
        }
        double forward[3][3]{};
        double inverse[3][3]{};
        MATH::Vec4 depthPlane{};
        if (!SquareToQuad(candidate.screen, forward) || !Invert3x3(forward, inverse) ||
            !SolveDepthPlane(candidate, depthPlane))
            continue;
        ScreenCompositeRegionConstants& out = frame.regions[frame.count++];
        for (int row = 0; row < 3; ++row)
            out.surfaceFromScreen[row] = {static_cast<float>(inverse[row][0]), static_cast<float>(inverse[row][1]),
                                          static_cast<float>(inverse[row][2]), 0.0f};
        out.uv01 = {region.uv[0].x, region.uv[0].y, region.uv[1].x, region.uv[1].y};
        out.uv23 = {region.uv[2].x, region.uv[2].y, region.uv[3].x, region.uv[3].y};
        depthPlane.w = static_cast<float>(textureIndex);
        out.depthPlane = depthPlane;
        const MATH::Vec3& origin = region.corners[0];
        out.worldOrigin = {origin.x, origin.y, origin.z, 0.0f};
        out.worldS = {region.corners[1].x - origin.x, region.corners[1].y - origin.y, region.corners[1].z - origin.z,
                      0.0f};
        out.worldT = {region.corners[3].x - origin.x, region.corners[3].y - origin.y, region.corners[3].z - origin.z,
                      0.0f};
        MATH::Vec2 low{1e9f, 1e9f}, high{-1e9f, -1e9f};
        for (const MATH::Vec2& uv : candidate.screen) {
            low = {(std::min)(low.x, uv.x), (std::min)(low.y, uv.y)};
            high = {(std::max)(high.x, uv.x), (std::max)(high.y, uv.y)};
        }
        out.rect = {low.x, low.y, high.x, high.y};
        out.baseColor = region.baseColor;
    }
    return frame;
}

} // namespace HIKARI::RENDER3D
