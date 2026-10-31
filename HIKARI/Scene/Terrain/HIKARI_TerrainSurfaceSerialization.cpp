#include "Scene/Terrain/HIKARI_TerrainSurfaceSerialization.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "Core/HIKARI_JsonRead.h"

namespace HIKARI::TERRAIN {
namespace {
constexpr uint32_t kTerrainSurfaceSchemaVersion = 9u;

std::vector<float> ReadFloatArray(const nlohmann::json& source, const char* key,
                                  std::size_t maximumSize = (std::numeric_limits<std::size_t>::max)()) {
    std::vector<float> out;
    if (const nlohmann::json* arr = JSONREAD::FindMember(source, key); arr != nullptr && arr->is_array()) {
        if (arr->size() > maximumSize) {
            return out;
        }
        out.reserve(arr->size());
        for (const nlohmann::json& v : *arr) {
            out.push_back(v.is_number() ? v.get<float>() : 0.0f);
        }
    }
    return out;
}

} // namespace

void SerializeTerrainSurfaceSettings(const TerrainSurface& s, nlohmann::json& out) {
    out = nlohmann::json::object();
    out["_schemaVersion"] = kTerrainSurfaceSchemaVersion;
    JSONFIELDS::Write(out, s, kTerrainSurfaceFields);
    out["layerPresetIds"] = s.layerPresetIds;
}

void SerializeTerrainSurface(const TerrainSurface& s, nlohmann::json& out) {
    SerializeTerrainSurfaceSettings(s, out);
    nlohmann::json chunks = nlohmann::json::array();
    for (const auto& [key, chunk] : s.chunks) {
        chunks.push_back({
            {"x", key.x},
            {"z", key.z},
            {"cells", chunk.cells},
            {"layerCount", chunk.layerCount},
            {"heights", chunk.heights},
            {"coverage", chunk.coverage},
            {"layerWeights", chunk.layerWeights},
        });
    }
    out["chunkCells"] = s.chunkCells;
    out["chunkCellSize"] = s.chunkCellSize;
    out["chunks"] = std::move(chunks);
}

bool DeserializeTerrainSurface(const nlohmann::json& in, TerrainSurface& surface) {
    if (!in.is_object()) {
        return false;
    }
    if (in.value("_schemaVersion", 0u) != kTerrainSurfaceSchemaVersion) {
        return false;
    }
    // 省略された項目は既定値から始め、以前の in-memory 値を持ち越さない。
    TerrainSurface decoded{};
    JSONFIELDS::Read(in, decoded, kTerrainSurfaceFields);
    if (const nlohmann::json* presetIds = JSONREAD::FindMember(in, "layerPresetIds");
        presetIds != nullptr && presetIds->is_array()) {
        for (const nlohmann::json& v : *presetIds) {
            decoded.layerPresetIds.push_back(v.is_string() ? v.get<std::string>() : "");
        }
    }
    decoded.chunkCells = JSONREAD::UnsignedMemberOr(in, "chunkCells", decoded.chunkCells);
    decoded.chunkCells = (std::clamp)(decoded.chunkCells, 1u, TERRAIN::kTerrainChunkMaxCells);
    decoded.chunkCellSize = JSONREAD::NumberMemberOr(in, "chunkCellSize", decoded.chunkCellSize);

    if (const nlohmann::json* chunks = JSONREAD::FindMember(in, "chunks"); chunks != nullptr && chunks->is_array()) {
        for (const nlohmann::json& c : *chunks) {
            if (!c.is_object()) {
                continue;
            }
            TERRAIN::TerrainChunkKey key{c.value("x", 0), c.value("z", 0)};
            TERRAIN::TerrainChunk chunk{};
            chunk.cells = JSONREAD::UnsignedMemberOr(c, "cells", decoded.chunkCells);
            chunk.layerCount = JSONREAD::UnsignedMemberOr(c, "layerCount", 1u);
            std::size_t vertexCount = 0u;
            if (!TERRAIN::TryGetTerrainChunkVertexCount(chunk.cells, vertexCount) || chunk.layerCount == 0u ||
                chunk.layerCount > TERRAIN::kTerrainChunkMaxLayers ||
                vertexCount > (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(chunk.layerCount)) {
                continue;
            }
            const std::size_t layerWeightCount = vertexCount * static_cast<std::size_t>(chunk.layerCount);
            chunk.heights = ReadFloatArray(c, "heights", vertexCount);
            chunk.coverage = ReadFloatArray(c, "coverage", vertexCount);
            chunk.layerWeights = ReadFloatArray(c, "layerWeights", layerWeightCount);
            if (TERRAIN::TerrainChunkValid(chunk)) {
                decoded.chunks.emplace(key, std::move(chunk));
            }
        }
    }

    surface = std::move(decoded);
    return true;
}

} // namespace HIKARI::TERRAIN
