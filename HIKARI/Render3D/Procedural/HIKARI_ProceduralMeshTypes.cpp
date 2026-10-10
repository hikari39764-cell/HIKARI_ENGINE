#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace HIKARI {

void FitProceduralSlopeBox(const ProceduralMeshSettings& settings, MATH::Vec3& center, MATH::Vec3& rotationDegrees,
                           MATH::Vec3& size) noexcept {
    const float length = std::sqrt(settings.height * settings.height + settings.depth * settings.depth);
    const float thickness = (std::max)(0.1f, (std::min)(settings.height, settings.depth) * 0.5f);
    const MATH::Vec3 normal{0.0f, settings.depth / length, -settings.height / length};
    center = normal * (-0.5f * thickness);
    rotationDegrees = {-std::atan2(settings.height, settings.depth) * 57.2957795f, 0.0f, 0.0f};
    size = {settings.width, thickness, length};
}

const char* GetProceduralMeshDisplayName(ProceduralMeshKind kind) noexcept {
    return kind == ProceduralMeshKind::GridPlane ? "Grid Plane"
                                                 : TEXT::EnumToName(kProceduralMeshKindNames, kind, "Box");
}

ProceduralMeshSettings SanitizeProceduralMeshSettings(ProceduralMeshSettings settings) noexcept {
    const auto finiteDimension = [](float value) {
        const float clamped = std::clamp(std::isfinite(value) ? value : 1.0f, 0.001f, 100000.0f);
        // mm へ丸めてから 1000 で割る（0.001f を掛けると 3 が 3.0000002 になる）。
        return std::round(clamped * 1000.0f) / 1000.0f;
    };
    settings.width = finiteDimension(settings.width);
    settings.height = finiteDimension(settings.height);
    settings.depth = finiteDimension(settings.depth);
    settings.segmentsX = std::clamp(settings.segmentsX, 1u, 512u);
    settings.segmentsY = std::clamp(settings.segmentsY, 1u, 512u);
    settings.segmentsZ = std::clamp(settings.segmentsZ, 1u, 64u);
    settings.sphereSlices = std::clamp(settings.sphereSlices, 3u, 512u);
    settings.sphereStacks = std::clamp(settings.sphereStacks, 2u, 256u);
    const auto finiteTileCount = [](float value) noexcept {
        if (!std::isfinite(value) || value <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(value, 0.001f, 4096.0f);
    };
    settings.uvTilesX = finiteTileCount(settings.uvTilesX);
    settings.uvTilesY = finiteTileCount(settings.uvTilesY);

    switch (settings.kind) {
    case ProceduralMeshKind::Plane:
        settings.segmentsX = 1u;
        settings.segmentsY = 1u;
        break;
    case ProceduralMeshKind::Sphere:
        settings.height = settings.width;
        settings.depth = settings.width;
        break;
    case ProceduralMeshKind::Cylinder: settings.depth = settings.width; break;
    case ProceduralMeshKind::Capsule:
        settings.depth = settings.width;
        settings.height = (std::max)(settings.height, settings.width);
        break;
    case ProceduralMeshKind::Stairs: settings.segmentsY = (std::min)(settings.segmentsY, 64u); break;
    default: break;
    }
    if (settings.kind != ProceduralMeshKind::GridPlane) {
        settings.uvTilesX = 0.0f;
        settings.uvTilesY = 0.0f;
    }
    // 切り抜きは箱だけ。箱と重ならない物と、mm より細い物は捨てる。
    if (settings.kind != ProceduralMeshKind::Box) {
        settings.cuts.clear();
    }
    const MATH::Vec3 half{settings.width * 0.5f, settings.height * 0.5f, settings.depth * 0.5f};
    const auto millimetre = [](float value) {
        return std::round(std::clamp(std::isfinite(value) ? value : 0.0f, -100000.0f, 100000.0f) * 1000.0f) / 1000.0f;
    };
    std::vector<ProceduralMeshCut> cuts;
    for (const ProceduralMeshCut& cut : settings.cuts) {
        ProceduralMeshCut sanitized{{millimetre(cut.center.x), millimetre(cut.center.y), millimetre(cut.center.z)},
                                    {millimetre(cut.size.x), millimetre(cut.size.y), millimetre(cut.size.z)}};
        // 丸は軸に沿って貫き、残り二軸は同じ直径で、外接する正方形が面の内側に収まる。
        if (cut.roundAxis >= 0 && cut.roundAxis <= 2) {
            const int axis = cut.roundAxis;
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            const auto at = [](MATH::Vec3& value, int index) -> float& {
                return index == 0 ? value.x : (index == 1 ? value.y : value.z);
            };
            MATH::Vec3 extent{settings.width, settings.height, settings.depth};
            const float diameter =
                std::clamp(at(sanitized.size, u), 0.01f, (std::min)(at(extent, u), at(extent, v)) * 0.98f);
            at(sanitized.size, u) = diameter;
            at(sanitized.size, v) = diameter;
            at(sanitized.size, axis) = at(extent, axis) + 0.2f;
            at(sanitized.center, axis) = 0.0f;
            for (const int inPlane : {u, v}) {
                const float limit = (at(extent, inPlane) - diameter) * 0.5f;
                at(sanitized.center, inPlane) = std::clamp(at(sanitized.center, inPlane), -limit, limit);
            }
            sanitized.roundAxis = static_cast<int8_t>(axis);
        }
        const bool overlaps = std::abs(sanitized.center.x) < half.x + sanitized.size.x * 0.5f &&
                              std::abs(sanitized.center.y) < half.y + sanitized.size.y * 0.5f &&
                              std::abs(sanitized.center.z) < half.z + sanitized.size.z * 0.5f;
        if (overlaps && sanitized.size.x >= 0.001f && sanitized.size.y >= 0.001f && sanitized.size.z >= 0.001f &&
            cuts.size() < kMaximumProceduralMeshCuts) {
            cuts.push_back(sanitized);
        }
    }
    settings.cuts = std::move(cuts);
    return settings;
}

int ProceduralCutGrid::CarvedBy(int x, int y, int z) const noexcept {
    if (x < 0 || y < 0 || z < 0 || x >= Count(0) || y >= Count(1) || z >= Count(2)) {
        return -2;
    }
    return carvedBy[static_cast<size_t>((z * Count(1) + y) * Count(0) + x)];
}

ProceduralCutGrid BuildProceduralCutGrid(const ProceduralMeshSettings& settings) {
    const std::array<float, 3> half{settings.width * 0.5f, settings.height * 0.5f, settings.depth * 0.5f};
    const auto component = [](const MATH::Vec3& value, size_t axis) {
        return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
    };
    ProceduralCutGrid grid{};
    for (size_t axis = 0; axis < 3; ++axis) {
        std::vector<float>& edges = grid.edges[axis];
        edges = {-half[axis], half[axis]};
        for (const ProceduralMeshCut& cut : settings.cuts) {
            for (const float sign : {-1.0f, 1.0f}) {
                const float edge = component(cut.center, axis) + sign * component(cut.size, axis) * 0.5f;
                if (edge > -half[axis] && edge < half[axis]) {
                    edges.push_back(edge);
                }
            }
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(
            std::unique(edges.begin(), edges.end(), [](float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-5f; }),
            edges.end());
    }
    grid.carvedBy.assign(static_cast<size_t>(grid.Count(0) * grid.Count(1) * grid.Count(2)), int16_t{-1});
    for (int z = 0; z < grid.Count(2); ++z) {
        for (int y = 0; y < grid.Count(1); ++y) {
            for (int x = 0; x < grid.Count(0); ++x) {
                const MATH::Vec3 center{(grid.edges[0][x] + grid.edges[0][x + 1]) * 0.5f,
                                        (grid.edges[1][y] + grid.edges[1][y + 1]) * 0.5f,
                                        (grid.edges[2][z] + grid.edges[2][z + 1]) * 0.5f};
                for (size_t index = 0; index < settings.cuts.size(); ++index) {
                    const ProceduralMeshCut& cut = settings.cuts[index];
                    if (std::abs(center.x - cut.center.x) < cut.size.x * 0.5f &&
                        std::abs(center.y - cut.center.y) < cut.size.y * 0.5f &&
                        std::abs(center.z - cut.center.z) < cut.size.z * 0.5f) {
                        grid.carvedBy[static_cast<size_t>((z * grid.Count(1) + y) * grid.Count(0) + x)] =
                            static_cast<int16_t>(index);
                        break;
                    }
                }
            }
        }
    }
    return grid;
}

std::vector<ProceduralMeshBox> DecomposeProceduralCutBox(const ProceduralMeshSettings& settings) {
    const ProceduralCutGrid grid = BuildProceduralCutGrid(settings);
    // 残った升を X、Y、Z の順に伸ばせるだけ伸ばした箱にまとめる。
    std::vector<uint8_t> used(grid.carvedBy.size(), 0u);
    const auto index = [&](int x, int y, int z) {
        return static_cast<size_t>((z * grid.Count(1) + y) * grid.Count(0) + x);
    };
    const auto available = [&](int x, int y, int z) { return grid.IsSolid(x, y, z) && used[index(x, y, z)] == 0u; };
    std::vector<ProceduralMeshBox> boxes;
    for (int z = 0; z < grid.Count(2); ++z) {
        for (int y = 0; y < grid.Count(1); ++y) {
            for (int x = 0; x < grid.Count(0); ++x) {
                if (!available(x, y, z)) {
                    continue;
                }
                int x1 = x + 1;
                while (available(x1, y, z)) {
                    ++x1;
                }
                const auto rowAvailable = [&](int row, int slice) {
                    for (int cell = x; cell < x1; ++cell) {
                        if (!available(cell, row, slice)) {
                            return false;
                        }
                    }
                    return true;
                };
                int y1 = y + 1;
                while (y1 < grid.Count(1) && rowAvailable(y1, z)) {
                    ++y1;
                }
                const auto sliceAvailable = [&](int slice) {
                    for (int row = y; row < y1; ++row) {
                        if (!rowAvailable(row, slice)) {
                            return false;
                        }
                    }
                    return true;
                };
                int z1 = z + 1;
                while (z1 < grid.Count(2) && sliceAvailable(z1)) {
                    ++z1;
                }
                for (int slice = z; slice < z1; ++slice) {
                    for (int row = y; row < y1; ++row) {
                        for (int cell = x; cell < x1; ++cell) {
                            used[index(cell, row, slice)] = 1u;
                        }
                    }
                }
                const MATH::Vec3 minimum{grid.edges[0][x], grid.edges[1][y], grid.edges[2][z]};
                const MATH::Vec3 maximum{grid.edges[0][x1], grid.edges[1][y1], grid.edges[2][z1]};
                boxes.push_back({(minimum + maximum) * 0.5f, maximum - minimum});
            }
        }
    }
    // 丸い穴の四隅：v を四つの帯に分け、帯の下端での円の幅より外側だけを箱にする（円に食い込まない）。
    constexpr int kBands = 4;
    const MATH::Vec3 extent{settings.width, settings.height, settings.depth};
    for (const ProceduralMeshCut& cut : settings.cuts) {
        if (cut.roundAxis < 0) {
            continue;
        }
        const int axis = cut.roundAxis;
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        const auto get = [](const MATH::Vec3& value, int index) {
            return index == 0 ? value.x : (index == 1 ? value.y : value.z);
        };
        const float radius = get(cut.size, u) * 0.5f;
        for (const float signU : {-1.0f, 1.0f}) {
            for (const float signV : {-1.0f, 1.0f}) {
                for (int band = 1; band < kBands; ++band) {
                    const float v0 = radius * static_cast<float>(band) / static_cast<float>(kBands);
                    const float v1 = radius * static_cast<float>(band + 1) / static_cast<float>(kBands);
                    const float u0 = std::sqrt((std::max)(radius * radius - v0 * v0, 0.0f));
                    ProceduralMeshBox box{};
                    float center[3]{cut.center.x, cut.center.y, cut.center.z};
                    float size[3]{};
                    center[axis] = 0.0f;
                    size[axis] = get(extent, axis);
                    center[u] += signU * (u0 + radius) * 0.5f;
                    size[u] = radius - u0;
                    center[v] += signV * (v0 + v1) * 0.5f;
                    size[v] = v1 - v0;
                    box.center = {center[0], center[1], center[2]};
                    box.size = {size[0], size[1], size[2]};
                    boxes.push_back(box);
                }
            }
        }
    }
    return boxes;
}

nlohmann::json WriteProceduralMeshCuts(const std::vector<ProceduralMeshCut>& cuts) {
    nlohmann::json out = nlohmann::json::array();
    for (const ProceduralMeshCut& cut : cuts) {
        nlohmann::json entry{{"center", {cut.center.x, cut.center.y, cut.center.z}},
                             {"size", {cut.size.x, cut.size.y, cut.size.z}}};
        if (cut.roundAxis >= 0) {
            entry["roundAxis"] = static_cast<int>(cut.roundAxis);
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<ProceduralMeshCut> ReadProceduralMeshCuts(const nlohmann::json& value) {
    std::vector<ProceduralMeshCut> cuts;
    if (!value.is_array()) {
        return cuts;
    }
    const auto vec3 = [](const nlohmann::json& node) {
        if (!node.is_array() || node.size() != 3u || !node[0].is_number() || !node[1].is_number() ||
            !node[2].is_number()) {
            return MATH::Vec3{};
        }
        return MATH::Vec3{node[0].get<float>(), node[1].get<float>(), node[2].get<float>()};
    };
    for (const nlohmann::json& entry : value) {
        if (entry.is_object()) {
            const int roundAxis = entry.value("roundAxis", -1);
            cuts.push_back({vec3(entry.value("center", nlohmann::json{})), vec3(entry.value("size", nlohmann::json{})),
                            static_cast<int8_t>(roundAxis >= 0 && roundAxis <= 2 ? roundAxis : -1)});
        }
    }
    return cuts;
}

} // namespace HIKARI
