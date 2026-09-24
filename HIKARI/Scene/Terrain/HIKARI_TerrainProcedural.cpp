#include "Scene/Terrain/HIKARI_TerrainProcedural.h"

#include <cmath>
#include <limits>

namespace HIKARI::TERRAIN {
namespace {
constexpr float kPi = 3.14159265358979323846f;

uint64_t MixBits(uint64_t value) noexcept {
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

float HashGrid(int64_t x, int64_t z, uint64_t seed) noexcept {
    const uint64_t ux = static_cast<uint64_t>(x);
    const uint64_t uz = static_cast<uint64_t>(z);
    const uint64_t mixed = MixBits(seed ^ (ux * 0x9e3779b97f4a7c15ull) ^ (uz * 0xc2b2ae3d27d4eb4full));
    constexpr float kInvMax = 1.0f / 9007199254740992.0f;
    const double unit = static_cast<double>(mixed >> 11u) * kInvMax;
    return static_cast<float>(unit * 2.0 - 1.0);
}

float Smooth(float value) noexcept {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ValueNoise(float x, float z, uint64_t seed) noexcept {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int64_t x0 = static_cast<int64_t>(fx);
    const int64_t z0 = static_cast<int64_t>(fz);
    const float tx = Smooth(x - fx);
    const float tz = Smooth(z - fz);
    const float a = HashGrid(x0, z0, seed);
    const float b = HashGrid(x0 + 1, z0, seed);
    const float c = HashGrid(x0, z0 + 1, seed);
    const float d = HashGrid(x0 + 1, z0 + 1, seed);
    return std::lerp(std::lerp(a, b, tx), std::lerp(c, d, tx), tz);
}

float FractalNoise(float x, float z, uint64_t seed, uint32_t octaves) noexcept {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float normalization = 0.0f;
    for (uint32_t octave = 0u; octave < octaves; ++octave) {
        sum += ValueNoise(x * frequency, z * frequency, seed + static_cast<uint64_t>(octave) * 0x9e3779b97f4a7c15ull) *
               amplitude;
        normalization += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return normalization > 1.0e-6f ? sum / normalization : 0.0f;
}

float RectMask(const TerrainProceduralSpec& spec, float localX, float localZ) noexcept {
    const float hx = (std::max)(spec.extentX, 0.5f);
    const float hz = (std::max)(spec.extentZ, 0.5f);
    const float nx = std::fabs(localX - spec.centerX) / hx;
    const float nz = std::fabs(localZ - spec.centerZ) / hz;
    if (nx >= 1.0f || nz >= 1.0f) {
        return 0.0f;
    }
    const float edge = (std::min)(1.0f - nx, 1.0f - nz);
    return Smooth(edge / (std::max)(spec.edgeFalloff, 0.01f));
}

float IslandMask(const TerrainProceduralSpec& spec, float localX, float localZ) noexcept {
    const float hx = (std::max)(spec.extentX, 0.5f);
    const float hz = (std::max)(spec.extentZ, 0.5f);
    const float nx = (localX - spec.centerX) / hx;
    const float nz = (localZ - spec.centerZ) / hz;
    const float radius = std::sqrt(nx * nx + nz * nz);
    if (radius >= 1.0f) {
        return 0.0f;
    }
    const float inner = 1.0f - spec.edgeFalloff;
    return 1.0f - Smooth((radius - inner) / (std::max)(spec.edgeFalloff, 0.01f));
}

float DirectionalCoordinates(const TerrainProceduralSpec& spec, float localX, float localZ, float& outAcross) noexcept {
    const float angle = spec.directionDegrees * kPi / 180.0f;
    const float axisX = std::cos(angle);
    const float axisZ = std::sin(angle);
    const float dx = localX - spec.centerX;
    const float dz = localZ - spec.centerZ;
    outAcross = -dz * axisX + dx * axisZ;
    return dx * axisX + dz * axisZ;
}
} // namespace

float EvaluateTerrainProceduralMask(const TerrainProceduralSpec& rawSpec, float localX, float localZ) noexcept {
    const TerrainProceduralSpec spec = SanitizeTerrainProceduralSpec(rawSpec);
    return spec.mode == TerrainProceduralMode::IslandCoast ? IslandMask(spec, localX, localZ)
                                                           : RectMask(spec, localX, localZ);
}

float EvaluateTerrainAuthoringNoise(float localX, float localZ, uint64_t seed, float scale, uint32_t octaves) noexcept {
    const float safeX = std::isfinite(localX) ? localX : 0.0f;
    const float safeZ = std::isfinite(localZ) ? localZ : 0.0f;
    const float safeScale = std::clamp(std::isfinite(scale) ? scale : 2.0f, 0.05f, 512.0f);
    return FractalNoise(safeX / safeScale, safeZ / safeScale, seed, std::clamp(octaves, 1u, 8u));
}

float EvaluateTerrainProceduralHeight(const TerrainProceduralSpec& rawSpec, float localX, float localZ) noexcept {
    const TerrainProceduralSpec spec = SanitizeTerrainProceduralSpec(rawSpec);
    const float mask = EvaluateTerrainProceduralMask(spec, localX, localZ);
    if (mask <= 0.0f) {
        return 0.0f;
    }
    const float dx = (localX - spec.centerX) / spec.scale;
    const float dz = (localZ - spec.centerZ) / spec.scale;
    const float noise = FractalNoise(dx, dz, spec.seed, spec.octaves);
    float rawHeight = 0.0f;
    switch (spec.mode) {
    case TerrainProceduralMode::NoiseHills: rawHeight = noise; break;
    case TerrainProceduralMode::IslandCoast: rawHeight = 0.35f + 0.65f * (noise * 0.5f + 0.5f); break;
    case TerrainProceduralMode::Ridge: {
        float across = 0.0f;
        const float along = DirectionalCoordinates(spec, localX, localZ, across);
        const float width = (std::max)(spec.scale * 0.42f, 0.1f);
        const float length = (std::max)((std::max)(spec.extentX, spec.extentZ) * 0.75f, spec.scale);
        const float ridge =
            std::exp(-(across * across) / (width * width)) * std::exp(-(along * along) / (length * length));
        rawHeight = ridge * (0.75f + 0.25f * (noise + 1.0f));
        break;
    }
    case TerrainProceduralMode::BasinCrater: {
        const float hx = (std::max)(spec.extentX, 0.5f);
        const float hz = (std::max)(spec.extentZ, 0.5f);
        const float nx = (localX - spec.centerX) / hx;
        const float nz = (localZ - spec.centerZ) / hz;
        const float radius = std::sqrt(nx * nx + nz * nz);
        const float bowl = -std::clamp(1.0f - radius * radius, 0.0f, 1.0f);
        const float rim = std::exp(-((radius - 0.78f) * (radius - 0.78f)) / 0.018f);
        rawHeight = bowl + rim * 0.65f + noise * 0.12f;
        break;
    }
    case TerrainProceduralMode::Dunes: {
        float across = 0.0f;
        const float along = DirectionalCoordinates(spec, localX, localZ, across);
        const float wave = std::sin(along / (std::max)(spec.scale, 0.25f) * 2.0f * kPi + noise * 1.5f);
        rawHeight = wave * 0.55f + noise * 0.45f;
        break;
    }
    }
    // The mask owns spatial admission and is applied exactly once by the
    // blend stage. Returning an already-masked value here would square
    // the falloff for Add/Max and make procedural edges disagree with
    // Replace and coverage authoring.
    return rawHeight * spec.amplitude;
}

TerrainProceduralPreview PreviewTerrainProcedural(const TerrainSurface& surface,
                                                  const TerrainProceduralSpec& rawSpec) noexcept {
    const TerrainProceduralSpec spec = SanitizeTerrainProceduralSpec(rawSpec);
    TerrainProceduralPreview preview{};
    if (spec.blendAmount <= 1.0e-6f) {
        return preview;
    }
    const float chunkSize = TerrainChunkWorldSize(surface);
    if (!std::isfinite(chunkSize) || chunkSize <= 1.0e-4f) {
        return preview;
    }
    const auto keyMin = TerrainChunkKeyFromWorld(spec.centerX - spec.extentX, spec.centerZ - spec.extentZ, chunkSize);
    const auto keyMax = TerrainChunkKeyFromWorld(spec.centerX + spec.extentX, spec.centerZ + spec.extentZ, chunkSize);
    const uint32_t cells = (std::clamp)(surface.chunkCells, 1u, kTerrainChunkMaxCells);
    const uint64_t chunkCountX =
        static_cast<uint64_t>(static_cast<int64_t>(keyMax.x) - static_cast<int64_t>(keyMin.x) + 1ll);
    const uint64_t chunkCountZ =
        static_cast<uint64_t>(static_cast<int64_t>(keyMax.z) - static_cast<int64_t>(keyMin.z) + 1ll);
    const uint64_t verticesPerChunk = static_cast<uint64_t>(cells + 1u) * static_cast<uint64_t>(cells + 1u);
    const uint64_t maximumChunks = kTerrainProceduralMaximumCandidateVertices / (std::max)(verticesPerChunk, 1ull);
    const uint64_t seamClosureChunkCountX = chunkCountX + 2u;
    const uint64_t seamClosureChunkCountZ = chunkCountZ + 2u;
    if (chunkCountX == 0u || chunkCountZ == 0u || chunkCountX > maximumChunks ||
        chunkCountZ > maximumChunks / chunkCountX || seamClosureChunkCountX > maximumChunks ||
        seamClosureChunkCountZ > maximumChunks / seamClosureChunkCountX) {
        preview.budgetExceeded = true;
        preview.candidateVertices = kTerrainProceduralMaximumCandidateVertices + 1u;
        return preview;
    }
    preview.candidateVertices = chunkCountX * chunkCountZ * verticesPerChunk;
    const float cellSize = SanitizeTerrainChunkCellSize(surface.chunkCellSize);
    float minimum = (std::numeric_limits<float>::max)();
    float maximum = -(std::numeric_limits<float>::max)();
    for (int32_t z = keyMin.z; z <= keyMax.z; ++z) {
        for (int32_t x = keyMin.x; x <= keyMax.x; ++x) {
            const TerrainChunkKey key{x, z};
            bool chunkAffected = false;
            const auto existing = surface.chunks.find(key);
            const TerrainChunk* existingChunk =
                existing == surface.chunks.end() || !TerrainChunkValid(existing->second) ? nullptr : &existing->second;
            const uint32_t stride = cells + 1u;
            for (uint32_t vz = 0u; vz <= cells; ++vz) {
                for (uint32_t vx = 0u; vx <= cells; ++vx) {
                    const float localX = static_cast<float>(x) * chunkSize + static_cast<float>(vx) * cellSize;
                    const float localZ = static_cast<float>(z) * chunkSize + static_cast<float>(vz) * cellSize;
                    const float mask = EvaluateTerrainProceduralMask(spec, localX, localZ);
                    if (mask <= 1.0e-5f) {
                        continue;
                    }
                    chunkAffected = true;
                    ++preview.affectedVertices;
                    const std::size_t index = static_cast<std::size_t>(vz) * stride + vx;
                    const float oldHeight = existingChunk != nullptr && index < existingChunk->heights.size()
                                                ? existingChunk->heights[index]
                                                : 0.0f;
                    const float generated = EvaluateTerrainProceduralHeight(spec, localX, localZ);
                    const float target = spec.baseHeight + generated;
                    const float amount = std::clamp(spec.blendAmount * mask, 0.0f, 1.0f);
                    float resultHeight = oldHeight;
                    switch (spec.blendMode) {
                    case TerrainProceduralBlendMode::Add:
                        // Add intentionally ignores baseHeight; there is
                        // no absolute target in this blend mode.
                        resultHeight += generated * amount;
                        break;
                    case TerrainProceduralBlendMode::Replace: resultHeight += (target - resultHeight) * amount; break;
                    case TerrainProceduralBlendMode::Max:
                        resultHeight += ((std::max)(resultHeight, target) - resultHeight) * amount;
                        break;
                    }
                    minimum = (std::min)(minimum, resultHeight);
                    maximum = (std::max)(maximum, resultHeight);
                }
            }
            if (chunkAffected) {
                ++preview.affectedChunks;
                if (!surface.chunks.contains(key)) {
                    ++preview.newChunks;
                }
            }
        }
    }
    preview.valid = preview.affectedChunks > 0u;
    preview.minimumGeneratedHeight = preview.valid ? minimum : 0.0f;
    preview.maximumGeneratedHeight = preview.valid ? maximum : 0.0f;
    return preview;
}

} // namespace HIKARI::TERRAIN
