#include "Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace HIKARI::ASSETS::LIGHTING {

namespace {

void AddIssue(LightmapSurfaceAnalysis& analysis, LightmapSurfaceIssue issue) {
    analysis.issueMask |= static_cast<uint32_t>(issue);
}

bool IsFiniteUv(const MATH::Vec2& uv) { return std::isfinite(uv.x) && std::isfinite(uv.y); }

bool IsInsideUnitRange(const MATH::Vec2& uv) {
    constexpr float kTolerance = 0.0001f;
    return uv.x >= -kTolerance && uv.y >= -kTolerance && uv.x <= 1.0f + kTolerance && uv.y <= 1.0f + kTolerance;
}

bool IsDegenerateUvTriangle(const MATH::Vec2& uv0, const MATH::Vec2& uv1, const MATH::Vec2& uv2) {
    const MATH::Vec2 edge0 = uv1 - uv0;
    const MATH::Vec2 edge1 = uv2 - uv0;
    const float twiceArea = edge0.x * edge1.y - edge0.y * edge1.x;
    return std::abs(twiceArea) <= 1.0e-8f;
}

using UvTriangle = std::array<MATH::Vec2, 3>;
using WorldTriangle = std::array<MATH::Vec3, 3>;

bool HasSeparatingUvEdge(const UvTriangle& axes, const UvTriangle& other) {
    for (size_t edge = 0u; edge < 3u; ++edge) {
        const auto& a = axes[edge];
        const auto& b = axes[(edge + 1u) % 3u];
        const double nx = static_cast<double>(a.y) - b.y;
        const double ny = static_cast<double>(b.x) - a.x;
        const auto project = [nx, ny](const MATH::Vec2& point) { return nx * point.x + ny * point.y; };
        double firstMin = project(axes[0]), firstMax = firstMin;
        double secondMin = project(other[0]), secondMax = secondMin;
        for (size_t corner = 1u; corner < 3u; ++corner) {
            const double first = project(axes[corner]);
            const double second = project(other[corner]);
            firstMin = (std::min)(firstMin, first);
            firstMax = (std::max)(firstMax, first);
            secondMin = (std::min)(secondMin, second);
            secondMax = (std::max)(secondMax, second);
        }
        // 面積を持たない共有辺・共有頂点は重複ではない。Texel サイズには依存させない。
        const double tolerance = 1.0e-12 * (std::abs(nx) + std::abs(ny));
        if (firstMax <= secondMin + tolerance || secondMax <= firstMin + tolerance) {
            return true;
        }
    }
    return false;
}

// p の重心座標（UV 三角形の上で）。面積が無ければ偽。
bool UvBarycentric(const UvTriangle& uv, const MATH::Vec2& p, double (&weights)[3]) {
    const auto edge = [](const MATH::Vec2& a, const MATH::Vec2& b, const MATH::Vec2& q) {
        return (static_cast<double>(b.x) - a.x) * (static_cast<double>(q.y) - a.y) -
               (static_cast<double>(b.y) - a.y) * (static_cast<double>(q.x) - a.x);
    };
    const double area = edge(uv[0], uv[1], uv[2]);
    if (area == 0.0)
        return false;
    weights[0] = edge(uv[1], uv[2], p) / area;
    weights[1] = edge(uv[2], uv[0], p) / area;
    weights[2] = edge(uv[0], uv[1], p) / area;
    return true;
}

MATH::Vec3 WorldAt(const WorldTriangle& world, const double (&weights)[3]) {
    return world[0] * static_cast<float>(weights[0]) + world[1] * static_cast<float>(weights[1]) +
           world[2] * static_cast<float>(weights[2]);
}

// UV で重なる二つの三角形が世界空間でも同じ所にあるか（同じ面の重複、対角線の違う二重の四角形など）。そうなら texel を
// 共有しても照明は正しい。重なる領域の頂点（相手の内側にある頂点と辺どうしの交点）を両方の三角形で世界座標へ戻して
// 比べる。UV から世界への写像は三角形ごとに一次なので、領域の頂点で一致すれば領域全体で一致する。向きの違う面
// （裏表の重複）は別の面として重なりに数える。
bool IsCoincidentOverlap(const UvTriangle& uvA, const WorldTriangle& worldA, const UvTriangle& uvB,
                         const WorldTriangle& worldB) {
    const MATH::Vec3 normalA = MATH::Cross(worldA[1] - worldA[0], worldA[2] - worldA[0]);
    const MATH::Vec3 normalB = MATH::Cross(worldB[1] - worldB[0], worldB[2] - worldB[0]);
    if (MATH::Dot(normalA, normalB) <= 0.5f * std::sqrt(MATH::Dot(normalA, normalA) * MATH::Dot(normalB, normalB)))
        return false;
    float longestEdge = 0.0f;
    for (const WorldTriangle* world : {&worldA, &worldB})
        for (size_t corner = 0u; corner < 3u; ++corner) {
            const MATH::Vec3 edge = (*world)[(corner + 1u) % 3u] - (*world)[corner];
            longestEdge = (std::max)(longestEdge, std::sqrt(MATH::Dot(edge, edge)));
        }
    const float tolerance = 0.02f * longestEdge;

    std::vector<MATH::Vec2> points;
    const auto inside = [](const UvTriangle& uv, const MATH::Vec2& p) {
        double weights[3];
        return UvBarycentric(uv, p, weights) && weights[0] >= -1.0e-6 && weights[1] >= -1.0e-6 && weights[2] >= -1.0e-6;
    };
    for (size_t corner = 0u; corner < 3u; ++corner) {
        if (inside(uvB, uvA[corner]))
            points.push_back(uvA[corner]);
        if (inside(uvA, uvB[corner]))
            points.push_back(uvB[corner]);
    }
    for (size_t i = 0u; i < 3u; ++i)
        for (size_t j = 0u; j < 3u; ++j) {
            const MATH::Vec2 p = uvA[i], r = uvA[(i + 1u) % 3u] - uvA[i];
            const MATH::Vec2 q = uvB[j], s = uvB[(j + 1u) % 3u] - uvB[j];
            const double denominator = static_cast<double>(r.x) * s.y - static_cast<double>(r.y) * s.x;
            if (denominator == 0.0)
                continue;
            const MATH::Vec2 qp = q - p;
            const double u = (static_cast<double>(qp.x) * s.y - static_cast<double>(qp.y) * s.x) / denominator;
            const double v = (static_cast<double>(qp.x) * r.y - static_cast<double>(qp.y) * r.x) / denominator;
            if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0)
                points.push_back({p.x + static_cast<float>(u) * r.x, p.y + static_cast<float>(u) * r.y});
        }
    for (const MATH::Vec2& point : points) {
        double weightsA[3], weightsB[3];
        if (!UvBarycentric(uvA, point, weightsA) || !UvBarycentric(uvB, point, weightsB))
            return false;
        const MATH::Vec3 offset = WorldAt(worldA, weightsA) - WorldAt(worldB, weightsB);
        if (MATH::Dot(offset, offset) > tolerance * tolerance)
            return false;
    }
    return true;
}

bool HasOverlappingUvTriangles(const std::vector<UvTriangle>& triangles, const std::vector<WorldTriangle>& world) {
    // Grid は候補の絞り込み専用。小島でも交差判定そのものは幾何学的に行う。
    constexpr int kGridSize = 32;
    std::array<std::vector<size_t>, kGridSize * kGridSize> cells{};
    std::vector<size_t> visited(triangles.size(), triangles.size());
    struct Bounds2D {
        double minX, minY, maxX, maxY;
    };
    std::vector<Bounds2D> bounds;
    bounds.reserve(triangles.size());
    const auto gridCell = [](double uv) {
        return std::clamp(static_cast<int>(std::floor(uv * kGridSize)), 0, kGridSize - 1);
    };
    for (size_t index = 0u; index < triangles.size(); ++index) {
        const auto& triangle = triangles[index];
        const Bounds2D box{(std::min)({triangle[0].x, triangle[1].x, triangle[2].x}),
                           (std::min)({triangle[0].y, triangle[1].y, triangle[2].y}),
                           (std::max)({triangle[0].x, triangle[1].x, triangle[2].x}),
                           (std::max)({triangle[0].y, triangle[1].y, triangle[2].y})};
        const int x0 = gridCell(box.minX), x1 = gridCell(box.maxX);
        const int y0 = gridCell(box.minY), y1 = gridCell(box.maxY);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                auto& cell = cells[y * kGridSize + x];
                for (const size_t candidate : cell) {
                    if (visited[candidate] == index)
                        continue;
                    visited[candidate] = index;
                    const auto& previous = bounds[candidate];
                    if (box.maxX <= previous.minX || previous.maxX <= box.minX || box.maxY <= previous.minY ||
                        previous.maxY <= box.minY)
                        continue;
                    if (!HasSeparatingUvEdge(triangle, triangles[candidate]) &&
                        !HasSeparatingUvEdge(triangles[candidate], triangle) &&
                        !IsCoincidentOverlap(triangle, world[index], triangles[candidate], world[candidate]))
                        return true;
                }
                cell.push_back(index);
            }
        bounds.push_back(box);
    }
    return false;
}

void AppendIssue(std::vector<std::string>& issues, bool present, const char* text) {
    if (present) {
        issues.emplace_back(text);
    }
}

} // namespace

LightmapSurfaceAnalysis AnalyzeLightmapSurface(const MeshPrimitive& primitive, AlphaMode alphaMode) {
    LightmapSurfaceAnalysis analysis{};
    if (primitive.layout != VertexLayoutKind::StaticPNTT) {
        AddIssue(analysis, LightmapSurfaceIssue::UnsupportedVertexLayout);
    }
    if (primitive.hasMorphTargets) {
        AddIssue(analysis, LightmapSurfaceIssue::MorphTargetsUnsupported);
    }
    if (!primitive.hasLightmapUv) {
        AddIssue(analysis, LightmapSurfaceIssue::MissingLightmapUv);
    }
    if (primitive.staticVertices.empty() || primitive.indices.size() < 3u) {
        AddIssue(analysis, LightmapSurfaceIssue::MissingGeometry);
    }
    if (alphaMode == AlphaMode::Mask) {
        AddIssue(analysis, LightmapSurfaceIssue::AlphaMaskedSurface);
    } else if (alphaMode == AlphaMode::Blend) {
        AddIssue(analysis, LightmapSurfaceIssue::BlendedSurface);
    }

    if (analysis.HasIssue(LightmapSurfaceIssue::UnsupportedVertexLayout) ||
        analysis.HasIssue(LightmapSurfaceIssue::MissingGeometry) ||
        analysis.HasIssue(LightmapSurfaceIssue::MissingLightmapUv) ||
        analysis.HasIssue(LightmapSurfaceIssue::AlphaMaskedSurface) ||
        analysis.HasIssue(LightmapSurfaceIssue::BlendedSurface)) {
        return analysis;
    }

    // UV で潰れた三角形が問題になるのは、形状の中で見える大きさを持つ時だけ。面取りの細い三角形のように world でも
    // ごく小さい三角形は自動展開で UV が潰れても一点の texel を使うだけで、焼き込みから外す理由にならない
    // （以前は一つでもあると形状ごと外れ、暖房器などが烘焙されなかった）。
    double worldArea = 0.0;
    const auto triangleWorldArea = [&](size_t index) {
        const MATH::Vec3& a = primitive.staticVertices[primitive.indices[index]].position;
        const MATH::Vec3 ab = primitive.staticVertices[primitive.indices[index + 1u]].position - a;
        const MATH::Vec3 ac = primitive.staticVertices[primitive.indices[index + 2u]].position - a;
        const MATH::Vec3 cross = MATH::Cross(ab, ac);
        return 0.5 * std::sqrt(static_cast<double>(MATH::Dot(cross, cross)));
    };
    for (size_t index = 0u; index + 2u < primitive.indices.size(); index += 3u)
        if (primitive.indices[index] < primitive.staticVertices.size() &&
            primitive.indices[index + 1u] < primitive.staticVertices.size() &&
            primitive.indices[index + 2u] < primitive.staticVertices.size())
            worldArea += triangleWorldArea(index);
    std::vector<UvTriangle> uvTriangles;
    std::vector<WorldTriangle> worldTriangles;
    uvTriangles.reserve(primitive.indices.size() / 3u);
    worldTriangles.reserve(primitive.indices.size() / 3u);
    for (size_t index = 0u; index + 2u < primitive.indices.size(); index += 3u) {
        const uint32_t indices[3]{primitive.indices[index], primitive.indices[index + 1u],
                                  primitive.indices[index + 2u]};
        if (indices[0] >= primitive.staticVertices.size() || indices[1] >= primitive.staticVertices.size() ||
            indices[2] >= primitive.staticVertices.size()) {
            AddIssue(analysis, LightmapSurfaceIssue::IndexOutOfRange);
            continue;
        }

        const MATH::Vec2 uv[3]{primitive.staticVertices[indices[0]].uv1, primitive.staticVertices[indices[1]].uv1,
                               primitive.staticVertices[indices[2]].uv1};
        bool finite = true;
        for (const MATH::Vec2& corner : uv) {
            if (!IsFiniteUv(corner)) {
                AddIssue(analysis, LightmapSurfaceIssue::UvNonFinite);
                finite = false;
            } else if (!IsInsideUnitRange(corner)) {
                AddIssue(analysis, LightmapSurfaceIssue::UvOutsideUnitRange);
            }
        }
        if (finite && IsDegenerateUvTriangle(uv[0], uv[1], uv[2])) {
            if (triangleWorldArea(index) > 1.0e-4 * worldArea)
                AddIssue(analysis, LightmapSurfaceIssue::UvDegenerate);
        } else if (finite && !analysis.HasIssue(LightmapSurfaceIssue::UvOutsideUnitRange)) {
            uvTriangles.push_back({uv[0], uv[1], uv[2]});
            worldTriangles.push_back({primitive.staticVertices[indices[0]].position,
                                      primitive.staticVertices[indices[1]].position,
                                      primitive.staticVertices[indices[2]].position});
        }
        ++analysis.triangleCount;
    }
    if (HasOverlappingUvTriangles(uvTriangles, worldTriangles)) {
        AddIssue(analysis, LightmapSurfaceIssue::UvOverlap);
    }
    return analysis;
}

std::string DescribeLightmapSurfaceAnalysis(const LightmapSurfaceAnalysis& analysis) {
    if (analysis.IsEligible()) {
        return "Ready";
    }

    std::vector<std::string> issues{};
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::UnsupportedVertexLayout), "unsupported vertex layout");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::MorphTargetsUnsupported),
                "morph targets are not bakeable");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::MissingGeometry), "missing triangle geometry");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::MissingLightmapUv), "lightmap UV is missing");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::IndexOutOfRange), "index out of range");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::UvNonFinite), "UV1 is not finite");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::UvOutsideUnitRange), "UV1 is outside [0, 1]");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::UvDegenerate), "UV1 contains degenerate triangles");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::UvOverlap), "UV1 contains overlapping islands");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::AlphaMaskedSurface),
                "alpha-masked surface needs DXR any-hit");
    AppendIssue(issues, analysis.HasIssue(LightmapSurfaceIssue::BlendedSurface), "blended surface is excluded");

    std::string description{};
    for (const std::string& issue : issues) {
        if (!description.empty()) {
            description += "; ";
        }
        description += issue;
    }
    return description.empty() ? "No bakeable triangles" : description;
}

} // namespace HIKARI::ASSETS::LIGHTING
