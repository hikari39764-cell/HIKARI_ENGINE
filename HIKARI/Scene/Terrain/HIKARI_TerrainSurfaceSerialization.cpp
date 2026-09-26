#include "Scene/Terrain/HIKARI_TerrainSurfaceSerialization.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "Core/HIKARI_JsonRead.h"

namespace HIKARI::TERRAIN {
namespace {
constexpr uint32_t kTerrainSurfaceSchemaVersion = 9u;
// 数値項目のキーとメンバーは保存・読込で共有する。
template <class Settings>
struct FloatField {
    const char* key;
    float Settings::* member;
};

template <class Settings, std::size_t N>
void WriteFloatFields(nlohmann::json& out, const Settings& settings, const FloatField<Settings> (&fields)[N]) {
    for (const auto& field : fields) {
        out[field.key] = settings.*field.member;
    }
}

template <class Settings, std::size_t N>
void ReadFloatFields(const nlohmann::json& in, Settings& settings, const FloatField<Settings> (&fields)[N]) {
    for (const auto& field : fields) {
        auto& value = settings.*field.member;
        value = JSONREAD::NumberMemberOr(in, field.key, value);
    }
}

using MaterialSettings = decltype(TerrainSurface::material);
constexpr FloatField<MaterialSettings> kMaterialFields[] = {
    {"sparkleStrength", &MaterialSettings::sparkleStrength},
    {"rippleAmount", &MaterialSettings::rippleAmount},
    {"pitDarkening", &MaterialSettings::pitDarkening},
    {"toonBandHardness", &MaterialSettings::toonBandHardness},
};

using StylizationSettings = decltype(TerrainSurface::stylization);
constexpr FloatField<StylizationSettings> kStylizationFields[] = {
    {"textureScale", &StylizationSettings::textureScale},
    {"textureStrength", &StylizationSettings::textureStrength},
    {"normalStrength", &StylizationSettings::normalStrength},
    {"saturation", &StylizationSettings::saturation},
    {"shadowStrength", &StylizationSettings::shadowStrength},
    {"rockSaturation", &StylizationSettings::rockSaturation},
    {"facetSizeMeters", &StylizationSettings::facetSizeMeters},
    {"facetStrength", &StylizationSettings::facetStrength},
};

using DeformSettings = decltype(TerrainSurface::deform);
constexpr FloatField<DeformSettings> kDeformFields[] = {
    {"maxDepth", &DeformSettings::maxDepth},
    {"stampRadius", &DeformSettings::stampRadius},
    {"rimStrength", &DeformSettings::rimStrength},
    {"restDepthScale", &DeformSettings::restDepthScale},
    {"minimumDepth", &DeformSettings::minimumDepth},
    {"settleStartRatio", &DeformSettings::settleStartRatio},
    {"settleSeconds", &DeformSettings::settleSeconds},
    {"impactDepthScale", &DeformSettings::impactDepthScale},
    {"rimWidthScale", &DeformSettings::rimWidthScale},
    {"refillPerSecond", &DeformSettings::refillPerSecond},
    {"diffusionPerSecond", &DeformSettings::diffusionPerSecond},
    {"plasticity", &DeformSettings::plasticity},
    {"plasticRefillPerSecond", &DeformSettings::plasticRefillPerSecond},
    {"recoveryDelaySeconds", &DeformSettings::recoveryDelaySeconds},
};

using ShorelineSettings = decltype(TerrainSurface::shoreline);
constexpr FloatField<ShorelineSettings> kShorelineFields[] = {
    {"edgeFalloff", &ShorelineSettings::edgeFalloff},
    {"edgeDepth", &ShorelineSettings::edgeDepth},
    {"wetness", &ShorelineSettings::wetness},
};

using BrushSettings = decltype(TerrainSurface::brush);
constexpr FloatField<BrushSettings> kBrushFields[] = {
    {"radius", &BrushSettings::radius},
    {"strength", &BrushSettings::strength},
    {"hardness", &BrushSettings::hardness},
    {"falloff", &BrushSettings::falloff},
    {"flattenHeight", &BrushSettings::flattenHeight},
    {"ridgeDirectionDegrees", &BrushSettings::ridgeDirectionDegrees},
    {"ridgeSharpness", &BrushSettings::ridgeSharpness},
    {"terraceStepHeight", &BrushSettings::terraceStepHeight},
    {"shaftDepth", &BrushSettings::shaftDepth},
    {"shaftWallWidth", &BrushSettings::shaftWallWidth},
    {"noiseAmplitude", &BrushSettings::noiseAmplitude},
    {"noiseScale", &BrushSettings::noiseScale},
};

nlohmann::json SerializeVec3(const MATH::Vec3& value) { return nlohmann::json::array({value.x, value.y, value.z}); }

MATH::Vec3 ReadVec3(const nlohmann::json& source, const char* key, const MATH::Vec3& fallback) {
    if (const nlohmann::json* value = JSONREAD::FindMember(source, key);
        value != nullptr && value->is_array() && value->size() >= 3u) {
        return MATH::Vec3{(*value)[0].get<float>(), (*value)[1].get<float>(), (*value)[2].get<float>()};
    }
    return fallback;
}

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
    out = {
        {"_schemaVersion", kTerrainSurfaceSchemaVersion},
        {"presetId", s.presetId},
        {"heightScale", s.heightScale},
        {"material",
         {
             {"lightTint", SerializeVec3(s.material.lightTint)},
             {"darkTint", SerializeVec3(s.material.darkTint)},
             {"toonBandCount", s.material.toonBandCount},
         }},
        {"stylization", nlohmann::json::object()},
        {"deform",
         {
             {"behavior", static_cast<uint32_t>(s.deform.behavior)},
             {"responseResolutionScale", s.deform.responseResolutionScale},
         }},
        {"shoreline", nlohmann::json::object()},
        {"layerPresetIds", s.layerPresetIds},
        {"brush",
         {
             {"mode", TERRAIN::ToString(s.brush.mode)},
             {"noiseSeed", s.brush.noiseSeed},
         }},
        {"grass", TERRAIN::SerializeTerrainGrassSettings(s.grass)},
    };
    WriteFloatFields(out["material"], s.material, kMaterialFields);
    WriteFloatFields(out["stylization"], s.stylization, kStylizationFields);
    WriteFloatFields(out["deform"], s.deform, kDeformFields);
    WriteFloatFields(out["shoreline"], s.shoreline, kShorelineFields);
    WriteFloatFields(out["brush"], s.brush, kBrushFields);
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
    out["chunked"] = true;
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
    // 省略された block は既定値から始め、以前の in-memory 値を持ち越さない。
    surface.stylization = TERRAIN::TerrainStylizationSettings{};
    surface.grass = TERRAIN::TerrainGrassSettings{};
    surface.presetId = in.value("presetId", surface.presetId);
    surface.heightScale = JSONREAD::NumberMemberOr(in, "heightScale", surface.heightScale);
    surface.chunkCells = JSONREAD::UnsignedMemberOr(in, "chunkCells", surface.chunkCells);
    surface.chunkCells = (std::clamp)(surface.chunkCells, 1u, TERRAIN::kTerrainChunkMaxCells);
    surface.chunkCellSize = JSONREAD::NumberMemberOr(in, "chunkCellSize", surface.chunkCellSize);

    if (const nlohmann::json* material = JSONREAD::FindMember(in, "material");
        material != nullptr && material->is_object()) {
        ReadFloatFields(*material, surface.material, kMaterialFields);
        surface.material.lightTint = ReadVec3(*material, "lightTint", surface.material.lightTint);
        surface.material.darkTint = ReadVec3(*material, "darkTint", surface.material.darkTint);

        surface.material.toonBandCount =
            static_cast<uint32_t>(material->value("toonBandCount", surface.material.toonBandCount));
    }
    if (const nlohmann::json* stylization = JSONREAD::FindMember(in, "stylization");
        stylization != nullptr && stylization->is_object()) {
        ReadFloatFields(*stylization, surface.stylization, kStylizationFields);
    }
    if (const nlohmann::json* deform = JSONREAD::FindMember(in, "deform"); deform != nullptr && deform->is_object()) {
        ReadFloatFields(*deform, surface.deform, kDeformFields);
        surface.deform.behavior = static_cast<TERRAIN::DeformBehaviorId>(
            deform->value("behavior", static_cast<uint32_t>(surface.deform.behavior)));

        surface.deform.responseResolutionScale =
            static_cast<uint32_t>(deform->value("responseResolutionScale", surface.deform.responseResolutionScale));
    }
    if (const nlohmann::json* shoreline = JSONREAD::FindMember(in, "shoreline");
        shoreline != nullptr && shoreline->is_object()) {
        ReadFloatFields(*shoreline, surface.shoreline, kShorelineFields);
    }
    if (const nlohmann::json* presetIds = JSONREAD::FindMember(in, "layerPresetIds");
        presetIds != nullptr && presetIds->is_array()) {
        surface.layerPresetIds.clear();
        for (const nlohmann::json& v : *presetIds) {
            surface.layerPresetIds.push_back(v.is_string() ? v.get<std::string>() : "");
        }
    }
    if (const nlohmann::json* brush = JSONREAD::FindMember(in, "brush"); brush != nullptr && brush->is_object()) {
        ReadFloatFields(*brush, surface.brush, kBrushFields);
        surface.brush.mode = TEXT::ParseEnum(TERRAIN::kTerrainBrushModeNames,
                                             brush->value("mode", std::string(TERRAIN::ToString(surface.brush.mode))),
                                             surface.brush.mode);

        surface.brush.noiseSeed = JSONREAD::UnsignedMemberOr(*brush, "noiseSeed", surface.brush.noiseSeed);
    }

    if (const nlohmann::json* grass = JSONREAD::FindMember(in, "grass"); grass != nullptr && grass->is_object()) {
        surface.grass = TERRAIN::DeserializeTerrainGrassSettings(*grass);
    }

    surface.chunks.clear();
    if (const nlohmann::json* chunks = JSONREAD::FindMember(in, "chunks"); chunks != nullptr && chunks->is_array()) {
        for (const nlohmann::json& c : *chunks) {
            if (!c.is_object()) {
                continue;
            }
            TERRAIN::TerrainChunkKey key{c.value("x", 0), c.value("z", 0)};
            TERRAIN::TerrainChunk chunk{};
            chunk.cells = JSONREAD::UnsignedMemberOr(c, "cells", surface.chunkCells);
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
                surface.chunks.emplace(key, std::move(chunk));
            }
        }
    }

    return true;
}

} // namespace HIKARI::TERRAIN
