#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include "HIKARI_TerrainKitEditorCommon.h"
#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

// 無圧縮の PNG（deflate の格納ブロック）。出す先は Library の中なので大きさは問わない。
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0u; i < 256u; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1u) : value >> 1u;
            }
            values[i] = value;
        }
        return values;
    }();
    for (size_t index = 0u; index < size; ++index) {
        crc = table[(crc ^ data[index]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc;
}

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void PutChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
    PutU32(out, static_cast<uint32_t>(body.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    PutU32(out, Crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
}

// channels は 1（灰）か 3（RGB）。
bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height, uint32_t channels,
              const std::vector<uint8_t>& pixels) {
    std::vector<uint8_t> raw{};
    const size_t rowBytes = static_cast<size_t>(width) * channels;
    raw.reserve((rowBytes + 1u) * height);
    for (uint32_t y = 0u; y < height; ++y) {
        raw.push_back(0u);
        const auto row = pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * rowBytes);
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(rowBytes));
    }
    std::vector<uint8_t> zlib{0x78u, 0x01u};
    uint32_t adlerA = 1u;
    uint32_t adlerB = 0u;
    for (const uint8_t value : raw) {
        adlerA = (adlerA + value) % 65521u;
        adlerB = (adlerB + adlerA) % 65521u;
    }
    for (size_t offset = 0u; offset < raw.size();) {
        const size_t size = (std::min)(raw.size() - offset, size_t{65535u});
        zlib.push_back(offset + size == raw.size() ? 1u : 0u);
        zlib.push_back(static_cast<uint8_t>(size));
        zlib.push_back(static_cast<uint8_t>(size >> 8u));
        zlib.push_back(static_cast<uint8_t>(~size));
        zlib.push_back(static_cast<uint8_t>(~size >> 8u));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    }
    PutU32(zlib, (adlerB << 16u) | adlerA);
    std::vector<uint8_t> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    std::vector<uint8_t> header{};
    PutU32(header, width);
    PutU32(header, height);
    header.insert(header.end(), {8u, static_cast<uint8_t>(channels == 3u ? 2u : 0u), 0u, 0u, 0u});
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});
    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(
        file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())));
}

} // namespace

std::filesystem::path TerrainOutputDirectory(const TerrainStore& store) {
    return store.ProjectRoot() / "Library" / "TerrainKit";
}

bool WriteGrayPng(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<float>& values,
                  float& outMinimum, float& outMaximum) {
    outMinimum = std::numeric_limits<float>::max();
    outMaximum = std::numeric_limits<float>::lowest();
    for (const float value : values) {
        outMinimum = (std::min)(outMinimum, value);
        outMaximum = (std::max)(outMaximum, value);
    }
    const float scale = outMaximum > outMinimum ? 255.0f / (outMaximum - outMinimum) : 0.0f;
    std::vector<uint8_t> gray(values.size());
    for (size_t index = 0u; index < values.size(); ++index) {
        gray[index] = static_cast<uint8_t>((values[index] - outMinimum) * scale);
    }
    return WritePng(path, width, height, 1u, gray);
}

bool WriteOverviewPng(const TerrainStore& store, uint32_t size, std::filesystem::path& outPath,
                      float& outMetersPerPixel, std::string& error) {
    const TerrainSettings& settings = store.Settings();
    const float tileSize = settings.grid.tileSizeMeters;
    const float minX = static_cast<float>(settings.firstTileX) * tileSize;
    const float minZ = static_cast<float>(settings.firstTileZ) * tileSize;
    const float extent =
        (std::max)(static_cast<float>(settings.tilesX), static_cast<float>(settings.tilesZ)) * tileSize;
    const float step = extent / static_cast<float>(size);
    std::vector<uint8_t> rgb(static_cast<size_t>(size) * size * 3u);
    const auto sample = [&](float x, float z) { return store.HeightAt(x, z).value_or(0.0f); };
    for (uint32_t row = 0u; row < size; ++row) {
        // 上が +Z になるよう、画像の行を反転する。
        const float z = minZ + extent - (static_cast<float>(row) + 0.5f) * step;
        for (uint32_t column = 0u; column < size; ++column) {
            const float x = minX + (static_cast<float>(column) + 0.5f) * step;
            const float height = sample(x, z);
            const float dx = (sample(x + step, z) - sample(x - step, z)) / (2.0f * step);
            const float dz = (sample(x, z + step) - sample(x, z - step)) / (2.0f * step);
            const float inverse = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
            const float shade = std::clamp((0.35f * -dx + 0.8f + 0.45f * -dz) * inverse, 0.15f, 1.0f);
            const float slope = 1.0f - inverse;
            const float rock = std::clamp((slope - 0.14f) / 0.18f, 0.0f, 1.0f);
            const float snow = std::clamp((height - 170.0f) / 40.0f, 0.0f, 1.0f) * (1.0f - rock);
            const std::array<float, 3> color{
                (0.30f + 0.20f * rock + 0.45f * snow) * shade,
                (0.40f + 0.05f * rock + 0.42f * snow) * shade,
                (0.20f + 0.20f * rock + 0.60f * snow) * shade,
            };
            uint8_t* pixel = rgb.data() + (static_cast<size_t>(row) * size + column) * 3u;
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = static_cast<uint8_t>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f);
            }
        }
    }
    outPath = TerrainOutputDirectory(store) / "overview.png";
    outMetersPerPixel = step;
    if (!WritePng(outPath, size, size, 3u, rgb)) {
        error = "Could not write " + outPath.generic_string();
        return false;
    }
    return true;
}

bool ExportBakedHeight(const TerrainStore& store, std::filesystem::path& outExr, std::filesystem::path& outRaw,
                       uint32_t& outWidth, std::string& error) {
    const TerrainSettings& settings = store.Settings();
    const TerrainGrid& grid = settings.grid;
    const SampleRect whole = WholeTerrainRect(store);
    HeightImage image{};
    image.width = whole.width;
    image.height = whole.height;
    image.values.assign(whole.Count(), 0.0f);
    const uint32_t samples = TileSamples(grid);
    for (const auto& [tile, runtime] : store.Tiles()) {
        if (runtime.heights.empty()) {
            continue;
        }
        const SampleRect rect = TileSampleRect(grid, tile);
        for (uint32_t z = 0u; z < samples; ++z) {
            const size_t row = static_cast<size_t>(rect.z0 - whole.z0 + z) * whole.width + (rect.x0 - whole.x0);
            for (uint32_t x = 0u; x < samples; ++x) {
                image.values[row + x] = DequantizeHeight(grid, runtime.heights[static_cast<size_t>(z) * samples + x]);
            }
        }
    }
    const std::filesystem::path directory = TerrainOutputDirectory(store);
    std::error_code ec{};
    std::filesystem::create_directories(directory, ec);
    outExr = directory / "height.exr";
    outRaw = directory / "height.r16";
    outWidth = whole.width;
    if (!SaveHeightImageExr(outExr, image, error)) {
        return false;
    }
    for (float& value : image.values) {
        value = (value - grid.heightMin) / grid.heightRange;
    }
    return SaveHeightImageRaw16(outRaw, image, error);
}

} // namespace HIKARI::TERRAINKIT
