#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

#include "HIKARI_TerrainDataFlow.h"
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

// 閉じた区切りの列から符号付き距離の図を作る。線の近くは実距離、他は chamfer で広げ、内外は走査線の交差で決める。
void BuildLakeField(PreparedSpline& out) {
    SplineLakeField& lake = out.lake;
    const std::vector<SplineSection>& sections = out.sections;
    float minX = (std::numeric_limits<float>::max)();
    float minZ = minX;
    float maxX = -minX;
    float maxZ = -minX;
    for (const SplineSection& section : sections) {
        minX = (std::min)(minX, section.x);
        maxX = (std::max)(maxX, section.x);
        minZ = (std::min)(minZ, section.z);
        maxZ = (std::max)(maxZ, section.z);
    }
    const float margin = out.reach + 4.0f;
    const float extent = (std::max)(maxX - minX, maxZ - minZ) + 2.0f * margin;
    lake.cellMeters = (std::max)(2.0f, extent / 512.0f);
    lake.originX = static_cast<double>(minX) - margin;
    lake.originZ = static_cast<double>(minZ) - margin;
    lake.width = static_cast<uint32_t>((maxX - minX + 2.0f * margin) / lake.cellMeters) + 2u;
    lake.height = static_cast<uint32_t>((maxZ - minZ + 2.0f * margin) / lake.cellMeters) + 2u;
    const size_t count = static_cast<size_t>(lake.width) * lake.height;
    std::vector<float>& distance = lake.signedDistance;
    distance.assign(count, (std::numeric_limits<float>::max)());
    const auto cellX = [&](uint32_t x) {
        return static_cast<float>(lake.originX + x * static_cast<double>(lake.cellMeters));
    };
    const auto cellZ = [&](uint32_t z) {
        return static_cast<float>(lake.originZ + z * static_cast<double>(lake.cellMeters));
    };
    // 線の近くの升：線分への実距離。
    for (size_t index = 0u; index + 1u < sections.size(); ++index) {
        const SplineSection& a = sections[index];
        const SplineSection& b = sections[index + 1u];
        const float abX = b.x - a.x;
        const float abZ = b.z - a.z;
        const float inverse = 1.0f / (std::max)(abX * abX + abZ * abZ, 1.0e-6f);
        const auto toCell = [&](float value, double origin) {
            return static_cast<int64_t>(std::floor((value - origin) / lake.cellMeters));
        };
        const int64_t x0 = (std::max)(toCell((std::min)(a.x, b.x), lake.originX) - 1, int64_t{0});
        const int64_t x1 = (std::min)(toCell((std::max)(a.x, b.x), lake.originX) + 2, int64_t{lake.width} - 1);
        const int64_t z0 = (std::max)(toCell((std::min)(a.z, b.z), lake.originZ) - 1, int64_t{0});
        const int64_t z1 = (std::min)(toCell((std::max)(a.z, b.z), lake.originZ) + 2, int64_t{lake.height} - 1);
        for (int64_t z = z0; z <= z1; ++z) {
            for (int64_t x = x0; x <= x1; ++x) {
                const float px = cellX(static_cast<uint32_t>(x)) - a.x;
                const float pz = cellZ(static_cast<uint32_t>(z)) - a.z;
                const float t = std::clamp((px * abX + pz * abZ) * inverse, 0.0f, 1.0f);
                float& cell = distance[static_cast<size_t>(z) * lake.width + static_cast<size_t>(x)];
                cell = (std::min)(cell, std::hypot(px - abX * t, pz - abZ * t));
            }
        }
    }
    // 二度の走査で全体へ広げる。
    const float straight = lake.cellMeters;
    const float diagonal = lake.cellMeters * 1.41421356f;
    const auto relax = [&](size_t index, int64_t x, int64_t z, float step) {
        if (x >= 0 && z >= 0 && x < int64_t{lake.width} && z < int64_t{lake.height}) {
            distance[index] = (std::min)(distance[index],
                                         distance[static_cast<size_t>(z) * lake.width + static_cast<size_t>(x)] + step);
        }
    };
    for (int64_t z = 0; z < int64_t{lake.height}; ++z) {
        for (int64_t x = 0; x < int64_t{lake.width}; ++x) {
            const size_t index = static_cast<size_t>(z) * lake.width + static_cast<size_t>(x);
            relax(index, x - 1, z, straight);
            relax(index, x, z - 1, straight);
            relax(index, x - 1, z - 1, diagonal);
            relax(index, x + 1, z - 1, diagonal);
        }
    }
    for (int64_t z = int64_t{lake.height} - 1; z >= 0; --z) {
        for (int64_t x = int64_t{lake.width} - 1; x >= 0; --x) {
            const size_t index = static_cast<size_t>(z) * lake.width + static_cast<size_t>(x);
            relax(index, x + 1, z, straight);
            relax(index, x, z + 1, straight);
            relax(index, x + 1, z + 1, diagonal);
            relax(index, x - 1, z + 1, diagonal);
        }
    }
    // 内外：行ごとに線との交差を数える。外は負。
    std::vector<float> crossings{};
    for (uint32_t z = 0u; z < lake.height; ++z) {
        const float rowZ = cellZ(z);
        crossings.clear();
        for (size_t index = 0u; index + 1u < sections.size(); ++index) {
            const SplineSection& a = sections[index];
            const SplineSection& b = sections[index + 1u];
            if ((a.z <= rowZ) != (b.z <= rowZ)) {
                crossings.push_back(a.x + (rowZ - a.z) / (b.z - a.z) * (b.x - a.x));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        size_t next = 0u;
        bool inside = false;
        for (uint32_t x = 0u; x < lake.width; ++x) {
            const float columnX = cellX(x);
            while (next < crossings.size() && crossings[next] <= columnX) {
                inside = !inside;
                ++next;
            }
            if (!inside) {
                float& cell = distance[static_cast<size_t>(z) * lake.width + x];
                cell = -cell;
            }
        }
    }
}

// 岸を崩した距離。高さと材質で同じ値を使う。
float LakeDistance(const PreparedSpline& spline, double worldX, double worldZ) noexcept {
    const float distance = spline.lake.DistanceAt(worldX, worldZ);
    const float amount = spline.settings.shoreNoiseMeters;
    return amount > 0.0f ? distance + amount * LatticeValueNoise(worldX / kBankNoiseMeters, worldZ / kBankNoiseMeters,
                                                                 kBankNoiseSeed)
                         : distance;
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

float SplineLakeField::DistanceAt(double worldX, double worldZ) const noexcept {
    constexpr float kFarOutside = -1.0e6f;
    if (!Valid()) {
        return kFarOutside;
    }
    const double fx = (worldX - originX) / cellMeters;
    const double fz = (worldZ - originZ) / cellMeters;
    if (fx < 0.0 || fz < 0.0 || fx >= width - 1u || fz >= height - 1u) {
        return kFarOutside;
    }
    const auto x = static_cast<uint32_t>(fx);
    const auto z = static_cast<uint32_t>(fz);
    const auto tx = static_cast<float>(fx - x);
    const auto tz = static_cast<float>(fz - z);
    const float* top = signedDistance.data() + static_cast<size_t>(z) * width + x;
    const float* bottom = top + width;
    const float upper = top[0] + (top[1] - top[0]) * tx;
    const float lower = bottom[0] + (bottom[1] - bottom[0]) * tx;
    return upper + (lower - upper) * tz;
}

PreparedSpline PrepareSpline(const SplineLayerSettings& settings) {
    PreparedSpline out{};
    out.settings = settings;
    const std::vector<SplinePoint>& points = settings.points;
    const bool river = settings.kind == SplineKind::River;
    const bool lake = settings.kind == SplineKind::Lake;
    float widest = 0.0f;
    std::vector<Knot> knots{};
    knots.reserve(points.size());
    for (const SplinePoint& point : points) {
        knots.push_back({point.position.x, point.position.y, point.position.z, (std::max)(point.widthMeters, 0.5f)});
        widest = (std::max)(widest, knots.back()[3]);
    }
    const float edge = settings.edgeMeters + (river || lake ? settings.wetMeters : 0.0f);
    out.reach =
        lake ? edge + settings.falloffMeters + settings.shelfMeters + settings.shoreNoiseMeters + 3.5f
             : widest * 0.5f + edge + settings.falloffMeters + (river ? RiverBankMargin(widest * 0.5f) : 0.0f) + 3.5f;
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
    // 湖は 3 点から閉じる。それまでは線のまま。
    const bool closed = lake && knots.size() >= 3u;
    const auto at = [&](int64_t index) -> Knot {
        const int64_t last = static_cast<int64_t>(knots.size()) - 1;
        if (closed) {
            const int64_t count = last + 1;
            return knots[static_cast<size_t>((index % count + count) % count)];
        }
        if (index < 0) {
            return Mix(knots[0], knots[1], -1.0f);
        }
        if (index > last) {
            return Mix(knots[static_cast<size_t>(last)], knots[static_cast<size_t>(last - 1)], -1.0f);
        }
        return knots[static_cast<size_t>(index)];
    };
    const size_t segments = closed ? knots.size() : knots.size() - 1u;
    for (size_t segment = 0u; segment < segments; ++segment) {
        out.segmentFirst.push_back(static_cast<uint32_t>(out.sections.size()));
        const int64_t i = static_cast<int64_t>(segment);
        const Knot p0 = at(i - 1);
        const Knot p1 = at(i);
        const Knot p2 = at(i + 1);
        const Knot p3 = at(i + 2);
        const float chord = std::hypot(p2[0] - p1[0], p2[2] - p1[2]);
        const uint32_t steps =
            std::clamp(static_cast<uint32_t>(std::ceil(chord / kSectionMeters)), 1u, kMaximumSectionsPerSegment);
        for (uint32_t step = 0u; step < steps; ++step) {
            append(EvaluateCatmullRom(p0, p1, p2, p3, static_cast<float>(step) / static_cast<float>(steps)));
        }
    }
    append(closed ? knots.front() : knots.back());
    out.segmentFirst.push_back(static_cast<uint32_t>(out.sections.size()));
    if (lake) {
        // 水面は平ら。高さは最初の点が決める。
        for (SplineSection& section : out.sections) {
            section.y = knots.front()[1];
        }
    }
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
    if (closed) {
        out.lake.level = knots.front()[1];
        BuildLakeField(out);
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
    const SplineLayerSettings& settings = spline.settings;
    if (settings.kind == SplineKind::Lake) {
        // 深さが 0 なら地形を変えない（窪地へ水を張るだけ）。
        if (!spline.lake.Valid() || settings.depthMeters <= 0.0f) {
            std::fill(alpha.begin(), alpha.end(), 0.0f);
            return;
        }
        const double spacing = SampleSpacing(grid);
        for (uint32_t z = 0u; z < rect.height; ++z) {
            for (uint32_t x = 0u; x < rect.width; ++x) {
                const size_t index = static_cast<size_t>(z) * rect.width + x;
                const float distance = LakeDistance(spline, static_cast<double>(rect.x0 + x) * spacing,
                                                    static_cast<double>(rect.z0 + z) * spacing);
                if (distance >= 0.0f) {
                    values[index] =
                        spline.lake.level - settings.depthMeters * Smoothstep(0.0f, settings.shelfMeters, distance);
                } else {
                    values[index] = spline.lake.level - distance * kBankRise;
                    alpha[index] *= 1.0f - Smoothstep(0.0f, settings.falloffMeters, -distance);
                }
            }
        }
        return;
    }
    SplineField field{};
    BuildField(spline, grid, rect, field);
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
                           a.wetMeters == b.wetMeters && a.kind == b.kind && a.shelfMeters == b.shelfMeters &&
                           a.shoreNoiseMeters == b.shoreNoiseMeters;
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
    const double spacing = SampleSpacing(grid);
    for (const PreparedSpline& spline : splines) {
        const SplineLayerSettings& settings = spline.settings;
        if (settings.kind == SplineKind::Lake) {
            if (!spline.lake.Valid()) {
                continue;
            }
            const auto surface = static_cast<uint8_t>((std::min)(settings.surface, kTerrainControlMaterialCount - 1u));
            const auto edgeSurface =
                static_cast<uint8_t>((std::min)(settings.edgeSurface, kTerrainControlMaterialCount - 1u));
            for (uint32_t z = 0u; z < rect.height; ++z) {
                for (uint32_t x = 0u; x < rect.width; ++x) {
                    const size_t index = static_cast<size_t>(z) * rect.width + x;
                    ControlTexel texel = UnpackControlTexel(control[index]);
                    if (texel.handPainted) {
                        continue;
                    }
                    const float distance = LakeDistance(spline, static_cast<double>(rect.x0 + x) * spacing,
                                                        static_cast<double>(rect.z0 + z) * spacing);
                    if (distance < -spline.reach) {
                        continue;
                    }
                    if (settings.paint) {
                        const float bed = Smoothstep(0.0f, settings.edgeMeters, distance);
                        const float shore =
                            1.0f - Smoothstep(settings.edgeMeters * 0.5f, settings.edgeMeters, -distance);
                        if (bed > 0.0f) {
                            texel.baseMaterial = surface;
                            texel.overlayMaterial = edgeSurface;
                            texel.blend = static_cast<uint8_t>((1.0f - bed) * 255.0f + 0.5f);
                        } else if (shore > 0.0f) {
                            texel = PaintControlMaterial(texel, edgeSurface, shore);
                            texel.handPainted = false;
                        }
                    }
                    if (settings.wetMeters > 0.0f) {
                        const float wet = 1.0f - Smoothstep(0.0f, settings.wetMeters, -distance);
                        texel.wetness = (std::max)(texel.wetness, static_cast<uint8_t>(wet * 15.0f + 0.5f));
                    }
                    (void)PackControlTexel(texel, control[index]);
                }
            }
            continue;
        }
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

bool FindLakeFill(std::span<const float> heights, uint32_t width, uint32_t height, double originX, double originZ,
                  float cellMeters, double seedX, double seedZ, float minimumArea, LakeFill& out) {
    out = {};
    const size_t count = static_cast<size_t>(width) * height;
    if (width < 3u || height < 3u || heights.size() != count || cellMeters <= 0.0f) {
        return false;
    }
    const auto clampCell = [](double value, uint32_t size) {
        return static_cast<int64_t>(std::clamp(std::floor(value + 0.5), 0.0, static_cast<double>(size - 1u)));
    };
    int64_t x = clampCell((seedX - originX) / cellMeters, width);
    int64_t z = clampCell((seedZ - originZ) / cellMeters, height);
    const auto indexOf = [&](int64_t cx, int64_t cz) {
        return static_cast<size_t>(cz) * width + static_cast<size_t>(cx);
    };
    // 一番低い所まで下る。
    for (bool moved = true; moved;) {
        moved = false;
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            const int64_t nx = x + kFlowNeighborX[n];
            const int64_t nz = z + kFlowNeighborZ[n];
            if (nx >= 0 && nz >= 0 && nx < int64_t{width} && nz < int64_t{height} &&
                heights[indexOf(nx, nz)] < heights[indexOf(x, z)]) {
                x = nx;
                z = nz;
                moved = true;
            }
        }
    }
    // 低い順に広げる。今までの最高点より低い所へ出たら、そこが峠の向こう（あふれ出る高さが決まる）。
    using Entry = std::pair<float, size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open{};
    std::vector<uint8_t> visited(count, 0u);
    visited[indexOf(x, z)] = 1u;
    open.emplace(heights[indexOf(x, z)], indexOf(x, z));
    float rim = heights[indexOf(x, z)];
    while (!open.empty()) {
        const auto [cellHeight, index] = open.top();
        open.pop();
        if (cellHeight < rim) {
            break;
        }
        rim = cellHeight;
        const auto cx = static_cast<int64_t>(index % width);
        const auto cz = static_cast<int64_t>(index / width);
        if (cx == 0 || cz == 0 || cx == int64_t{width} - 1 || cz == int64_t{height} - 1) {
            break; // 外周へ届いた
        }
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            const size_t neighbor = indexOf(cx + kFlowNeighborX[n], cz + kFlowNeighborZ[n]);
            if (visited[neighbor] == 0u) {
                visited[neighbor] = 1u;
                open.emplace(heights[neighbor], neighbor);
            }
        }
    }
    // 水面は峠より少しだけ下（線が峠を越えないように）。水位より低くつながる升が湖。
    constexpr float kBelowRim = 0.05f;
    const float level = rim - kBelowRim;
    std::vector<uint8_t> water(count, 0u);
    std::vector<size_t> stack{indexOf(x, z)};
    if (heights[stack.back()] >= level) {
        return false;
    }
    water[stack.back()] = 1u;
    size_t cells = 0u;
    bool touchesBorder = false;
    while (!stack.empty()) {
        const size_t index = stack.back();
        stack.pop_back();
        ++cells;
        const auto cx = static_cast<int64_t>(index % width);
        const auto cz = static_cast<int64_t>(index / width);
        touchesBorder = touchesBorder || cx == 0 || cz == 0 || cx == int64_t{width} - 1 || cz == int64_t{height} - 1;
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            const int64_t nx = cx + kFlowNeighborX[n];
            const int64_t nz = cz + kFlowNeighborZ[n];
            if (nx < 0 || nz < 0 || nx >= int64_t{width} || nz >= int64_t{height}) {
                continue;
            }
            const size_t neighbor = indexOf(nx, nz);
            if (water[neighbor] == 0u && heights[neighbor] < level) {
                water[neighbor] = 1u;
                stack.push_back(neighbor);
            }
        }
    }
    const float area = static_cast<float>(cells) * cellMeters * cellMeters;
    if (touchesBorder || area < minimumArea) {
        return false;
    }
    // 岸の線：水の升の外周を Moore の方法でたどる。
    int64_t startX = -1;
    int64_t startZ = -1;
    for (int64_t cz = 0; cz < int64_t{height} && startX < 0; ++cz) {
        for (int64_t cx = 0; cx < int64_t{width}; ++cx) {
            if (water[indexOf(cx, cz)] != 0u) {
                startX = cx;
                startZ = cz;
                break;
            }
        }
    }
    static constexpr int kAroundX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int kAroundZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    std::vector<MATH::Vec2> trace{};
    int64_t cx = startX;
    int64_t cz = startZ;
    int direction = 0;
    const size_t limit = count;
    for (size_t step = 0u; step < limit; ++step) {
        trace.push_back(MATH::Vec2{static_cast<float>(originX + static_cast<double>(cx) * cellMeters),
                                   static_cast<float>(originZ + static_cast<double>(cz) * cellMeters)});
        bool found = false;
        for (int turn = 0; turn < 8; ++turn) {
            const int candidate = (direction + 6 + turn) % 8;
            const int64_t nx = cx + kAroundX[candidate];
            const int64_t nz = cz + kAroundZ[candidate];
            if (nx >= 0 && nz >= 0 && nx < int64_t{width} && nz < int64_t{height} && water[indexOf(nx, nz)] != 0u) {
                cx = nx;
                cz = nz;
                direction = candidate;
                found = true;
                break;
            }
        }
        if (!found || (cx == startX && cz == startZ)) {
            break;
        }
    }
    if (trace.size() < 3u) {
        return false;
    }
    // 制御点の数へ間引く（等間隔。曲線が滑らかに補う）。
    constexpr size_t kMaximumShorePoints = 48u;
    const size_t stride = (std::max)(trace.size() / kMaximumShorePoints, size_t{1});
    for (size_t index = 0u; index < trace.size(); index += stride) {
        out.shore.push_back(trace[index]);
    }
    out.level = level;
    out.areaSquareMeters = area;
    return out.shore.size() >= 3u;
}

} // namespace HIKARI::TERRAIN::DATA
