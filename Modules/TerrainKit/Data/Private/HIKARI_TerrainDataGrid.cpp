#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::TERRAIN::DATA {

bool ValidateTerrainGrid(const TerrainGrid& grid, std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!std::isfinite(grid.tileSizeMeters) || grid.tileSizeMeters < 16.0f || grid.tileSizeMeters > 4096.0f) {
        return fail("grid.tileSizeMeters must be within 16..4096 m");
    }
    if (std::find(std::begin(kTerrainTileCellChoices), std::end(kTerrainTileCellChoices), grid.tileCells) ==
        std::end(kTerrainTileCellChoices)) {
        return fail("grid.tileCells must be 64, 128, 256 or 512");
    }
    if (!std::isfinite(grid.heightMin) || !std::isfinite(grid.heightRange) || grid.heightRange < 1.0f) {
        return fail("grid.heightMin and grid.heightRange must be finite and the range at least 1 m");
    }
    return true;
}

TileCoord TileOfWorld(const TerrainGrid& grid, double worldX, double worldZ) noexcept {
    const double size = grid.tileSizeMeters;
    return TileCoord{static_cast<int32_t>(std::floor(worldX / size)), static_cast<int32_t>(std::floor(worldZ / size))};
}

SampleRect TileSampleRect(const TerrainGrid& grid, TileCoord tile) noexcept {
    return SampleRect{TileFirstSample(grid, tile.x), TileFirstSample(grid, tile.z), TileSamples(grid),
                      TileSamples(grid)};
}

SampleRect SampleRectAround(const TerrainGrid& grid, double worldX, double worldZ, double radius) noexcept {
    const double spacing = SampleSpacing(grid);
    const auto lowSample = [&](double value) { return static_cast<int64_t>(std::floor((value - radius) / spacing)); };
    const auto highSample = [&](double value) { return static_cast<int64_t>(std::ceil((value + radius) / spacing)); };
    const int64_t x0 = lowSample(worldX);
    const int64_t z0 = lowSample(worldZ);
    const int64_t x1 = highSample(worldX);
    const int64_t z1 = highSample(worldZ);
    if (x1 < x0 || z1 < z0) {
        return {};
    }
    return SampleRect{x0, z0, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1)};
}

SampleRect IntersectRects(const SampleRect& a, const SampleRect& b) noexcept {
    if (a.Empty() || b.Empty()) {
        return {};
    }
    const int64_t x0 = (std::max)(a.x0, b.x0);
    const int64_t z0 = (std::max)(a.z0, b.z0);
    const int64_t x1 = (std::min)(a.X1(), b.X1());
    const int64_t z1 = (std::min)(a.Z1(), b.Z1());
    if (x1 < x0 || z1 < z0) {
        return {};
    }
    return SampleRect{x0, z0, static_cast<uint32_t>(x1 - x0 + 1), static_cast<uint32_t>(z1 - z0 + 1)};
}

uint16_t QuantizeHeight(const TerrainGrid& grid, float height) noexcept {
    if (!std::isfinite(height)) {
        return 0u;
    }
    const double normalized = (static_cast<double>(height) - grid.heightMin) / grid.heightRange;
    const double scaled = std::clamp(normalized, 0.0, 1.0) * 65535.0;
    return static_cast<uint16_t>(scaled + 0.5);
}

float DequantizeHeight(const TerrainGrid& grid, uint16_t value) noexcept {
    return static_cast<float>(grid.heightMin + static_cast<double>(value) / 65535.0 * grid.heightRange);
}

} // namespace HIKARI::TERRAIN::DATA
