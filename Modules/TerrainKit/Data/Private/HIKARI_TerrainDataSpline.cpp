#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "HIKARI_TerrainDataNoise.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr float kSectionMeters = 2.0f;
constexpr uint32_t kMaximumSectionsPerSegment = 512u;
constexpr uint32_t kBankNoiseSeed = 4817u;
constexpr double kBankNoiseMeters = 19.0;
// 河の中で、岸の外へ上がっていく傾き。
constexpr float kBankRise = 0.15f;

float Smoothstep(float edge0, float edge1, float value) noexcept {
    if (edge1 <= edge0) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

using Knot = std::array<float, 4>; // x, y, z, 幅

Knot Mix(const Knot& a, const Knot& b, float t) noexcept {
    Knot out{};
    for (size_t index = 0u; index < out.size(); ++index) {
        out[index] = a[index] + (b[index] - a[index]) * t;
    }
    return out;
}

float KnotStep(const Knot& a, const Knot& b) noexcept {
    const float dx = b[0] - a[0];
    const float dz = b[2] - a[2];
    return (std::max)(std::pow(dx * dx + dz * dz, 0.25f), 1.0e-3f); // 距離の平方根（向心）
}

// Barry-Goldman の向心 Catmull-Rom。p1 から p2 までを u = 0..1 で。
Knot EvaluateCatmullRom(const Knot& p0, const Knot& p1, const Knot& p2, const Knot& p3, float u) noexcept {
    const float t1 = KnotStep(p0, p1);
    const float t2 = t1 + KnotStep(p1, p2);
    const float t3 = t2 + KnotStep(p2, p3);
    const float t = t1 + (t2 - t1) * u;
    const Knot a1 = Mix(p0, p1, t / t1);
    const Knot a2 = Mix(p1, p2, (t - t1) / (t2 - t1));
    const Knot a3 = Mix(p2, p3, (t - t2) / (t3 - t2));
    const Knot b1 = Mix(a1, a2, t / t2);
    const Knot b2 = Mix(a2, a3, (t - t1) / (t3 - t1));
    return Mix(b1, b2, (t - t1) / (t2 - t1));
}

// 標本ごとの一番近い中心線の点。
struct SplineField {
    std::vector<float> distance{};
    std::vector<float> height{};
    std::vector<float> halfWidth{};
};

void BuildField(const PreparedSpline& spline, const TerrainGrid& grid, const SampleRect& rect, SplineField& field) {
    const size_t count = rect.Count();
    field.distance.assign(count, (std::numeric_limits<float>::max)());
    field.height.assign(count, 0.0f);
    field.halfWidth.assign(count, 0.0f);
    const auto& sections = spline.sections;
    const double spacing = SampleSpacing(grid);
    const auto visit = [&](const SplineSection& a, const SplineSection& b) {
        const double minX = (std::min)(a.x, b.x) - spline.reach;
        const double maxX = (std::max)(a.x, b.x) + spline.reach;
        const double minZ = (std::min)(a.z, b.z) - spline.reach;
        const double maxZ = (std::max)(a.z, b.z) + spline.reach;
        const int64_t x0 = (std::max)(static_cast<int64_t>(std::floor(minX / spacing)), rect.x0);
        const int64_t x1 = (std::min)(static_cast<int64_t>(std::ceil(maxX / spacing)), rect.X1());
        const int64_t z0 = (std::max)(static_cast<int64_t>(std::floor(minZ / spacing)), rect.z0);
        const int64_t z1 = (std::min)(static_cast<int64_t>(std::ceil(maxZ / spacing)), rect.Z1());
        const float abX = b.x - a.x;
        const float abZ = b.z - a.z;
        const float inverse = 1.0f / (std::max)(abX * abX + abZ * abZ, 1.0e-6f);
        for (int64_t z = z0; z <= z1; ++z) {
            const float worldZ = static_cast<float>(static_cast<double>(z) * spacing);
            const size_t row = static_cast<size_t>(z - rect.z0) * rect.width;
            for (int64_t x = x0; x <= x1; ++x) {
                const float worldX = static_cast<float>(static_cast<double>(x) * spacing);
                const float t = std::clamp(((worldX - a.x) * abX + (worldZ - a.z) * abZ) * inverse, 0.0f, 1.0f);
                const float dx = worldX - (a.x + abX * t);
                const float dz = worldZ - (a.z + abZ * t);
                const float distance = std::sqrt(dx * dx + dz * dz);
                const size_t index = row + static_cast<size_t>(x - rect.x0);
                if (distance < field.distance[index]) {
                    field.distance[index] = distance;
                    field.height[index] = a.y + (b.y - a.y) * t;
                    field.halfWidth[index] = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
                }
            }
        }
    };
    if (sections.size() == 1u) {
        visit(sections[0], sections[0]);
    }
    for (size_t index = 0u; index + 1u < sections.size(); ++index) {
        visit(sections[index], sections[index + 1u]);
    }
}

// 河の岸を噪声で崩す。高さと材質で同じ値を使う。
float RiverDistance(const SplineField& field, const TerrainGrid& grid, const SampleRect& rect, size_t index, uint32_t x,
                    uint32_t z) noexcept {
    const double spacing = SampleSpacing(grid);
    const double worldX = static_cast<double>(rect.x0 + x) * spacing;
    const double worldZ = static_cast<double>(rect.z0 + z) * spacing;
    const float noise = LatticeValueNoise(worldX / kBankNoiseMeters, worldZ / kBankNoiseMeters, kBankNoiseSeed);
    return (std::max)(field.distance[index] + noise * (std::min)(field.halfWidth[index] * 0.25f, 2.5f), 0.0f);
}

} // namespace

PreparedSpline PrepareSpline(const SplineLayerSettings& settings) {
    PreparedSpline out{};
    out.settings = settings;
    const std::vector<SplinePoint>& points = settings.points;
    const bool river = settings.kind == SplineKind::River;
    float widest = 0.0f;
    std::vector<Knot> knots{};
    knots.reserve(points.size());
    for (const SplinePoint& point : points) {
        knots.push_back({point.position.x, point.position.y, point.position.z, (std::max)(point.widthMeters, 0.5f)});
        widest = (std::max)(widest, knots.back()[3]);
    }
    const float edge = settings.edgeMeters + (river ? settings.wetMeters : 0.0f);
    out.reach = widest * 0.5f + edge + settings.falloffMeters + (river ? RiverBankMargin(widest * 0.5f) : 0.0f) + 3.5f;
    if (knots.empty()) {
        out.segmentFirst.push_back(0u);
        return out;
    }
    const auto append = [&](const Knot& knot) {
        SplineSection section{};
        section.x = knot[0];
        section.y = knot[1];
        section.z = knot[2];
        section.halfWidth = (std::max)(knot[3] * 0.5f, 0.25f);
        out.sections.push_back(section);
    };
    if (knots.size() == 1u) {
        out.segmentFirst.push_back(0u);
        append(knots[0]);
        out.segmentFirst.push_back(1u);
        return out;
    }
    const auto at = [&](int64_t index) -> Knot {
        const int64_t last = static_cast<int64_t>(knots.size()) - 1;
        if (index < 0) {
            return Mix(knots[0], knots[1], -1.0f);
        }
        if (index > last) {
            return Mix(knots[static_cast<size_t>(last)], knots[static_cast<size_t>(last - 1)], -1.0f);
        }
        return knots[static_cast<size_t>(index)];
    };
    for (size_t segment = 0u; segment + 1u < knots.size(); ++segment) {
        out.segmentFirst.push_back(static_cast<uint32_t>(out.sections.size()));
        const int64_t i = static_cast<int64_t>(segment);
        const Knot p0 = at(i - 1);
        const Knot& p1 = knots[segment];
        const Knot& p2 = knots[segment + 1u];
        const Knot p3 = at(i + 2);
        const float chord = std::hypot(p2[0] - p1[0], p2[2] - p1[2]);
        const uint32_t steps =
            std::clamp(static_cast<uint32_t>(std::ceil(chord / kSectionMeters)), 1u, kMaximumSectionsPerSegment);
        for (uint32_t step = 0u; step < steps; ++step) {
            append(EvaluateCatmullRom(p0, p1, p2, p3, static_cast<float>(step) / static_cast<float>(steps)));
        }
    }
    append(knots.back());
    out.segmentFirst.push_back(static_cast<uint32_t>(out.sections.size()));
    float along = 0.0f;
    float level = out.sections.front().y;
    for (size_t index = 0u; index < out.sections.size(); ++index) {
        SplineSection& section = out.sections[index];
        const SplineSection& previous = out.sections[index > 0u ? index - 1u : 0u];
        const SplineSection& next = out.sections[(std::min)(index + 1u, out.sections.size() - 1u)];
        along += std::hypot(section.x - previous.x, section.z - previous.z);
        section.along = along;
        const float tangentX = next.x - previous.x;
        const float tangentZ = next.z - previous.z;
        const float length = (std::max)(std::hypot(tangentX, tangentZ), 1.0e-5f);
        section.tangentX = tangentX / length;
        section.tangentZ = tangentZ / length;
        if (river) {
            level = (std::min)(level, section.y);
            section.y = level;
        }
    }
    return out;
}

void EvaluateSplineHeights(const PreparedSpline& spline, const TerrainGrid& grid, const SampleRect& rect,
                           std::vector<float>& values, std::vector<float>& alpha) {
    const size_t count = rect.Count();
    values.assign(count, 0.0f);
    alpha.resize(count, 0.0f);
    if (spline.sections.empty()) {
        std::fill(alpha.begin(), alpha.end(), 0.0f);
        return;
    }
    SplineField field{};
    BuildField(spline, grid, rect, field);
    const SplineLayerSettings& settings = spline.settings;
    const bool river = settings.kind == SplineKind::River;
    const float shoulder = settings.kind == SplineKind::Road ? settings.edgeMeters : 0.0f;
    for (uint32_t z = 0u; z < rect.height; ++z) {
        for (uint32_t x = 0u; x < rect.width; ++x) {
            const size_t index = static_cast<size_t>(z) * rect.width + x;
            if (field.distance[index] > spline.reach) {
                alpha[index] = 0.0f;
                continue;
            }
            const float half = field.halfWidth[index];
            if (river) {
                const float distance = RiverDistance(field, grid, rect, index, x, z);
                const float across = distance / half;
                values[index] = distance < half ? field.height[index] - settings.depthMeters * (1.0f - across * across)
                                                : field.height[index] + (distance - half) * kBankRise;
                const float bank = half + RiverBankMargin(half);
                alpha[index] *= 1.0f - Smoothstep(bank, bank + settings.falloffMeters, field.distance[index]);
            } else {
                values[index] = field.height[index];
                const float flat = half + shoulder;
                alpha[index] *= 1.0f - Smoothstep(flat, flat + settings.falloffMeters, field.distance[index]);
            }
        }
    }
}

namespace {

// 区切りの列の範囲を覆う標本の矩形を rect へ足す。
void IncludeSections(const PreparedSpline& spline, size_t first, size_t last, bool& any, double bounds[4]) {
    if (spline.sections.empty()) {
        return;
    }
    const size_t limit = (std::min)(last, spline.sections.size() - 1u);
    for (size_t index = first; index <= limit; ++index) {
        const SplineSection& section = spline.sections[index];
        const double minX = section.x - spline.reach;
        const double maxX = section.x + spline.reach;
        const double minZ = section.z - spline.reach;
        const double maxZ = section.z + spline.reach;
        if (!any) {
            bounds[0] = minX;
            bounds[1] = minZ;
            bounds[2] = maxX;
            bounds[3] = maxZ;
            any = true;
        } else {
            bounds[0] = (std::min)(bounds[0], minX);
            bounds[1] = (std::min)(bounds[1], minZ);
            bounds[2] = (std::max)(bounds[2], maxX);
            bounds[3] = (std::max)(bounds[3], maxZ);
        }
    }
}

SampleRect BoundsToRect(const TerrainGrid& grid, bool any, const double bounds[4]) {
    if (!any) {
        return SampleRect{};
    }
    const double spacing = SampleSpacing(grid);
    const int64_t x0 = static_cast<int64_t>(std::floor(bounds[0] / spacing)) - 1;
    const int64_t z0 = static_cast<int64_t>(std::floor(bounds[1] / spacing)) - 1;
    const int64_t x1 = static_cast<int64_t>(std::ceil(bounds[2] / spacing)) + 1;
    const int64_t z1 = static_cast<int64_t>(std::ceil(bounds[3] / spacing)) + 1;
    return SampleRect{x0, z0, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1)};
}

bool SameSection(const SplineSection& a, const SplineSection& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.halfWidth == b.halfWidth;
}

} // namespace

SampleRect SplineAffectedRect(const PreparedSpline& spline, const TerrainGrid& grid) {
    bool any = false;
    double bounds[4]{};
    if (!spline.sections.empty()) {
        IncludeSections(spline, 0u, spline.sections.size() - 1u, any, bounds);
    }
    return BoundsToRect(grid, any, bounds);
}

SampleRect SplineChangedRect(const PreparedSpline& before, const PreparedSpline& after, const TerrainGrid& grid) {
    const SplineLayerSettings& a = before.settings;
    const SplineLayerSettings& b = after.settings;
    const bool sameShape = a.falloffMeters == b.falloffMeters && a.edgeMeters == b.edgeMeters && a.paint == b.paint &&
                           a.surface == b.surface && a.edgeSurface == b.edgeSurface && a.depthMeters == b.depthMeters &&
                           a.wetMeters == b.wetMeters && a.kind == b.kind;
    bool any = false;
    double bounds[4]{};
    if (!sameShape || before.segmentFirst.size() != after.segmentFirst.size() || before.segmentFirst.size() < 2u) {
        if (!before.sections.empty()) {
            IncludeSections(before, 0u, before.sections.size() - 1u, any, bounds);
        }
        if (!after.sections.empty()) {
            IncludeSections(after, 0u, after.sections.size() - 1u, any, bounds);
        }
        return BoundsToRect(grid, any, bounds);
    }
    for (size_t segment = 0u; segment + 1u < before.segmentFirst.size(); ++segment) {
        const uint32_t beforeFirst = before.segmentFirst[segment];
        const uint32_t beforeEnd = before.segmentFirst[segment + 1u];
        const uint32_t afterFirst = after.segmentFirst[segment];
        const uint32_t afterEnd = after.segmentFirst[segment + 1u];
        bool same = beforeEnd - beforeFirst == afterEnd - afterFirst;
        for (uint32_t offset = 0u; same && offset < beforeEnd - beforeFirst; ++offset) {
            same = SameSection(before.sections[beforeFirst + offset], after.sections[afterFirst + offset]);
        }
        if (!same) {
            // 区間の終わりの次の区切りまで含める（線分が次の区間の始点へつながる）。
            IncludeSections(before, beforeFirst, beforeEnd, any, bounds);
            IncludeSections(after, afterFirst, afterEnd, any, bounds);
        }
    }
    return BoundsToRect(grid, any, bounds);
}

void PaintSplineSurfaces(std::span<const PreparedSpline> splines, const TerrainGrid& grid, const SampleRect& rect,
                         std::vector<uint32_t>& control) {
    if (control.size() != rect.Count()) {
        return;
    }
    SplineField field{};
    for (const PreparedSpline& spline : splines) {
        const SplineLayerSettings& settings = spline.settings;
        if (spline.sections.empty() || (!settings.paint && settings.kind != SplineKind::River)) {
            continue;
        }
        BuildField(spline, grid, rect, field);
        const bool river = settings.kind == SplineKind::River;
        const uint8_t surface = static_cast<uint8_t>((std::min)(settings.surface, kTerrainControlMaterialCount - 1u));
        const uint8_t edgeSurface =
            static_cast<uint8_t>((std::min)(settings.edgeSurface, kTerrainControlMaterialCount - 1u));
        for (uint32_t z = 0u; z < rect.height; ++z) {
            for (uint32_t x = 0u; x < rect.width; ++x) {
                const size_t index = static_cast<size_t>(z) * rect.width + x;
                if (field.distance[index] > spline.reach) {
                    continue;
                }
                ControlTexel texel = UnpackControlTexel(control[index]);
                if (texel.handPainted) {
                    continue;
                }
                const float half = field.halfWidth[index];
                const float distance = river ? RiverDistance(field, grid, rect, index, x, z) : field.distance[index];
                if (settings.paint) {
                    const float soft = (std::min)(half * 0.3f, 0.75f);
                    const float inner = 1.0f - Smoothstep(half - soft, half + soft, distance);
                    if (inner >= 0.995f) {
                        texel.baseMaterial = surface;
                        texel.overlayMaterial = surface;
                        texel.blend = 0u;
                    } else if (inner > 0.0f) {
                        texel.baseMaterial = surface;
                        texel.overlayMaterial = edgeSurface;
                        texel.blend = static_cast<uint8_t>((1.0f - inner) * 255.0f + 0.5f);
                    } else if (settings.edgeMeters > 0.0f) {
                        const float outer = 1.0f - Smoothstep(half + settings.edgeMeters * 0.4f,
                                                              half + settings.edgeMeters + soft, distance);
                        if (outer > 0.0f) {
                            texel = PaintControlMaterial(texel, edgeSurface, outer);
                            texel.handPainted = false;
                        }
                    }
                }
                if (river && settings.wetMeters > 0.0f) {
                    const float wet = 1.0f - Smoothstep(half, half + settings.wetMeters, distance);
                    texel.wetness = (std::max)(texel.wetness, static_cast<uint8_t>(wet * 15.0f + 0.5f));
                }
                (void)PackControlTexel(texel, control[index]);
            }
        }
    }
}

} // namespace HIKARI::TERRAIN::DATA
