#include "TerrainKit/Data/HIKARI_TerrainDataFile.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "HIKARI_TerrainDataCodec.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr uint32_t FourCc(char a, char b, char c, char d) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8u) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16u) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24u);
}

constexpr uint32_t kMagic = FourCc('H', 'K', 'T', 'C');
constexpr uint32_t kChunkHeights = FourCc('H', 'G', 'H', 'T');
constexpr uint32_t kChunkControl = FourCc('C', 'T', 'R', 'L');
constexpr uint32_t kChunkLayerValues = FourCc('L', 'V', 'A', 'L');
constexpr uint32_t kChunkLayerMask = FourCc('L', 'M', 'S', 'K');
constexpr uint8_t kCompressionNone = 0u;
constexpr uint8_t kCompressionDeflate = 1u;

struct Chunk {
    uint32_t type = 0u;
    uint32_t layerId = 0u;
    uint8_t compression = kCompressionNone;
    uint8_t elementSize = 0u;
    std::vector<uint8_t> stored{};
    uint32_t rawSize = 0u;
};

template <class T>
bool MakeChunk(uint32_t type, uint32_t layerId, const TileRaster<T>& raster, bool deflate, Chunk& out) {
    out.type = type;
    out.layerId = layerId;
    out.elementSize = static_cast<uint8_t>(sizeof(T));
    const std::span<const uint8_t> raw = CODEC::AsBytes(raster.values);
    out.rawSize = static_cast<uint32_t>(raw.size());
    if (deflate) {
        out.compression = kCompressionDeflate;
        return CODEC::Deflate(raw, out.stored);
    }
    out.compression = kCompressionNone;
    out.stored.assign(raw.begin(), raw.end());
    return true;
}

struct ParsedChunk {
    uint32_t type = 0u;
    uint32_t layerId = 0u;
    std::vector<uint8_t> raw{};
};

struct ParsedCell {
    std::vector<ParsedChunk> chunks{};
};

// ヘッダー・目録・各チャンクの CRC と大きさを全部確かめ、展開したチャンクを返す。
bool ParseCell(const TerrainGrid& grid, TileCoord expected, std::span<const uint8_t> bytes, ParsedCell& out,
               std::string& error) {
    if (bytes.size() < kTerrainCellHeaderBytes) {
        error = "terrain cell file is shorter than its header";
        return false;
    }
    CODEC::ByteReader header(bytes);
    const uint32_t magic = header.U32();
    const uint16_t version = header.U16();
    const uint16_t reserved = header.U16();
    const TileCoord tile{header.I32(), header.I32()};
    const uint32_t tileCells = header.U32();
    const uint32_t chunkCount = header.U32();
    const float heightMin = header.F32();
    const float heightRange = header.F32();
    const uint32_t directoryCrc = header.U32();
    const uint32_t headerCrc = header.U32();
    if (magic != kMagic) {
        error = "not a terrain cell file (magic mismatch)";
        return false;
    }
    if (version != kTerrainCellFileVersion) {
        error = "unsupported terrain cell file version " + std::to_string(version) + " (current is " +
                std::to_string(kTerrainCellFileVersion) + ")";
        return false;
    }
    if (headerCrc != CODEC::Crc32(bytes.first(36u))) {
        error = "terrain cell header checksum mismatch";
        return false;
    }
    if (reserved != 0u) {
        error = "terrain cell header has non-zero reserved bits";
        return false;
    }
    if (tile != expected) {
        error = "terrain cell file holds tile (" + std::to_string(tile.x) + ", " + std::to_string(tile.z) +
                ") instead of (" + std::to_string(expected.x) + ", " + std::to_string(expected.z) + ")";
        return false;
    }
    if (tileCells != grid.tileCells || heightMin != grid.heightMin || heightRange != grid.heightRange) {
        error = "terrain cell file was written for a different terrain grid";
        return false;
    }
    const std::size_t directoryBytes = static_cast<std::size_t>(chunkCount) * kTerrainCellDirectoryEntryBytes;
    if (chunkCount > 4096u || bytes.size() - kTerrainCellHeaderBytes < directoryBytes) {
        error = "terrain cell directory is truncated";
        return false;
    }
    if (directoryCrc != CODEC::Crc32(bytes.subspan(kTerrainCellHeaderBytes, directoryBytes))) {
        error = "terrain cell directory checksum mismatch";
        return false;
    }
    const std::size_t sampleCount = TileSampleCount(grid);
    CODEC::ByteReader directory(bytes, kTerrainCellHeaderBytes);
    ParsedCell parsed{};
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (uint32_t index = 0u; index < chunkCount; ++index) {
        ParsedChunk chunk{};
        chunk.type = directory.U32();
        chunk.layerId = directory.U32();
        const uint8_t compression = directory.U8();
        const uint8_t elementSize = directory.U8();
        const uint16_t entryReserved = directory.U16();
        const uint64_t offset = directory.U64();
        const uint32_t storedSize = directory.U32();
        const uint32_t rawSize = directory.U32();
        const uint32_t crc = directory.U32();
        const uint8_t expectedElement = chunk.type == kChunkHeights ? 2u : 4u;
        const bool knownType = chunk.type == kChunkHeights || chunk.type == kChunkControl ||
                               chunk.type == kChunkLayerValues || chunk.type == kChunkLayerMask;
        if (!knownType || entryReserved != 0u || elementSize != expectedElement ||
            rawSize != sampleCount * expectedElement ||
            (compression != kCompressionNone && compression != kCompressionDeflate)) {
            error = "terrain cell chunk " + std::to_string(index) + " has an unknown type, size or compression";
            return false;
        }
        if (!seen.emplace(chunk.type, chunk.layerId).second) {
            error = "terrain cell chunk " + std::to_string(index) + " repeats an earlier chunk";
            return false;
        }
        if (offset > bytes.size() || bytes.size() - offset < storedSize) {
            error = "terrain cell chunk " + std::to_string(index) + " points outside the file";
            return false;
        }
        const std::span<const uint8_t> stored = bytes.subspan(static_cast<std::size_t>(offset), storedSize);
        if (CODEC::Crc32(stored) != crc) {
            error = "terrain cell chunk " + std::to_string(index) + " checksum mismatch";
            return false;
        }
        if (compression == kCompressionDeflate) {
            if (!CODEC::Inflate(stored, rawSize, chunk.raw)) {
                error = "terrain cell chunk " + std::to_string(index) + " does not inflate to its size";
                return false;
            }
        } else if (storedSize != rawSize) {
            error = "terrain cell chunk " + std::to_string(index) + " stored size mismatch";
            return false;
        } else {
            chunk.raw.assign(stored.begin(), stored.end());
        }
        parsed.chunks.push_back(std::move(chunk));
    }
    out = std::move(parsed);
    return true;
}

template <class T>
bool ToRaster(const TerrainGrid& grid, const ParsedChunk& chunk, TileRaster<T>& out) {
    out.samples = TileSamples(grid);
    return CODEC::FromBytes(chunk.raw, TileSampleCount(grid), out.values);
}

} // namespace

std::string TerrainCellFileName(TileCoord tile) {
    char name[64];
    std::snprintf(name, sizeof(name), "x%+04d_z%+04d.terrain.bin", tile.x, tile.z);
    return name;
}

bool WriteTerrainCell(const TerrainData& data, TileCoord tile, const HeightStackEvaluator* evaluator,
                      const TerrainCellWriteOptions& options, std::vector<uint8_t>& out, std::string& error) {
    const TerrainGrid& grid = data.Grid();
    std::vector<Chunk> chunks;
    if (options.includeBakedHeights) {
        TileRaster<uint16_t> baked{};
        if (evaluator == nullptr || !evaluator->BakeTile(tile, baked, &error)) {
            if (error.empty()) {
                error = "baking terrain heights needs an evaluator";
            }
            return false;
        }
        chunks.emplace_back();
        MakeChunk(kChunkHeights, 0u, baked, false, chunks.back());
    }
    if (const TileRaster<uint32_t>* control = data.Control().Find(tile)) {
        chunks.emplace_back();
        if (!MakeChunk(kChunkControl, 0u, *control, true, chunks.back())) {
            error = "terrain control map could not be compressed";
            return false;
        }
    }
    for (const HeightLayer& layer : data.Stack().layers) {
        const TiledRaster<float>* values = data.LayerValues(layer.id);
        const TiledRaster<float>* mask = data.LayerMask(layer.id);
        for (const auto& [type, raster] : {std::pair{kChunkLayerValues, values}, std::pair{kChunkLayerMask, mask}}) {
            const TileRaster<float>* found = raster != nullptr ? raster->Find(tile) : nullptr;
            if (found == nullptr) {
                continue;
            }
            chunks.emplace_back();
            if (!MakeChunk(type, layer.id, *found, true, chunks.back())) {
                error = "terrain layer tile could not be compressed";
                return false;
            }
        }
    }
    out.clear();
    CODEC::ByteWriter writer(out);
    writer.U32(kMagic);
    writer.U16(kTerrainCellFileVersion);
    writer.U16(0u);
    writer.I32(tile.x);
    writer.I32(tile.z);
    writer.U32(grid.tileCells);
    writer.U32(static_cast<uint32_t>(chunks.size()));
    writer.F32(grid.heightMin);
    writer.F32(grid.heightRange);
    const std::size_t directoryCrcOffset = writer.Size();
    writer.U32(0u);
    writer.U32(0u);
    uint64_t offset = kTerrainCellHeaderBytes + chunks.size() * kTerrainCellDirectoryEntryBytes;
    for (const Chunk& chunk : chunks) {
        writer.U32(chunk.type);
        writer.U32(chunk.layerId);
        writer.U8(chunk.compression);
        writer.U8(chunk.elementSize);
        writer.U16(0u);
        writer.U64(offset);
        writer.U32(static_cast<uint32_t>(chunk.stored.size()));
        writer.U32(chunk.rawSize);
        writer.U32(CODEC::Crc32(chunk.stored));
        offset += chunk.stored.size();
    }
    for (const Chunk& chunk : chunks) {
        writer.Bytes(chunk.stored);
    }
    const std::span<const uint8_t> bytes(out);
    writer.PatchU32(directoryCrcOffset, CODEC::Crc32(bytes.subspan(kTerrainCellHeaderBytes,
                                                                   chunks.size() * kTerrainCellDirectoryEntryBytes)));
    writer.PatchU32(directoryCrcOffset + 4u, CODEC::Crc32(bytes.first(36u)));
    return true;
}

bool ReadTerrainCell(TerrainData& data, TileCoord expected, std::span<const uint8_t> bytes, std::string& error) {
    const TerrainGrid& grid = data.Grid();
    ParsedCell parsed{};
    if (!ParseCell(grid, expected, bytes, parsed, error)) {
        return false;
    }
    // 先に全チャンクを型付きの配列にして層の存在を確かめ、それから置き換える。
    std::optional<TileRaster<uint32_t>> control;
    std::vector<std::pair<uint32_t, TileRaster<float>>> values, masks;
    for (const ParsedChunk& chunk : parsed.chunks) {
        if (chunk.type == kChunkHeights) {
            continue; // 焼いた高さは編集データから作り直せる
        }
        if (chunk.type == kChunkControl) {
            TileRaster<uint32_t> raster{};
            if (!ToRaster(grid, chunk, raster)) {
                error = "terrain control map could not be read";
                return false;
            }
            control = std::move(raster);
            continue;
        }
        const HeightLayer* layer = data.FindLayer(chunk.layerId);
        if (layer == nullptr || (chunk.type == kChunkLayerValues && layer->kind != HeightLayerKind::Sculpt)) {
            error = "terrain cell refers to layer " + std::to_string(chunk.layerId) + " that is not in the stack";
            return false;
        }
        TileRaster<float> raster{};
        ToRaster(grid, chunk, raster);
        (chunk.type == kChunkLayerValues ? values : masks).emplace_back(chunk.layerId, std::move(raster));
    }
    if (control.has_value()) {
        data.Control().Assign(expected, std::move(*control));
    } else {
        data.Control().Erase(expected);
    }
    for (const HeightLayer& layer : data.Stack().layers) {
        if (TiledRaster<float>* raster = data.LayerValues(layer.id)) {
            raster->Erase(expected);
        }
        if (TiledRaster<float>* raster = data.LayerMask(layer.id)) {
            raster->Erase(expected);
        }
    }
    for (auto& [id, raster] : values) {
        data.LayerValues(id)->Assign(expected, std::move(raster));
    }
    for (auto& [id, raster] : masks) {
        data.LayerMask(id)->Assign(expected, std::move(raster));
    }
    data.MarkChanged();
    return true;
}

bool ReadTerrainCellRuntime(const TerrainGrid& grid, TileCoord expected, std::span<const uint8_t> bytes,
                            TerrainCellRuntime& out, std::string& error) {
    ParsedCell parsed{};
    if (!ParseCell(grid, expected, bytes, parsed, error)) {
        return false;
    }
    TerrainCellRuntime runtime{};
    runtime.control = TileRaster<uint32_t>(TileSamples(grid), 0u);
    bool hasHeights = false;
    for (const ParsedChunk& chunk : parsed.chunks) {
        if (chunk.type == kChunkHeights) {
            hasHeights = ToRaster(grid, chunk, runtime.heights);
        } else if (chunk.type == kChunkControl) {
            ToRaster(grid, chunk, runtime.control);
        }
    }
    if (!hasHeights) {
        error = "terrain cell file has no baked heights";
        return false;
    }
    out = std::move(runtime);
    return true;
}

bool SaveTerrainCellFile(const std::filesystem::path& path, std::span<const uint8_t> bytes, std::string& error) {
    // 途中で落ちても前のファイルを壊さないよう、隣の一時ファイルへ書いてから置き換える。
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file ||
            !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            error = "cannot write " + temporary.string();
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, path, code);
    if (code) {
        error = "cannot replace " + path.string() + ": " + code.message();
        return false;
    }
    return true;
}

bool LoadTerrainCellFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "cannot open " + path.string();
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || size > (std::numeric_limits<int32_t>::max)()) {
        error = "unsupported terrain cell file size: " + path.string();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        error = "cannot read " + path.string();
        return false;
    }
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
