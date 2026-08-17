#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <map>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"

namespace HIKARI {

    namespace {
        nlohmann::json SerializeVec3(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        MATH::Vec3 ReadVec3(
            const nlohmann::json& source,
            const char* key,
            const MATH::Vec3& fallback) {
            if (const nlohmann::json* value =
                    JSONREAD::FindMember(source, key);
                value != nullptr && value->is_array() &&
                value->size() >= 3u) {
                return MATH::Vec3{
                    (*value)[0].get<float>(),
                    (*value)[1].get<float>(),
                    (*value)[2].get<float>()
                };
            }
            return fallback;
        }

        nlohmann::json SerializeFloatArray(const std::vector<float>& values) {
            nlohmann::json out = nlohmann::json::array();
            for (float v : values) {
                out.push_back(v);
            }
            return out;
        }

        std::vector<float> ReadFloatArray(
            const nlohmann::json& source,
            const char* key) {
            std::vector<float> out;
            if (const nlohmann::json* arr =
                    JSONREAD::FindMember(source, key);
                arr != nullptr && arr->is_array()) {
                out.reserve(arr->size());
                for (const nlohmann::json& v : *arr) {
                    out.push_back(v.is_number() ? v.get<float>() : 0.0f);
                }
            }
            return out;
        }

        // ブラシのフォールオフ。hardness が中心の平らさ、falloff が
        // 縁のなだらかさを決める（成熟ツールの一般的なブラシモデル）。
        // hardness=1 でほぼ一定、falloff=1 で滑らかに 0 へ落ちる。
        float BrushFalloff(
            float distance,
            float radius,
            float hardness,
            float falloff) noexcept {
            if (radius <= 1.0e-4f || distance >= radius) {
                return 0.0f;
            }
            const float t = 1.0f - distance / radius;  // 中心 1 → 縁 0
            const float soft = t * t * (3.0f - 2.0f * t);  // smoothstep
            // hardness を上げると内側が平らになり、falloff を上げると
            // 縁の減衰が強まる。両者を掛けて最終強度とする。
            const float core = hardness + (1.0f - hardness) * soft;
            const float edge = 1.0f - falloff * (1.0f - soft);
            return core * edge;
        }
    }

    const TERRAIN::TerrainSurface&
        TerrainSurfaceComponent::GetSurface() const noexcept {
        return surface_;
    }

    void TerrainSurfaceComponent::SetSurface(
        const TERRAIN::TerrainSurface& surface) {
        surface_ = surface;
        ++revision_;
        NotifyRenderStateDirty();
    }

    uint64_t TerrainSurfaceComponent::GetRevision() const noexcept {
        return revision_;
    }

    uint32_t TerrainSurfaceComponent::GetLayerCount() const noexcept {
        return (std::max)(
            static_cast<uint32_t>(surface_.layerPresetIds.size()), 1u);
    }

    const TERRAIN::TerrainChunkMap&
        TerrainSurfaceComponent::GetChunks() const noexcept {
        return surface_.chunks;
    }

    uint32_t TerrainSurfaceComponent::GetChunkCells() const noexcept {
        return (std::max)(surface_.chunkCells, 1u);
    }

    float TerrainSurfaceComponent::GetChunkWorldSize() const noexcept {
        return static_cast<float>(GetChunkCells()) *
            (std::max)(surface_.chunkCellSize, 0.01f);
    }

    void TerrainSurfaceComponent::ApplySolidPreset(
        std::string_view presetId) {
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        const TERRAIN::SurfacePreset* found =
            TERRAIN::SurfaceLayerRegistry::Get().Find(presetId);
        std::optional<TERRAIN::SurfacePreset> fallback;
        if (found == nullptr) {
            fallback = TERRAIN::ResolveSurfacePreset(presetId);
            if (fallback.has_value()) {
                found = &*fallback;
            }
        }
        if (found == nullptr) {
            return;
        }
        TERRAIN::TerrainSurface next = surface_;
        next.presetId = std::string(presetId);
        next.material = found->material;
        next.deform = found->deform;
        next.shoreline = found->shoreline;
        SetSurface(next);
    }

    TERRAIN::TerrainBrush TerrainSurfaceComponent::GetBrush() const noexcept {
        return surface_.brush;
    }

    void TerrainSurfaceComponent::SetBrushMode(
        TERRAIN::TerrainBrushMode mode) {
        surface_.brush.mode = mode;
    }

    void TerrainSurfaceComponent::SetBrushRadius(float radius) {
        surface_.brush.radius = (std::max)(radius, 0.05f);
    }

    TERRAIN::TerrainChunk& TerrainSurfaceComponent::EnsureChunk(
        const TERRAIN::TerrainChunkKey& key) {
        auto it = surface_.chunks.find(key);
        if (it == surface_.chunks.end()) {
            it = surface_.chunks
                     .emplace(
                         key,
                         TERRAIN::MakeTerrainChunk(
                             GetChunkCells(),
                             GetLayerCount()))
                     .first;
        }
        return it->second;
    }

    template <typename Fn>
    void TerrainSurfaceComponent::ForEachChunkInBrush(
        const MATH::Vec3& worldPos,
        Fn&& fn) {
        const float chunkSize = GetChunkWorldSize();
        const float radius = (std::max)(surface_.brush.radius, 0.05f);
        const float minX = worldPos.x - radius;
        const float maxX = worldPos.x + radius;
        const float minZ = worldPos.z - radius;
        const float maxZ = worldPos.z + radius;
        const auto keyMin = TERRAIN::TerrainChunkKeyFromWorld(
            minX, minZ, chunkSize);
        const auto keyMax = TERRAIN::TerrainChunkKeyFromWorld(
            maxX, maxZ, chunkSize);
        for (int32_t kz = keyMin.z; kz <= keyMax.z; ++kz) {
            for (int32_t kx = keyMin.x; kx <= keyMax.x; ++kx) {
                const TERRAIN::TerrainChunkKey key{ kx, kz };
                TERRAIN::TerrainChunk& chunk = EnsureChunk(key);
                const MATH::Vec2 chunkOrigin{
                    static_cast<float>(kx) * chunkSize,
                    static_cast<float>(kz) * chunkSize
                };
                fn(chunk, chunkOrigin);
            }
        }
    }

    void TerrainSurfaceComponent::SculptAtWorld(
        const MATH::Vec3& worldPos,
        float deltaSeconds) {
        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        // strength をフレームレート非依存にするため dt を畳み込む。
        const float dtScale =
            std::clamp(deltaSeconds, 0.0f, 0.1f) * 60.0f;
        const float chunkSize = GetChunkWorldSize();
        const uint32_t cells = GetChunkCells();
        const float cellSize = chunkSize / static_cast<float>(cells);
        const bool isSmooth =
            brush.mode == TERRAIN::TerrainBrushMode::Smooth;
        // Smooth は近傍平均へ寄せるため、編集前の高さを参照する必要がある。
        // 書き込みながら読むと方向バイアスが出るので、先にスナップショット
        // を取り、それを読みながら新しい値を書く（2 パス相当）。
        std::map<TERRAIN::TerrainChunkKey, std::vector<float>>
            smoothSource{};
        if (isSmooth) {
            const float r = (std::max)(surface_.brush.radius, 0.05f);
            const auto kMin = TERRAIN::TerrainChunkKeyFromWorld(
                worldPos.x - r, worldPos.z - r, chunkSize);
            const auto kMax = TERRAIN::TerrainChunkKeyFromWorld(
                worldPos.x + r, worldPos.z + r, chunkSize);
            for (int32_t kz = kMin.z; kz <= kMax.z; ++kz) {
                for (int32_t kx = kMin.x; kx <= kMax.x; ++kx) {
                    const TERRAIN::TerrainChunkKey key{ kx, kz };
                    const auto it = surface_.chunks.find(key);
                    if (it != surface_.chunks.end()) {
                        smoothSource.emplace(key, it->second.heights);
                    }
                }
            }
        }
        // ワールド XZ の高さをスナップショットから読む（chunk 非依存）。
        const auto sampleSource = [&](float wx, float wz, float& outH) -> bool {
            if (!isSmooth) {
                return false;
            }
            const TERRAIN::TerrainChunkKey key =
                TERRAIN::TerrainChunkKeyFromWorld(wx, wz, chunkSize);
            const auto it = smoothSource.find(key);
            if (it == smoothSource.end()) {
                return false;
            }
            const std::vector<float>& src = it->second;
            const float localX = wx - static_cast<float>(key.x) * chunkSize;
            const float localZ = wz - static_cast<float>(key.z) * chunkSize;
            const float fx = std::clamp(
                localX / cellSize, 0.0f, static_cast<float>(cells));
            const float fz = std::clamp(
                localZ / cellSize, 0.0f, static_cast<float>(cells));
            const uint32_t x0 = static_cast<uint32_t>(fx);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t x1 = (std::min)(x0 + 1u, cells);
            const uint32_t z1 = (std::min)(z0 + 1u, cells);
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            const uint32_t stride = cells + 1u;
            const auto H = [&](uint32_t x, uint32_t z) {
                return src[static_cast<std::size_t>(z) * stride + x];
            };
            outH = std::lerp(
                std::lerp(H(x0, z0), H(x1, z0), tx),
                std::lerp(H(x0, z1), H(x1, z1), tx),
                tz);
            return true;
        };

        ForEachChunkInBrush(
            worldPos,
            [&](TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                const uint32_t stride = chunk.cells + 1u;
                for (uint32_t z = 0u; z <= chunk.cells; ++z) {
                    for (uint32_t x = 0u; x <= chunk.cells; ++x) {
                        const float wx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float wz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = wx - worldPos.x;
                        const float dz = wz - worldPos.z;
                        const float dist = std::sqrt(dx * dx + dz * dz);
                        const float falloff = BrushFalloff(
                            dist,
                            brush.radius,
                            brush.hardness,
                            brush.falloff);
                        if (falloff <= 0.0f) {
                            continue;
                        }
                        float& h = chunk.heights[
                            static_cast<std::size_t>(z) * stride + x];
                        // strength は 1 ストローク当たりの伸びとして扱い、
                        // heightScale を掛けて見た目の起伏スケールに合わせる。
                        const float amount =
                            brush.strength * dtScale * falloff *
                                surface_.heightScale;
                        switch (brush.mode) {
                        case TERRAIN::TerrainBrushMode::Raise:
                            h += amount;
                            break;
                        case TERRAIN::TerrainBrushMode::Lower:
                            h -= amount;
                            break;
                        case TERRAIN::TerrainBrushMode::Mound:
                            // 中心を強く持ち上げる盛り土。
                            h += amount * 2.0f;
                            break;
                        case TERRAIN::TerrainBrushMode::Pit:
                            // 中心を強く掘る穴。
                            h -= amount * 2.0f;
                            break;
                        case TERRAIN::TerrainBrushMode::Flatten: {
                            const float target = brush.flattenHeight;
                            h += (target - h) *
                                (brush.strength * falloff);
                            break;
                        }
                        case TERRAIN::TerrainBrushMode::Smooth: {
                            // 4 近傍との平均へ寄せる本物の平滑。
                            float self = 0.0f;
                            float sum = 0.0f;
                            uint32_t n = 0u;
                            if (sampleSource(wx, wz, self)) {
                                sum += self;
                                ++n;
                                float nv = 0.0f;
                                if (sampleSource(wx - cellSize, wz, nv)) { sum += nv; ++n; }
                                if (sampleSource(wx + cellSize, wz, nv)) { sum += nv; ++n; }
                                if (sampleSource(wx, wz - cellSize, nv)) { sum += nv; ++n; }
                                if (sampleSource(wx, wz + cellSize, nv)) { sum += nv; ++n; }
                            }
                            if (n > 1u) {
                                const float avg = sum / static_cast<float>(n);
                                h += (avg - h) *
                                    (brush.strength * falloff);
                            }
                            break;
                        }
                        }
                    }
                }
            });
        ++revision_;
        NotifyRenderStateDirty();
    }

    void TerrainSurfaceComponent::PaintLayerAtWorld(
        std::string_view presetId,
        const MATH::Vec3& worldPos) {
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        int layerIndex =
            TERRAIN::SurfaceLayerRegistry::Get().IndexOf(presetId);
        if (layerIndex < 0) {
            const std::optional<TERRAIN::SurfacePreset> fallback =
                TERRAIN::ResolveSurfacePreset(presetId);
            if (!fallback.has_value()) {
                return;
            }
            if (fallback->id == "sand") { layerIndex = 0; }
            else if (fallback->id == "snow") { layerIndex = 1; }
            else if (fallback->id == "dirt") { layerIndex = 2; }
            else { return; }
        }
        const uint32_t layer = static_cast<uint32_t>(layerIndex);

        // 層 preset の対応を記録する（着色 tint 解決用）。
        if (surface_.layerPresetIds.size() <
            static_cast<std::size_t>(layer) + 1u) {
            surface_.layerPresetIds.resize(
                static_cast<std::size_t>(layer) + 1u);
        }
        surface_.layerPresetIds[static_cast<std::size_t>(layer)] =
            std::string(presetId);

        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        const float chunkSize = GetChunkWorldSize();
        const uint32_t cells = GetChunkCells();
        const float cellSize = chunkSize / static_cast<float>(cells);

        ForEachChunkInBrush(
            worldPos,
            [&](TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                // 層数が足りなければ拡張する。
                if (chunk.layerCount <= layer) {
                    const uint32_t newCount = layer + 1u;
                    const std::size_t count =
                        TERRAIN::TerrainChunkVertexCount(chunk.cells);
                    std::vector<float> weights(
                        count * newCount, 0.0f);
                    for (std::size_t cell = 0u; cell < count; ++cell) {
                        weights[cell] = 1.0f; // 基底層
                    }
                    for (uint32_t l = 0u; l < chunk.layerCount; ++l) {
                        for (std::size_t cell = 0u; cell < count; ++cell) {
                            weights[
                                static_cast<std::size_t>(l) * count + cell] =
                                chunk.layerWeights[
                                    static_cast<std::size_t>(l) * count + cell];
                        }
                    }
                    chunk.layerCount = newCount;
                    chunk.layerWeights = std::move(weights);
                }

                const uint32_t stride = chunk.cells + 1u;
                const std::size_t count =
                    TERRAIN::TerrainChunkVertexCount(chunk.cells);
                for (uint32_t z = 0u; z <= chunk.cells; ++z) {
                    for (uint32_t x = 0u; x <= chunk.cells; ++x) {
                        const float wx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float wz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = wx - worldPos.x;
                        const float dz = wz - worldPos.z;
                        const float dist = std::sqrt(dx * dx + dz * dz);
                        const float falloff = BrushFalloff(
                            dist,
                            brush.radius,
                            brush.hardness,
                            brush.falloff);
                        if (falloff <= 0.0f) {
                            continue;
                        }
                        const std::size_t cell =
                            static_cast<std::size_t>(z) * stride + x;
                        const float amount = std::clamp(
                            brush.strength * falloff, 0.0f, 1.0f);
                        // 対象層を増やし、全体で正規化する。
                        chunk.layerWeights[
                            static_cast<std::size_t>(layer) * count + cell] +=
                            amount;
                        float total = 0.0f;
                        for (uint32_t l = 0u; l < chunk.layerCount; ++l) {
                            total += chunk.layerWeights[
                                static_cast<std::size_t>(l) * count + cell];
                        }
                        if (total > 1.0e-6f) {
                            for (uint32_t l = 0u; l < chunk.layerCount; ++l) {
                                chunk.layerWeights[
                                    static_cast<std::size_t>(l) * count + cell] /=
                                    total;
                            }
                        }
                        // ペイントした場所は実体化する。
                        chunk.coverage[cell] = 1.0f;
                    }
                }
            });
        ++revision_;
        NotifyRenderStateDirty();
    }

    void TerrainSurfaceComponent::PaintCoverageAtWorld(
        float target,
        const MATH::Vec3& worldPos) {
        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        const float goal = std::clamp(target, 0.0f, 1.0f);
        const float chunkSize = GetChunkWorldSize();
        const uint32_t cells = GetChunkCells();
        const float cellSize = chunkSize / static_cast<float>(cells);

        ForEachChunkInBrush(
            worldPos,
            [&](TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                const uint32_t stride = chunk.cells + 1u;
                for (uint32_t z = 0u; z <= chunk.cells; ++z) {
                    for (uint32_t x = 0u; x <= chunk.cells; ++x) {
                        const float wx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float wz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = wx - worldPos.x;
                        const float dz = wz - worldPos.z;
                        const float dist = std::sqrt(dx * dx + dz * dz);
                        const float falloff = BrushFalloff(
                            dist,
                            brush.radius,
                            brush.hardness,
                            brush.falloff);
                        if (falloff <= 0.0f) {
                            continue;
                        }
                        float& c = chunk.coverage[
                            static_cast<std::size_t>(z) * stride + x];
                        c += (goal - c) * (brush.strength * falloff);
                    }
                }
            });
        ++revision_;
        NotifyRenderStateDirty();
    }

    void TerrainSurfaceComponent::ResetTerrain() {
        surface_.chunks.clear();
        ++revision_;
        NotifyRenderStateDirty();
    }

    bool TerrainSurfaceComponent::SampleWorldHeight(
        const MATH::Vec3& worldPos,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {
        outHeight = 0.0f;
        outNormal = { 0.0f, 1.0f, 0.0f };
        const float chunkSize = GetChunkWorldSize();
        const TERRAIN::TerrainChunkKey key =
            TERRAIN::TerrainChunkKeyFromWorld(
                worldPos.x, worldPos.z, chunkSize);
        const auto it = surface_.chunks.find(key);
        if (it == surface_.chunks.end()) {
            return false;
        }
        const TERRAIN::TerrainChunk& chunk = it->second;
        const uint32_t cells = chunk.cells;
        const float cellSize = chunkSize / static_cast<float>(cells);
        const float localX =
            worldPos.x - static_cast<float>(key.x) * chunkSize;
        const float localZ =
            worldPos.z - static_cast<float>(key.z) * chunkSize;
        const float fx = std::clamp(
            localX / cellSize, 0.0f, static_cast<float>(cells));
        const float fz = std::clamp(
            localZ / cellSize, 0.0f, static_cast<float>(cells));
        const uint32_t x0 = static_cast<uint32_t>(fx);
        const uint32_t z0 = static_cast<uint32_t>(fz);
        const uint32_t x1 = (std::min)(x0 + 1u, cells);
        const uint32_t z1 = (std::min)(z0 + 1u, cells);
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);
        const uint32_t stride = cells + 1u;
        const auto H = [&](uint32_t x, uint32_t z) {
            return chunk.heights[static_cast<std::size_t>(z) * stride + x];
        };
        const float h00 = H(x0, z0);
        const float h10 = H(x1, z0);
        const float h01 = H(x0, z1);
        const float h11 = H(x1, z1);
        outHeight = std::lerp(
            std::lerp(h00, h10, tx),
            std::lerp(h01, h11, tx),
            tz);
        // 高さ場から法線を推定する。
        const float hL = H(x0 > 0u ? x0 - 1u : x0, z0);
        const float hR = H((std::min)(x0 + 1u, cells), z0);
        const float hD = H(x0, z0 > 0u ? z0 - 1u : z0);
        const float hU = H(x0, (std::min)(z0 + 1u, cells));
        MATH::Vec3 normal{
            (hL - hR) / (2.0f * cellSize),
            1.0f,
            (hD - hU) / (2.0f * cellSize)
        };
        const float lenSq = MATH::Dot(normal, normal);
        outNormal = lenSq > 1.0e-12f
            ? MATH::Normalize(normal)
            : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
        return true;
    }

    void TerrainSurfaceComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void TerrainSurfaceComponent::Serialize(nlohmann::json& out) const {
        const TERRAIN::TerrainSurface& s = surface_;
        nlohmann::json chunks = nlohmann::json::array();
        for (const auto& [key, chunk] : s.chunks) {
            chunks.push_back({
                { "x", key.x },
                { "z", key.z },
                { "cells", chunk.cells },
                { "layerCount", chunk.layerCount },
                { "heights", SerializeFloatArray(chunk.heights) },
                { "coverage", SerializeFloatArray(chunk.coverage) },
                { "layerWeights", SerializeFloatArray(chunk.layerWeights) },
            });
        }
        out = {
            { "_schemaVersion", 3u },
            { "chunked", true },
            { "presetId", s.presetId },
            { "heightScale", s.heightScale },
            { "chunkCells", s.chunkCells },
            { "chunkCellSize", s.chunkCellSize },
            { "material", {
                { "lightTint", SerializeVec3(s.material.lightTint) },
                { "darkTint", SerializeVec3(s.material.darkTint) },
                { "sparkleStrength", s.material.sparkleStrength },
                { "rippleAmount", s.material.rippleAmount },
                { "pitDarkening", s.material.pitDarkening },
                { "toonBandCount", s.material.toonBandCount },
                { "toonBandHardness", s.material.toonBandHardness },
            } },
            { "deform", {
                { "behavior", static_cast<uint32_t>(s.deform.behavior) },
                { "maxDepth", s.deform.maxDepth },
                { "stampRadius", s.deform.stampRadius },
                { "rimStrength", s.deform.rimStrength },
                { "refillPerSecond", s.deform.refillPerSecond },
                { "diffusionPerSecond", s.deform.diffusionPerSecond },
            } },
            { "shoreline", {
                { "edgeFalloff", s.shoreline.edgeFalloff },
                { "edgeDepth", s.shoreline.edgeDepth },
                { "wetness", s.shoreline.wetness },
            } },
            { "layerPresetIds", s.layerPresetIds },
            { "brush", {
                { "mode", TERRAIN::ToString(s.brush.mode) },
                { "radius", s.brush.radius },
                { "strength", s.brush.strength },
                { "hardness", s.brush.hardness },
                { "falloff", s.brush.falloff },
                { "flattenHeight", s.brush.flattenHeight },
            } },
            { "chunks", chunks },
        };
    }

    void TerrainSurfaceComponent::Deserialize(const nlohmann::json& in) {
        if (!in.is_object()) {
            return;
        }
        TERRAIN::TerrainSurface surface = surface_;
        surface.presetId = in.value("presetId", surface.presetId);
        surface.heightScale = JSONREAD::NumberMemberOr(
            in, "heightScale", surface.heightScale);
        surface.chunkCells = static_cast<uint32_t>(
            in.value("chunkCells", surface.chunkCells));
        surface.chunkCellSize = JSONREAD::NumberMemberOr(
            in, "chunkCellSize", surface.chunkCellSize);

        if (const nlohmann::json* material =
                JSONREAD::FindMember(in, "material");
            material != nullptr && material->is_object()) {
            surface.material.lightTint = ReadVec3(
                *material, "lightTint", surface.material.lightTint);
            surface.material.darkTint = ReadVec3(
                *material, "darkTint", surface.material.darkTint);
            surface.material.sparkleStrength = JSONREAD::NumberMemberOr(
                *material, "sparkleStrength", surface.material.sparkleStrength);
            surface.material.rippleAmount = JSONREAD::NumberMemberOr(
                *material, "rippleAmount", surface.material.rippleAmount);
            surface.material.pitDarkening = JSONREAD::NumberMemberOr(
                *material, "pitDarkening", surface.material.pitDarkening);
            surface.material.toonBandCount = static_cast<uint32_t>(
                material->value("toonBandCount", surface.material.toonBandCount));
            surface.material.toonBandHardness = JSONREAD::NumberMemberOr(
                *material, "toonBandHardness", surface.material.toonBandHardness);
        }
        if (const nlohmann::json* deform =
                JSONREAD::FindMember(in, "deform");
            deform != nullptr && deform->is_object()) {
            surface.deform.behavior = static_cast<TERRAIN::DeformBehaviorId>(
                deform->value("behavior", static_cast<uint32_t>(surface.deform.behavior)));
            surface.deform.maxDepth = JSONREAD::NumberMemberOr(
                *deform, "maxDepth", surface.deform.maxDepth);
            surface.deform.stampRadius = JSONREAD::NumberMemberOr(
                *deform, "stampRadius", surface.deform.stampRadius);
            surface.deform.rimStrength = JSONREAD::NumberMemberOr(
                *deform, "rimStrength", surface.deform.rimStrength);
            surface.deform.refillPerSecond = JSONREAD::NumberMemberOr(
                *deform, "refillPerSecond", surface.deform.refillPerSecond);
            surface.deform.diffusionPerSecond = JSONREAD::NumberMemberOr(
                *deform, "diffusionPerSecond", surface.deform.diffusionPerSecond);
        }
        if (const nlohmann::json* shoreline =
                JSONREAD::FindMember(in, "shoreline");
            shoreline != nullptr && shoreline->is_object()) {
            surface.shoreline.edgeFalloff = JSONREAD::NumberMemberOr(
                *shoreline, "edgeFalloff", surface.shoreline.edgeFalloff);
            surface.shoreline.edgeDepth = JSONREAD::NumberMemberOr(
                *shoreline, "edgeDepth", surface.shoreline.edgeDepth);
            surface.shoreline.wetness = JSONREAD::NumberMemberOr(
                *shoreline, "wetness", surface.shoreline.wetness);
        }
        if (const nlohmann::json* presetIds =
                JSONREAD::FindMember(in, "layerPresetIds");
            presetIds != nullptr && presetIds->is_array()) {
            surface.layerPresetIds.clear();
            for (const nlohmann::json& v : *presetIds) {
                surface.layerPresetIds.push_back(
                    v.is_string() ? v.get<std::string>() : "");
            }
        }
        if (const nlohmann::json* brush =
                JSONREAD::FindMember(in, "brush");
            brush != nullptr && brush->is_object()) {
            surface.brush.mode = TERRAIN::ParseTerrainBrushMode(
                brush->value("mode", std::string(TERRAIN::ToString(surface.brush.mode))),
                surface.brush.mode);
            surface.brush.radius = JSONREAD::NumberMemberOr(
                *brush, "radius", surface.brush.radius);
            surface.brush.strength = JSONREAD::NumberMemberOr(
                *brush, "strength", surface.brush.strength);
            surface.brush.hardness = JSONREAD::NumberMemberOr(
                *brush, "hardness", surface.brush.hardness);
            surface.brush.falloff = JSONREAD::NumberMemberOr(
                *brush, "falloff", surface.brush.falloff);
            surface.brush.flattenHeight = JSONREAD::NumberMemberOr(
                *brush, "flattenHeight", surface.brush.flattenHeight);
        }

        surface.chunks.clear();
        if (const nlohmann::json* chunks =
                JSONREAD::FindMember(in, "chunks");
            chunks != nullptr && chunks->is_array()) {
            for (const nlohmann::json& c : *chunks) {
                if (!c.is_object()) {
                    continue;
                }
                TERRAIN::TerrainChunkKey key{
                    c.value("x", 0),
                    c.value("z", 0)
                };
                TERRAIN::TerrainChunk chunk{};
                chunk.cells = static_cast<uint32_t>(
                    c.value("cells", surface.chunkCells));
                chunk.layerCount = static_cast<uint32_t>(
                    c.value("layerCount", 1u));
                chunk.heights = ReadFloatArray(c, "heights");
                chunk.coverage = ReadFloatArray(c, "coverage");
                chunk.layerWeights = ReadFloatArray(c, "layerWeights");
                if (TERRAIN::TerrainChunkValid(chunk)) {
                    surface.chunks.emplace(key, std::move(chunk));
                }
            }
        }

        SetSurface(surface);
    }

    void TerrainSurfaceComponent::BuildInspector(
        IInspectorBuilder& builder) {
        constexpr std::array<AuthoringChoice, 3u> kSolidPresets{ {
            { "sand", "Sand" },
            { "snow", "Snow" },
            { "dirt", "Dirt" },
        } };
        if (builder.Section("Terrain Surface", true)) {
            int solidIndex = 0;
            if (surface_.presetId == "snow") { solidIndex = 1; }
            else if (surface_.presetId == "dirt") { solidIndex = 2; }
            if (builder.Choice(
                    AuthoringFieldDescriptor{
                        "terrain.solidPreset",
                        "Solid Preset",
                        "Solid surface appearance and deformation preset."
                    },
                    solidIndex,
                    kSolidPresets)) {
                ApplySolidPreset(kSolidPresets[
                    static_cast<std::size_t>(solidIndex)].id);
            }
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.heightScale", "Height Scale",
                    "Overall dent depth scale in meters."
                },
                surface_.heightScale, 0.0f, 2.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeFalloff", "Edge Falloff",
                    "Shoreline transition width in meters."
                },
                surface_.shoreline.edgeFalloff, 0.0f, 40.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.edgeDepth", "Edge Depth",
                    "Shoreline sink depth in meters."
                },
                surface_.shoreline.edgeDepth, 0.0f, 20.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.wetness", "Wetness",
                    "Shoreline wet-sand strength (0 disables)."
                },
                surface_.shoreline.wetness, 0.0f, 1.0f, 0.01f);
        }
        if (builder.Section("Brush", false)) {
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.radius", "Radius", "Brush radius in meters."
                },
                surface_.brush.radius, 0.05f, 100.0f, 0.1f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.strength", "Strength", "Brush strength 0..1."
                },
                surface_.brush.strength, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.hardness", "Hardness", "Inner hardness 0..1."
                },
                surface_.brush.hardness, 0.0f, 1.0f, 0.01f);
            builder.FloatRange(
                AuthoringFieldDescriptor{
                    "terrain.brush.falloff", "Falloff", "Outer soft falloff 0..1."
                },
                surface_.brush.falloff, 0.0f, 1.0f, 0.01f);
        }
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
