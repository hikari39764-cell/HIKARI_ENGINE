#pragma once

#include <cmath>

namespace HIKARI {
// 論理キャンバス座標用。親の非一様スケールと子の回転も保持する。
struct Affine2D {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    bool operator==(const Affine2D&) const noexcept = default;
    void Transform(float& x, float& y) const noexcept {
        const float px = a * x + c * y + tx;
        y = b * x + d * y + ty;
        x = px;
    }
    bool InverseTransform(float& x, float& y) const noexcept {
        const float det = a * d - b * c;
        if (!std::isfinite(det) || std::abs(det) < 1e-8f)
            return false;
        const float px = x - tx, py = y - ty;
        x = (d * px - c * py) / det;
        y = (a * py - b * px) / det;
        return std::isfinite(x) && std::isfinite(y);
    }
    bool IsFinite() const noexcept {
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d) && std::isfinite(tx) &&
               std::isfinite(ty);
    }
    static Affine2D Around(float px, float py, float angle, float sx = 1, float sy = 1, float dx = 0,
                           float dy = 0) noexcept {
        const float cosine = std::cos(angle), sine = std::sin(angle);
        Affine2D m{cosine * sx, sine * sx, -sine * sy, cosine * sy};
        m.tx = px + dx - m.a * px - m.c * py;
        m.ty = py + dy - m.b * px - m.d * py;
        return m;
    }
};
inline Affine2D operator*(const Affine2D& parent, const Affine2D& child) noexcept {
    return {parent.a * child.a + parent.c * child.b,
            parent.b * child.a + parent.d * child.b,
            parent.a * child.c + parent.c * child.d,
            parent.b * child.c + parent.d * child.d,
            parent.a * child.tx + parent.c * child.ty + parent.tx,
            parent.b * child.tx + parent.d * child.ty + parent.ty};
}
} // namespace HIKARI
