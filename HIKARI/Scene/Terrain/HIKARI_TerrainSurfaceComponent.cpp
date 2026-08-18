#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <map>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
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
            const float normalized = distance / radius;
            const float coreRadius = std::clamp(hardness, 0.0f, 0.999f);
            if (normalized <= coreRadius) {
                return 1.0f;
            }
            const float edgeT = std::clamp(
                (1.0f - normalized) / (1.0f - coreRadius),
                0.0f,
                1.0f);
            const float smooth = edgeT * edgeT * (3.0f - 2.0f * edgeT);
            return std::lerp(edgeT, smooth, std::clamp(falloff, 0.0f, 1.0f));
        }

        uint64_t PackTerrainVertexKey(
            int32_t x,
            int32_t z) noexcept {
            return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u) |
                static_cast<uint32_t>(z);
        }

        struct BrushVertexRange {
            uint32_t minX = 1u;
            uint32_t maxX = 0u;
            uint32_t minZ = 1u;
            uint32_t maxZ = 0u;

            bool IsValid() const noexcept {
                return minX <= maxX && minZ <= maxZ;
            }
        };

        BrushVertexRange ResolveBrushVertexRange(
            const MATH::Vec3& localPos,
            float radius,
            const MATH::Vec2& chunkOrigin,
            float cellSize,
            uint32_t cells) noexcept {
            const float safeCellSize = (std::max)(cellSize, 1.0e-4f);
            const auto clampVertex = [cells](int32_t value) {
                return static_cast<uint32_t>(std::clamp(
                    value, 0, static_cast<int32_t>(cells)));
            };
            BrushVertexRange range{};
            range.minX = clampVertex(static_cast<int32_t>(std::floor(
                (localPos.x - radius - chunkOrigin.x) / safeCellSize)));
            range.maxX = clampVertex(static_cast<int32_t>(std::ceil(
                (localPos.x + radius - chunkOrigin.x) / safeCellSize)));
            range.minZ = clampVertex(static_cast<int32_t>(std::floor(
                (localPos.z - radius - chunkOrigin.y) / safeCellSize)));
            range.maxZ = clampVertex(static_cast<int32_t>(std::ceil(
                (localPos.z + radius - chunkOrigin.y) / safeCellSize)));
            return range;
        }
    }

    const TERRAIN::TerrainSurface&
        TerrainSurfaceComponent::GetSurface() const noexcept {
        return surface_;
    }

    void TerrainSurfaceComponent::SetSurface(
        const TERRAIN::TerrainSurface& surface) {
        surface_ = surface;
        for (auto it = surface_.chunks.begin();
            it != surface_.chunks.end();) {
            if (!TERRAIN::TerrainChunkValid(it->second) ||
                it->second.cells != GetChunkCells()) {
                it = surface_.chunks.erase(it);
            } else {
                ++it;
            }
        }
        StitchChunkBorders();
        ++revision_;
        MarkGeometryChanged();
        ++coverageRevision_;
        InvalidateCoverageCache();
        NotifyRenderStateDirty();
    }

    uint64_t TerrainSurfaceComponent::GetRevision() const noexcept {
        return revision_;
    }

    uint64_t TerrainSurfaceComponent::GetGeometryRevision() const noexcept {
        return geometryRevision_;
    }

    uint64_t TerrainSurfaceComponent::GetCoverageRevision() const noexcept {
        return coverageRevision_;
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

    void TerrainSurfaceComponent::SetBrush(
        const TERRAIN::TerrainBrush& brush) noexcept {
        surface_.brush = TERRAIN::SanitizeTerrainBrush(brush);
    }

    void TerrainSurfaceComponent::SetBrushMode(
        TERRAIN::TerrainBrushMode mode) {
        surface_.brush.mode = mode;
    }

    void TerrainSurfaceComponent::SetBrushRadius(float radius) {
        surface_.brush.radius = (std::max)(radius, 0.05f);
    }

    void TerrainSurfaceComponent::SetDeformParams(
        const TERRAIN::DeformParams& deform) {
        const TERRAIN::DeformParams& current = surface_.deform;
        if (current.behavior == deform.behavior &&
            current.maxDepth == deform.maxDepth &&
            current.stampRadius == deform.stampRadius &&
            current.rimStrength == deform.rimStrength &&
            current.responseResolutionScale ==
                deform.responseResolutionScale &&
            current.restDepthScale == deform.restDepthScale &&
            current.minimumDepth == deform.minimumDepth &&
            current.settleStartRatio == deform.settleStartRatio &&
            current.settleSeconds == deform.settleSeconds &&
            current.impactDepthScale == deform.impactDepthScale &&
            current.rimWidthScale == deform.rimWidthScale &&
            current.refillPerSecond == deform.refillPerSecond &&
            current.diffusionPerSecond == deform.diffusionPerSecond &&
            current.plasticity == deform.plasticity &&
            current.plasticRefillPerSecond ==
                deform.plasticRefillPerSecond &&
            current.recoveryDelaySeconds == deform.recoveryDelaySeconds) {
            return;
        }
        const bool collisionGeometryChanged =
            current.rimStrength != deform.rimStrength &&
            surface_.shoreline.edgeDepth <= 1.0e-4f;
        surface_.deform = deform;
        ++revision_;
        if (collisionGeometryChanged) {
            MarkGeometryChanged();
        }
        NotifyRenderStateDirty();
    }

    void TerrainSurfaceComponent::SetShorelineParams(
        const TERRAIN::ShorelineParams& shoreline) {
        const TERRAIN::ShorelineParams& current = surface_.shoreline;
        if (current.edgeFalloff == shoreline.edgeFalloff &&
            current.edgeDepth == shoreline.edgeDepth &&
            current.wetness == shoreline.wetness) {
            return;
        }
        const bool coverageChanged =
            current.edgeFalloff != shoreline.edgeFalloff;
        const bool collisionGeometryChanged = coverageChanged ||
            current.edgeDepth != shoreline.edgeDepth;
        surface_.shoreline = shoreline;
        ++revision_;
        if (collisionGeometryChanged) {
            MarkGeometryChanged();
        }
        if (coverageChanged) {
            ++coverageRevision_;
            InvalidateCoverageCache();
        }
        NotifyRenderStateDirty();
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
            std::fill(
                it->second.coverage.begin(),
                it->second.coverage.end(),
                0.0f);
            MarkCoverageChunkDirty(key);
        }
        return it->second;
    }

    MATH::Vec3 TerrainSurfaceComponent::WorldToLocal(
        const MATH::Vec3& worldPos) const noexcept {
        const GameObject* owner = GetOwner();
        if (owner == nullptr) {
            return worldPos;
        }
        const MATH::Mat4 inv =
            MATH::Inverse(owner->GetTransform().GetWorldMatrix());
        const MATH::Vec4 local =
            inv.TransformPoint({ worldPos.x, worldPos.y, worldPos.z, 1.0f });
        if (std::fabs(local.w) > 1.0e-6f) {
            const float invW = 1.0f / local.w;
            return { local.x * invW, local.y * invW, local.z * invW };
        }
        return { local.x, local.y, local.z };
    }

    MATH::Vec3 TerrainSurfaceComponent::LocalToWorld(
        const MATH::Vec3& localPos) const noexcept {
        const GameObject* owner = GetOwner();
        if (owner == nullptr) {
            return localPos;
        }
        const MATH::Vec4 world =
            owner->GetTransform().GetWorldMatrix().TransformPoint({
                localPos.x, localPos.y, localPos.z, 1.0f });
        if (std::fabs(world.w) > 1.0e-6f) {
            const float invW = 1.0f / world.w;
            return { world.x * invW, world.y * invW, world.z * invW };
        }
        return { world.x, world.y, world.z };
    }

    void TerrainSurfaceComponent::StitchChunkBorders() {
        if (surface_.chunks.empty()) {
            return;
        }
        std::vector<TERRAIN::TerrainChunkKey> allChunks{};
        allChunks.reserve(surface_.chunks.size());
        for (const auto& [key, chunk] : surface_.chunks) {
            (void)chunk;
            allChunks.push_back(key);
        }
        (void)StitchChunkBorders(allChunks);
    }

    bool TerrainSurfaceComponent::StitchChunkBorders(
        const std::vector<TERRAIN::TerrainChunkKey>& touchedChunks) {
        if (touchedChunks.empty() || surface_.chunks.empty()) {
            return false;
        }

        // 筆が触れた chunk と 8 近傍だけを seam island とする。共有頂点を
        // 書き戻す両側を必ず含めつつ、遠方 chunk の境界走査を避ける。
        std::set<TERRAIN::TerrainChunkKey> affected{};
        for (const TERRAIN::TerrainChunkKey& touched : touchedChunks) {
            for (int32_t dz = -1; dz <= 1; ++dz) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const TERRAIN::TerrainChunkKey candidate{
                        touched.x + dx,
                        touched.z + dz
                    };
                    if (surface_.chunks.contains(candidate)) {
                        affected.insert(candidate);
                    }
                }
            }
        }
        if (affected.empty()) {
            return false;
        }

        bool coverageChanged = false;

        uint32_t layerCount = GetLayerCount();
        for (const TERRAIN::TerrainChunkKey& key : affected) {
            const TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
            layerCount = (std::max)(layerCount, chunk.layerCount);
        }
        layerCount = std::clamp(
            layerCount, 1u, TERRAIN::kTerrainChunkMaxLayers);

        for (const TERRAIN::TerrainChunkKey& key : affected) {
            TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
            if (chunk.layerCount == layerCount) {
                continue;
            }
            const std::size_t count =
                TERRAIN::TerrainChunkVertexCount(chunk.cells);
            std::vector<float> expanded(
                count * static_cast<std::size_t>(layerCount), 0.0f);
            for (uint32_t layer = 0u;
                layer < (std::min)(chunk.layerCount, layerCount);
                ++layer) {
                std::copy_n(
                    chunk.layerWeights.begin() +
                        static_cast<std::size_t>(layer) * count,
                    count,
                    expanded.begin() +
                        static_cast<std::size_t>(layer) * count);
            }
            chunk.layerCount = layerCount;
            chunk.layerWeights = std::move(expanded);
        }

        struct BorderValue {
            double height = 0.0;
            double coverage = 0.0;
            std::array<double, TERRAIN::kTerrainChunkMaxLayers> weights{};
            uint32_t count = 0u;
        };
        using VertexKey = std::pair<int64_t, int64_t>;
        std::map<VertexKey, BorderValue> values{};
        for (const TERRAIN::TerrainChunkKey& key : affected) {
            const TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
            const uint32_t cells = chunk.cells;
            const uint32_t stride = cells + 1u;
            const std::size_t vertexCount =
                TERRAIN::TerrainChunkVertexCount(cells);
            for (uint32_t z = 0u; z <= cells; ++z) {
                for (uint32_t x = 0u; x <= cells; ++x) {
                    if (x != 0u && x != cells &&
                        z != 0u && z != cells) {
                        continue;
                    }
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    const VertexKey vertex{
                        static_cast<int64_t>(key.x) * cells + x,
                        static_cast<int64_t>(key.z) * cells + z };
                    BorderValue& value = values[vertex];
                    value.height += chunk.heights[index];
                    value.coverage += chunk.coverage[index];
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        value.weights[layer] += chunk.layerWeights[
                            static_cast<std::size_t>(layer) * vertexCount + index];
                    }
                    ++value.count;
                }
            }
        }

        for (const TERRAIN::TerrainChunkKey& key : affected) {
            TERRAIN::TerrainChunk& chunk = surface_.chunks.at(key);
            const uint32_t cells = chunk.cells;
            const uint32_t stride = cells + 1u;
            const std::size_t vertexCount =
                TERRAIN::TerrainChunkVertexCount(cells);
            for (uint32_t z = 0u; z <= cells; ++z) {
                for (uint32_t x = 0u; x <= cells; ++x) {
                    if (x != 0u && x != cells &&
                        z != 0u && z != cells) {
                        continue;
                    }
                    const VertexKey vertex{
                        static_cast<int64_t>(key.x) * cells + x,
                        static_cast<int64_t>(key.z) * cells + z };
                    const BorderValue& value = values.at(vertex);
                    const float inv = 1.0f /
                        static_cast<float>((std::max)(value.count, 1u));
                    const std::size_t index =
                        static_cast<std::size_t>(z) * stride + x;
                    const float oldCoverage = chunk.coverage[index];
                    chunk.heights[index] =
                        static_cast<float>(value.height) * inv;
                    chunk.coverage[index] = std::clamp(
                        static_cast<float>(value.coverage) * inv,
                        0.0f,
                        1.0f);
                    if (std::fabs(
                            chunk.coverage[index] - oldCoverage) >
                        1.0e-7f) {
                        coverageChanged = true;
                        MarkCoverageVertexDirty(key, x, z);
                    }
                    float total = 0.0f;
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        float& weight = chunk.layerWeights[
                            static_cast<std::size_t>(layer) * vertexCount + index];
                        weight = (std::max)(
                            static_cast<float>(value.weights[layer]) * inv,
                            0.0f);
                        total += weight;
                    }
                    if (total <= 1.0e-6f) {
                        chunk.layerWeights[index] = 1.0f;
                    } else {
                        for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                            chunk.layerWeights[
                                static_cast<std::size_t>(layer) * vertexCount + index] /= total;
                        }
                    }
                }
            }
        }
        return coverageChanged;
    }

    template <typename Fn>
    void TerrainSurfaceComponent::ForEachChunkInBrushLocal(
        const MATH::Vec3& localPos,
        float radius,
        bool createMissingChunks,
        Fn&& fn) {
        const float chunkSize = GetChunkWorldSize();
        const float r = (std::max)(radius, 0.05f);
        const auto keyMin = TERRAIN::TerrainChunkKeyFromWorld(
            localPos.x - r, localPos.z - r, chunkSize);
        const auto keyMax = TERRAIN::TerrainChunkKeyFromWorld(
            localPos.x + r, localPos.z + r, chunkSize);
        for (int32_t kz = keyMin.z; kz <= keyMax.z; ++kz) {
            for (int32_t kx = keyMin.x; kx <= keyMax.x; ++kx) {
                const MATH::Vec2 chunkOrigin{
                    static_cast<float>(kx) * chunkSize,
                    static_cast<float>(kz) * chunkSize
                };
                const float nearestX = std::clamp(
                    localPos.x,
                    chunkOrigin.x,
                    chunkOrigin.x + chunkSize);
                const float nearestZ = std::clamp(
                    localPos.z,
                    chunkOrigin.y,
                    chunkOrigin.y + chunkSize);
                const float dx = nearestX - localPos.x;
                const float dz = nearestZ - localPos.z;
                if (dx * dx + dz * dz >= r * r) {
                    continue;
                }
                const float cellSize = chunkSize /
                    static_cast<float>(GetChunkCells());
                const float nearestVertexX = chunkOrigin.x +
                    std::clamp(
                        std::round(
                            (localPos.x - chunkOrigin.x) / cellSize),
                        0.0f,
                        static_cast<float>(GetChunkCells())) * cellSize;
                const float nearestVertexZ = chunkOrigin.y +
                    std::clamp(
                        std::round(
                            (localPos.z - chunkOrigin.y) / cellSize),
                        0.0f,
                        static_cast<float>(GetChunkCells())) * cellSize;
                const float vertexDx = nearestVertexX - localPos.x;
                const float vertexDz = nearestVertexZ - localPos.z;
                if (vertexDx * vertexDx + vertexDz * vertexDz >= r * r) {
                    continue;
                }
                const TERRAIN::TerrainChunkKey key{ kx, kz };
                auto found = surface_.chunks.find(key);
                if (found == surface_.chunks.end() &&
                    !createMissingChunks) {
                    continue;
                }
                TERRAIN::TerrainChunk& chunk =
                    found != surface_.chunks.end()
                        ? found->second
                        : EnsureChunk(key);
                fn(key, chunk, chunkOrigin);
            }
        }
    }

    bool TerrainSurfaceComponent::SculptAtWorld(
        const MATH::Vec3& worldPos,
        float deltaSeconds) {
        return SculptAtLocal(WorldToLocal(worldPos), deltaSeconds);
    }

    bool TerrainSurfaceComponent::SculptAtLocal(
        const MATH::Vec3& localPos,
        float deltaSeconds) {
        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        // strength をフレームレート非依存にするため dt を畳み込む。
        const float dtScale =
            std::clamp(deltaSeconds, 0.0f, 0.1f);
        if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
            return false;
        }
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
                localPos.x - r, localPos.z - r, chunkSize);
            const auto kMax = TERRAIN::TerrainChunkKeyFromWorld(
                localPos.x + r, localPos.z + r, chunkSize);
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
        const auto sampleSource = [&](float lx, float lz, float& outH) -> bool {
            if (!isSmooth) {
                return false;
            }
            const TERRAIN::TerrainChunkKey key =
                TERRAIN::TerrainChunkKeyFromWorld(lx, lz, chunkSize);
            const auto it = smoothSource.find(key);
            if (it == smoothSource.end()) {
                return false;
            }
            const std::vector<float>& src = it->second;
            const float localX = lx - static_cast<float>(key.x) * chunkSize;
            const float localZ = lz - static_cast<float>(key.z) * chunkSize;
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

        bool changed = false;
        bool coverageChanged = false;
        std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
        ForEachChunkInBrushLocal(
            localPos,
            brush.radius,
            true,
            [&](const TERRAIN::TerrainChunkKey& key,
                TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                touchedChunks.push_back(key);
                const uint32_t stride = chunk.cells + 1u;
                const BrushVertexRange range = ResolveBrushVertexRange(
                    localPos,
                    brush.radius,
                    chunkOrigin,
                    cellSize,
                    chunk.cells);
                if (!range.IsValid()) {
                    return;
                }
                for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
                    for (uint32_t x = range.minX; x <= range.maxX; ++x) {
                        const float lx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float lz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = lx - localPos.x;
                        const float dz = lz - localPos.z;
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
                        float& coverage = chunk.coverage[
                            static_cast<std::size_t>(z) * stride + x];
                        const float oldHeight = h;
                        const float oldCoverage = coverage;
                        coverage = (std::max)(coverage, falloff);
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
                            h += (target - h) * std::clamp(
                                brush.strength * dtScale * falloff,
                                0.0f,
                                1.0f);
                            break;
                        }
                        case TERRAIN::TerrainBrushMode::Smooth: {
                            // 4 近傍との平均へ寄せる本物の平滑。
                            float self = 0.0f;
                            float sum = 0.0f;
                            uint32_t n = 0u;
                            if (sampleSource(lx, lz, self)) {
                                sum += self;
                                ++n;
                                float nv = 0.0f;
                                if (sampleSource(lx - cellSize, lz, nv)) { sum += nv; ++n; }
                                if (sampleSource(lx + cellSize, lz, nv)) { sum += nv; ++n; }
                                if (sampleSource(lx, lz - cellSize, nv)) { sum += nv; ++n; }
                                if (sampleSource(lx, lz + cellSize, nv)) { sum += nv; ++n; }
                            }
                            if (n > 1u) {
                                const float avg = sum / static_cast<float>(n);
                                h += (avg - h) * std::clamp(
                                    brush.strength * dtScale * falloff,
                                    0.0f,
                                    1.0f);
                            }
                            break;
                        }
                        }
                        const bool vertexCoverageChanged =
                            std::fabs(coverage - oldCoverage) > 1.0e-7f;
                        changed = changed ||
                            std::fabs(h - oldHeight) > 1.0e-7f ||
                            vertexCoverageChanged;
                        coverageChanged = coverageChanged ||
                            vertexCoverageChanged;
                        if (vertexCoverageChanged) {
                            MarkCoverageVertexDirty(key, x, z);
                        }
                    }
                }
            });
        if (!changed) {
            return false;
        }
        coverageChanged =
            StitchChunkBorders(touchedChunks) || coverageChanged;
        ++revision_;
        MarkGeometryChanged();
        if (coverageChanged) {
            ++coverageRevision_;
        }
        NotifyRenderStateDirty();
        return true;
    }

    bool TerrainSurfaceComponent::PaintLayerAtWorld(
        std::string_view presetId,
        const MATH::Vec3& worldPos,
        float deltaSeconds) {
        return PaintLayerAtLocal(
            presetId,
            WorldToLocal(worldPos),
            deltaSeconds);
    }

    bool TerrainSurfaceComponent::PaintLayerAtLocal(
        std::string_view presetId,
        const MATH::Vec3& localPos,
        float deltaSeconds) {
        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        const float dtScale = std::clamp(deltaSeconds, 0.0f, 1.0f);
        if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
            return false;
        }
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        int layerIndex =
            TERRAIN::SurfaceLayerRegistry::Get().IndexOf(presetId);
        if (layerIndex < 0) {
            const std::optional<TERRAIN::SurfacePreset> fallback =
                TERRAIN::ResolveSurfacePreset(presetId);
            if (!fallback.has_value()) {
                return false;
            }
            if (fallback->id == "sand") { layerIndex = 0; }
            else if (fallback->id == "snow") { layerIndex = 1; }
            else if (fallback->id == "dirt") { layerIndex = 2; }
            else { return false; }
        }
        const uint32_t layer = static_cast<uint32_t>(layerIndex);

        // 層 preset の対応を記録する（着色 tint 解決用）。
        if (surface_.layerPresetIds.size() <
            static_cast<std::size_t>(layer) + 1u) {
            surface_.layerPresetIds.resize(
                static_cast<std::size_t>(layer) + 1u);
        }
        const std::string nextPresetId(presetId);
        bool changed =
            surface_.layerPresetIds[static_cast<std::size_t>(layer)] !=
                nextPresetId;
        surface_.layerPresetIds[static_cast<std::size_t>(layer)] =
            nextPresetId;
        bool coverageChanged = false;

        const float chunkSize = GetChunkWorldSize();
        const uint32_t cells = GetChunkCells();
        const float cellSize = chunkSize / static_cast<float>(cells);

        std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
        ForEachChunkInBrushLocal(
            localPos,
            brush.radius,
            true,
            [&](const TERRAIN::TerrainChunkKey& key,
                TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                touchedChunks.push_back(key);
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
                    changed = true;
                }

                const uint32_t stride = chunk.cells + 1u;
                const std::size_t count =
                    TERRAIN::TerrainChunkVertexCount(chunk.cells);
                const BrushVertexRange range = ResolveBrushVertexRange(
                    localPos,
                    brush.radius,
                    chunkOrigin,
                    cellSize,
                    chunk.cells);
                if (!range.IsValid()) {
                    return;
                }
                for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
                    for (uint32_t x = range.minX; x <= range.maxX; ++x) {
                        const float lx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float lz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = lx - localPos.x;
                        const float dz = lz - localPos.z;
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
                            brush.strength * dtScale * falloff,
                            0.0f,
                            1.0f);
                        const float oldCoverage = chunk.coverage[cell];
                        const float oldTargetWeight = chunk.layerWeights[
                            static_cast<std::size_t>(layer) * count + cell];
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
                        // 新規 chunk は coverage=0 から始め、筆の勾配で
                        // 実体化する。四角い chunk 輪郭を露出させない。
                        chunk.coverage[cell] = (std::max)(
                            chunk.coverage[cell], falloff);
                        const bool vertexCoverageChanged =
                            std::fabs(chunk.coverage[cell] - oldCoverage) >
                                1.0e-7f;
                        changed = changed ||
                            std::fabs(chunk.layerWeights[
                                static_cast<std::size_t>(layer) * count + cell] -
                                oldTargetWeight) > 1.0e-7f ||
                            vertexCoverageChanged;
                        coverageChanged = coverageChanged ||
                            vertexCoverageChanged;
                        if (vertexCoverageChanged) {
                            MarkCoverageVertexDirty(key, x, z);
                        }
                    }
                }
            });
        if (!changed) {
            return false;
        }
        coverageChanged =
            StitchChunkBorders(touchedChunks) || coverageChanged;
        ++revision_;
        if (coverageChanged) {
            MarkGeometryChanged();
            ++coverageRevision_;
        }
        NotifyRenderStateDirty();
        return true;
    }

    bool TerrainSurfaceComponent::PaintCoverageAtWorld(
        float target,
        const MATH::Vec3& worldPos,
        float deltaSeconds) {
        return PaintCoverageAtLocal(
            target,
            WorldToLocal(worldPos),
            deltaSeconds);
    }

    bool TerrainSurfaceComponent::PaintCoverageAtLocal(
        float target,
        const MATH::Vec3& localPos,
        float deltaSeconds) {
        const TERRAIN::TerrainBrush brush =
            TERRAIN::SanitizeTerrainBrush(surface_.brush);
        const float dtScale = std::clamp(deltaSeconds, 0.0f, 1.0f);
        if (brush.strength <= 1.0e-6f || dtScale <= 0.0f) {
            return false;
        }
        const float goal = std::clamp(target, 0.0f, 1.0f);
        const float chunkSize = GetChunkWorldSize();
        const uint32_t cells = GetChunkCells();
        const float cellSize = chunkSize / static_cast<float>(cells);

        bool changed = false;
        std::vector<TERRAIN::TerrainChunkKey> touchedChunks{};
        ForEachChunkInBrushLocal(
            localPos,
            brush.radius,
            goal > 1.0e-6f,
            [&](const TERRAIN::TerrainChunkKey& key,
                TERRAIN::TerrainChunk& chunk,
                const MATH::Vec2& chunkOrigin) {
                touchedChunks.push_back(key);
                const uint32_t stride = chunk.cells + 1u;
                const BrushVertexRange range = ResolveBrushVertexRange(
                    localPos,
                    brush.radius,
                    chunkOrigin,
                    cellSize,
                    chunk.cells);
                if (!range.IsValid()) {
                    return;
                }
                for (uint32_t z = range.minZ; z <= range.maxZ; ++z) {
                    for (uint32_t x = range.minX; x <= range.maxX; ++x) {
                        const float lx = chunkOrigin.x +
                            static_cast<float>(x) * cellSize;
                        const float lz = chunkOrigin.y +
                            static_cast<float>(z) * cellSize;
                        const float dx = lx - localPos.x;
                        const float dz = lz - localPos.z;
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
                        const float oldCoverage = c;
                        c += (goal - c) * std::clamp(
                            brush.strength * dtScale * falloff,
                            0.0f,
                            1.0f);
                        const bool vertexCoverageChanged =
                            std::fabs(c - oldCoverage) > 1.0e-7f;
                        changed = changed || vertexCoverageChanged;
                        if (vertexCoverageChanged) {
                            MarkCoverageVertexDirty(key, x, z);
                        }
                    }
                }
            });
        if (!changed) {
            return false;
        }
        (void)StitchChunkBorders(touchedChunks);
        ++revision_;
        MarkGeometryChanged();
        ++coverageRevision_;
        NotifyRenderStateDirty();
        return true;
    }

    void TerrainSurfaceComponent::ResetTerrain() {
        if (surface_.chunks.empty()) {
            return;
        }
        surface_.chunks.clear();
        ++revision_;
        MarkGeometryChanged();
        ++coverageRevision_;
        InvalidateCoverageCache();
        NotifyRenderStateDirty();
    }

    void TerrainSurfaceComponent::BeginGeometryEditBatch() noexcept {
        if (geometryEditBatchActive_) {
            return;
        }
        geometryEditBatchActive_ = true;
        geometryEditBatchDirty_ = false;
    }

    bool TerrainSurfaceComponent::EndGeometryEditBatch() noexcept {
        if (!geometryEditBatchActive_) {
            return false;
        }
        geometryEditBatchActive_ = false;
        const bool changed = geometryEditBatchDirty_;
        geometryEditBatchDirty_ = false;
        if (changed) {
            ++geometryRevision_;
        }
        return changed;
    }

    void TerrainSurfaceComponent::MarkGeometryChanged() noexcept {
        if (geometryEditBatchActive_) {
            geometryEditBatchDirty_ = true;
            return;
        }
        ++geometryRevision_;
    }

    void TerrainSurfaceComponent::InvalidateCoverageCache() noexcept {
        coverageCache_.clear();
        coverageDistanceCache_.clear();
        coverageCacheRevision_ = 0u;
        coverageCacheInitialized_ = false;
        coverageCacheFullRebuild_ = true;
        coverageDirtyValid_ = false;
    }

    void TerrainSurfaceComponent::MarkCoverageVertexDirty(
        const TERRAIN::TerrainChunkKey& chunkKey,
        uint32_t localX,
        uint32_t localZ) noexcept {

        const int64_t cells = static_cast<int64_t>(GetChunkCells());
        const int64_t globalX64 =
            static_cast<int64_t>(chunkKey.x) * cells + localX;
        const int64_t globalZ64 =
            static_cast<int64_t>(chunkKey.z) * cells + localZ;
        if (globalX64 < (std::numeric_limits<int32_t>::min)() ||
            globalX64 > (std::numeric_limits<int32_t>::max)() ||
            globalZ64 < (std::numeric_limits<int32_t>::min)() ||
            globalZ64 > (std::numeric_limits<int32_t>::max)()) {
            coverageCacheFullRebuild_ = true;
            coverageDirtyValid_ = false;
            return;
        }
        const int32_t globalX = static_cast<int32_t>(globalX64);
        const int32_t globalZ = static_cast<int32_t>(globalZ64);
        if (!coverageDirtyValid_) {
            coverageDirtyMinX_ = globalX;
            coverageDirtyMaxX_ = globalX;
            coverageDirtyMinZ_ = globalZ;
            coverageDirtyMaxZ_ = globalZ;
            coverageDirtyValid_ = true;
            return;
        }
        coverageDirtyMinX_ = (std::min)(coverageDirtyMinX_, globalX);
        coverageDirtyMaxX_ = (std::max)(coverageDirtyMaxX_, globalX);
        coverageDirtyMinZ_ = (std::min)(coverageDirtyMinZ_, globalZ);
        coverageDirtyMaxZ_ = (std::max)(coverageDirtyMaxZ_, globalZ);
    }

    void TerrainSurfaceComponent::MarkCoverageChunkDirty(
        const TERRAIN::TerrainChunkKey& chunkKey) noexcept {

        const uint32_t cells = GetChunkCells();
        MarkCoverageVertexDirty(chunkKey, 0u, 0u);
        MarkCoverageVertexDirty(chunkKey, cells, cells);
    }

    bool TerrainSurfaceComponent::TryReadCoverageVertex(
        int32_t globalX,
        int32_t globalZ,
        float& outRawCoverage) const noexcept {

        outRawCoverage = 0.0f;
        const int64_t cells = static_cast<int64_t>(GetChunkCells());
        const auto floorDiv = [cells](int32_t value) noexcept {
            int64_t quotient = static_cast<int64_t>(value) / cells;
            const int64_t remainder =
                static_cast<int64_t>(value) % cells;
            if (remainder < 0) {
                --quotient;
            }
            return quotient;
        };
        const int64_t baseX = floorDiv(globalX);
        const int64_t baseZ = floorDiv(globalZ);
        bool foundAny = false;
        constexpr int64_t offsets[2] = { 0, -1 };
        for (const int64_t dz : offsets) {
            for (const int64_t dx : offsets) {
                const int64_t chunkX64 = baseX + dx;
                const int64_t chunkZ64 = baseZ + dz;
                if (chunkX64 < (std::numeric_limits<int32_t>::min)() ||
                    chunkX64 > (std::numeric_limits<int32_t>::max)() ||
                    chunkZ64 < (std::numeric_limits<int32_t>::min)() ||
                    chunkZ64 > (std::numeric_limits<int32_t>::max)()) {
                    continue;
                }
                const TERRAIN::TerrainChunkKey chunkKey{
                    static_cast<int32_t>(chunkX64),
                    static_cast<int32_t>(chunkZ64)
                };
                const auto found = surface_.chunks.find(chunkKey);
                if (found == surface_.chunks.end() ||
                    !TERRAIN::TerrainChunkValid(found->second)) {
                    continue;
                }
                const TERRAIN::TerrainChunk& chunk = found->second;
                const int64_t localX = static_cast<int64_t>(globalX) -
                    chunkX64 * cells;
                const int64_t localZ = static_cast<int64_t>(globalZ) -
                    chunkZ64 * cells;
                if (localX < 0 || localZ < 0 ||
                    localX > static_cast<int64_t>(chunk.cells) ||
                    localZ > static_cast<int64_t>(chunk.cells)) {
                    continue;
                }
                const uint32_t x = static_cast<uint32_t>(localX);
                const uint32_t z = static_cast<uint32_t>(localZ);
                const uint32_t stride = chunk.cells + 1u;
                const float raw = std::clamp(
                    chunk.coverage[
                        static_cast<std::size_t>(z) * stride + x],
                    0.0f,
                    1.0f);
                outRawCoverage = (std::max)(outRawCoverage, raw);
                foundAny = true;
            }
        }
        return foundAny;
    }

    void TerrainSurfaceComponent::RebuildCoverageCache() const {
        struct CoverageNode {
            int32_t x = 0;
            int32_t z = 0;
            float raw = 0.0f;
            float distance = 0.0f;
        };
        struct QueueEntry {
            float distance = 0.0f;
            std::size_t index = 0u;
        };
        struct QueueGreater {
            bool operator()(
                const QueueEntry& lhs,
                const QueueEntry& rhs) const noexcept {
                return lhs.distance > rhs.distance;
            }
        };

        const float cellSize = (std::max)(
            surface_.chunkCellSize, 0.01f);
        const float width = (std::max)(
            surface_.shoreline.edgeFalloff, 0.0f);
        const bool fullRebuild =
            coverageCacheFullRebuild_ ||
            !coverageCacheInitialized_ ||
            !coverageDirtyValid_;
        const int64_t radiusCells = width > cellSize * 0.5f
            ? static_cast<int64_t>(std::ceil(width / cellSize))
            : 0;
        const auto expand = [](int32_t value, int64_t amount) {
            return static_cast<int32_t>(std::clamp(
                static_cast<int64_t>(value) + amount,
                static_cast<int64_t>(
                    (std::numeric_limits<int32_t>::min)()),
                static_cast<int64_t>(
                    (std::numeric_limits<int32_t>::max)())));
        };

        int32_t outputMinX = (std::numeric_limits<int32_t>::min)();
        int32_t outputMaxX = (std::numeric_limits<int32_t>::max)();
        int32_t outputMinZ = (std::numeric_limits<int32_t>::min)();
        int32_t outputMaxZ = (std::numeric_limits<int32_t>::max)();
        int32_t evalMinX = outputMinX;
        int32_t evalMaxX = outputMaxX;
        int32_t evalMinZ = outputMinZ;
        int32_t evalMaxZ = outputMaxZ;
        if (!fullRebuild) {
            outputMinX = expand(coverageDirtyMinX_, -radiusCells);
            outputMaxX = expand(coverageDirtyMaxX_, radiusCells);
            outputMinZ = expand(coverageDirtyMinZ_, -radiusCells);
            outputMaxZ = expand(coverageDirtyMaxZ_, radiusCells);
            // 1 頂点 halo は、影響範囲外の不変 distance field を境界条件
            // として再利用する。seed 消失による距離増加も局所で解き直せる。
            evalMinX = expand(outputMinX, -1);
            evalMaxX = expand(outputMaxX, 1);
            evalMinZ = expand(outputMinZ, -1);
            evalMaxZ = expand(outputMaxZ, 1);
        }
        const auto insideOutput = [&](int32_t x, int32_t z) {
            return x >= outputMinX && x <= outputMaxX &&
                z >= outputMinZ && z <= outputMaxZ;
        };

        std::vector<CoverageNode> nodes{};
        std::unordered_map<uint64_t, std::size_t> nodeIndices{};
        const uint32_t cells = GetChunkCells();
        const auto appendChunk = [&](
            const TERRAIN::TerrainChunkKey& chunkKey,
            const TERRAIN::TerrainChunk& chunk) {
            if (!TERRAIN::TerrainChunkValid(chunk)) {
                return;
            }
            const uint32_t stride = chunk.cells + 1u;
            for (uint32_t z = 0u; z <= chunk.cells; ++z) {
                for (uint32_t x = 0u; x <= chunk.cells; ++x) {
                    const int64_t globalX64 =
                        static_cast<int64_t>(chunkKey.x) * cells + x;
                    const int64_t globalZ64 =
                        static_cast<int64_t>(chunkKey.z) * cells + z;
                    if (globalX64 < evalMinX || globalX64 > evalMaxX ||
                        globalZ64 < evalMinZ || globalZ64 > evalMaxZ) {
                        continue;
                    }
                    const int32_t globalX = static_cast<int32_t>(globalX64);
                    const int32_t globalZ = static_cast<int32_t>(globalZ64);
                    const uint64_t packed = PackTerrainVertexKey(
                        globalX, globalZ);
                    const float raw = std::clamp(
                        chunk.coverage[
                            static_cast<std::size_t>(z) * stride + x],
                        0.0f,
                        1.0f);
                    const auto [found, inserted] = nodeIndices.emplace(
                        packed, nodes.size());
                    if (inserted) {
                        nodes.push_back(CoverageNode{
                            globalX, globalZ, raw, width });
                    } else {
                        nodes[found->second].raw = (std::max)(
                            nodes[found->second].raw, raw);
                    }
                }
            }
        };

        if (fullRebuild) {
            coverageCache_.clear();
            coverageDistanceCache_.clear();
            for (const auto& [chunkKey, chunk] : surface_.chunks) {
                appendChunk(chunkKey, chunk);
            }
        } else {
            const auto floorDiv = [cells](int32_t value) {
                int64_t quotient = static_cast<int64_t>(value) / cells;
                const int64_t remainder =
                    static_cast<int64_t>(value) % cells;
                if (remainder < 0) {
                    --quotient;
                }
                return quotient;
            };
            // evalMin が共有 border 上の場合は左/下 chunk の duplicate
            // vertex も raw=max へ参加させる。
            const int64_t chunkMinX = floorDiv(evalMinX) - 1;
            const int64_t chunkMaxX = floorDiv(evalMaxX);
            const int64_t chunkMinZ = floorDiv(evalMinZ) - 1;
            const int64_t chunkMaxZ = floorDiv(evalMaxZ);
            for (int64_t chunkZ = chunkMinZ;
                chunkZ <= chunkMaxZ;
                ++chunkZ) {
                for (int64_t chunkX = chunkMinX;
                    chunkX <= chunkMaxX;
                    ++chunkX) {
                    if (chunkX <
                            (std::numeric_limits<int32_t>::min)() ||
                        chunkX >
                            (std::numeric_limits<int32_t>::max)() ||
                        chunkZ <
                            (std::numeric_limits<int32_t>::min)() ||
                        chunkZ >
                            (std::numeric_limits<int32_t>::max)()) {
                        continue;
                    }
                    const TERRAIN::TerrainChunkKey key{
                        static_cast<int32_t>(chunkX),
                        static_cast<int32_t>(chunkZ)
                    };
                    const auto found = surface_.chunks.find(key);
                    if (found != surface_.chunks.end()) {
                        appendChunk(found->first, found->second);
                    }
                }
            }
        }

        constexpr std::array<std::pair<int32_t, int32_t>, 8u> offsets{{
            { -1, -1 }, { 0, -1 }, { 1, -1 },
            { -1, 0 },              { 1, 0 },
            { -1, 1 },  { 0, 1 },  { 1, 1 },
        }};
        std::priority_queue<
            QueueEntry,
            std::vector<QueueEntry>,
            QueueGreater> queue{};
        constexpr float threshold =
            SAND::kSandSurfaceVisibleCoverageThreshold;
        if (width > cellSize * 0.5f) {
            for (std::size_t index = 0u; index < nodes.size(); ++index) {
                CoverageNode& node = nodes[index];
                if (!fullRebuild && !insideOutput(node.x, node.z)) {
                    const auto cached = coverageDistanceCache_.find(
                        PackTerrainVertexKey(node.x, node.z));
                    if (cached != coverageDistanceCache_.end()) {
                        node.distance = std::clamp(
                            cached->second, 0.0f, width);
                    }
                } else if (node.raw < threshold) {
                    node.distance = 0.0f;
                } else {
                    bool touchesOutside = false;
                    for (const auto [dx, dz] : offsets) {
                        float neighborRaw = 0.0f;
                        if (!TryReadCoverageVertex(
                                node.x + dx,
                                node.z + dz,
                                neighborRaw)) {
                            touchesOutside = true;
                            break;
                        }
                    }
                    if (touchesOutside) {
                        node.distance = (std::min)(cellSize, width);
                    }
                }
                if (node.distance + 1.0e-6f < width) {
                    queue.push({ node.distance, index });
                }
            }

            while (!queue.empty()) {
                const QueueEntry entry = queue.top();
                queue.pop();
                const CoverageNode& current = nodes[entry.index];
                if (entry.distance > current.distance + 1.0e-6f) {
                    continue;
                }
                for (const auto [dx, dz] : offsets) {
                    const auto found = nodeIndices.find(
                        PackTerrainVertexKey(
                            current.x + dx,
                            current.z + dz));
                    if (found == nodeIndices.end()) {
                        continue;
                    }
                    CoverageNode& neighbor = nodes[found->second];
                    const float step = (dx != 0 && dz != 0)
                        ? cellSize * 1.41421356237f
                        : cellSize;
                    const float candidate = entry.distance + step;
                    if (candidate + 1.0e-6f >= width ||
                        candidate + 1.0e-6f >= neighbor.distance) {
                        continue;
                    }
                    neighbor.distance = candidate;
                    queue.push({ candidate, found->second });
                }
            }
        }

        for (const CoverageNode& node : nodes) {
            if (!fullRebuild && !insideOutput(node.x, node.z)) {
                continue;
            }
            const uint64_t packed = PackTerrainVertexKey(node.x, node.z);
            const float distance = width > cellSize * 0.5f
                ? std::clamp(node.distance, 0.0f, width)
                : width;
            const float t = width > 1.0e-6f
                ? std::clamp(distance / width, 0.0f, 1.0f)
                : 1.0f;
            const float feather = t * t * (3.0f - 2.0f * t);
            coverageDistanceCache_[packed] = distance;
            coverageCache_[packed] = (std::min)(node.raw, feather);
        }

        coverageCacheRevision_ = coverageRevision_;
        coverageCacheInitialized_ = true;
        coverageCacheFullRebuild_ = false;
        coverageDirtyValid_ = false;
    }

    bool TerrainSurfaceComponent::SampleLocalRaw(
        float localX,
        float localZ,
        float& outHeight,
        float& outCoverage) const noexcept {
        const float chunkSize = GetChunkWorldSize();
        const TERRAIN::TerrainChunkKey base =
            TERRAIN::TerrainChunkKeyFromWorld(
                localX, localZ, chunkSize);
        constexpr int32_t offsets[2] = { 0, -1 };
        for (const int32_t dz : offsets) {
            for (const int32_t dx : offsets) {
                const TERRAIN::TerrainChunkKey key{
                    base.x + dx, base.z + dz };
                const auto found = surface_.chunks.find(key);
                if (found == surface_.chunks.end()) {
                    continue;
                }
                const TERRAIN::TerrainChunk& chunk = found->second;
                const float cx = localX -
                    static_cast<float>(key.x) * chunkSize;
                const float cz = localZ -
                    static_cast<float>(key.z) * chunkSize;
                constexpr float epsilon = 1.0e-4f;
                if (cx < -epsilon || cx > chunkSize + epsilon ||
                    cz < -epsilon || cz > chunkSize + epsilon) {
                    continue;
                }
                const uint32_t cells = chunk.cells;
                const uint32_t stride = cells + 1u;
                const float chunkCell = chunkSize /
                    static_cast<float>(cells);
                const float fx = std::clamp(
                    cx / chunkCell, 0.0f, static_cast<float>(cells));
                const float fz = std::clamp(
                    cz / chunkCell, 0.0f, static_cast<float>(cells));
                const uint32_t x0 = static_cast<uint32_t>(fx);
                const uint32_t z0 = static_cast<uint32_t>(fz);
                const uint32_t x1 = (std::min)(x0 + 1u, cells);
                const uint32_t z1 = (std::min)(z0 + 1u, cells);
                const float tx = fx - static_cast<float>(x0);
                const float tz = fz - static_cast<float>(z0);
                const auto index = [stride](uint32_t x, uint32_t z) {
                    return static_cast<std::size_t>(z) * stride + x;
                };
                outHeight = std::lerp(
                    std::lerp(
                        chunk.heights[index(x0, z0)],
                        chunk.heights[index(x1, z0)], tx),
                    std::lerp(
                        chunk.heights[index(x0, z1)],
                        chunk.heights[index(x1, z1)], tx), tz);
                outCoverage = std::lerp(
                    std::lerp(
                        chunk.coverage[index(x0, z0)],
                        chunk.coverage[index(x1, z0)], tx),
                    std::lerp(
                        chunk.coverage[index(x0, z1)],
                        chunk.coverage[index(x1, z1)], tx), tz);
                return true;
            }
        }
        outHeight = 0.0f;
        outCoverage = 0.0f;
        return false;
    }

    float TerrainSurfaceComponent::SampleLocalCoverage(
        const MATH::Vec3& localPos) const noexcept {
        if (coverageCacheRevision_ != coverageRevision_) {
            RebuildCoverageCache();
        }
        const float chunkSize = GetChunkWorldSize();
        const TERRAIN::TerrainChunkKey base =
            TERRAIN::TerrainChunkKeyFromWorld(
                localPos.x, localPos.z, chunkSize);
        constexpr int32_t offsets[2] = { 0, -1 };
        for (const int32_t dz : offsets) {
            for (const int32_t dx : offsets) {
                const TERRAIN::TerrainChunkKey key{
                    base.x + dx, base.z + dz };
                const auto found = surface_.chunks.find(key);
                if (found == surface_.chunks.end()) {
                    continue;
                }
                const TERRAIN::TerrainChunk& chunk = found->second;
                const float cx = localPos.x -
                    static_cast<float>(key.x) * chunkSize;
                const float cz = localPos.z -
                    static_cast<float>(key.z) * chunkSize;
                constexpr float epsilon = 1.0e-4f;
                if (cx < -epsilon || cx > chunkSize + epsilon ||
                    cz < -epsilon || cz > chunkSize + epsilon) {
                    continue;
                }
                const uint32_t cells = chunk.cells;
                const float cellSize = chunkSize /
                    static_cast<float>(cells);
                const float fx = std::clamp(
                    cx / cellSize, 0.0f, static_cast<float>(cells));
                const float fz = std::clamp(
                    cz / cellSize, 0.0f, static_cast<float>(cells));
                const uint32_t x0 = static_cast<uint32_t>(fx);
                const uint32_t z0 = static_cast<uint32_t>(fz);
                const uint32_t x1 = (std::min)(x0 + 1u, cells);
                const uint32_t z1 = (std::min)(z0 + 1u, cells);
                const float tx = fx - static_cast<float>(x0);
                const float tz = fz - static_cast<float>(z0);
                const auto C = [&](uint32_t x, uint32_t z) {
                    const int32_t globalX = key.x *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(x);
                    const int32_t globalZ = key.z *
                        static_cast<int32_t>(cells) +
                        static_cast<int32_t>(z);
                    const uint64_t packed = PackTerrainVertexKey(
                        globalX, globalZ);
                    const auto cached = coverageCache_.find(packed);
                    if (cached != coverageCache_.end()) {
                        return cached->second;
                    }
                    // Full build は全 node、regional build は dirty+falloff
                    // 内の全 node を必ず materialize する。未登録 node は
                    // revision 外の raw fallback とし、sample 自体は O(1)。
                    return std::clamp(
                        chunk.coverage[
                            static_cast<std::size_t>(z) * (cells + 1u) + x],
                        0.0f,
                        1.0f);
                };
                return std::clamp(
                    std::lerp(
                        std::lerp(C(x0, z0), C(x1, z0), tx),
                        std::lerp(C(x0, z1), C(x1, z1), tx), tz),
                    0.0f,
                    1.0f);
            }
        }
        return 0.0f;
    }

    bool TerrainSurfaceComponent::SampleLocalSurfaceHeight(
        const MATH::Vec3& localPos,
        float& outHeight) const noexcept {
        float rawCoverage = 0.0f;
        if (!SampleLocalRaw(
                localPos.x, localPos.z,
                outHeight, rawCoverage)) {
            outHeight = 0.0f;
            return false;
        }
        const float coverage = SampleLocalCoverage(localPos);
        if (coverage < SAND::kSandSurfaceVisibleCoverageThreshold) {
            outHeight = 0.0f;
            return false;
        }
        outHeight = SAND::EvaluateSandSurfaceEffectiveHeight(
            outHeight,
            coverage,
            surface_.shoreline.edgeDepth,
            surface_.deform.rimStrength);
        return true;
    }

    bool TerrainSurfaceComponent::SampleLocalHeight(
        const MATH::Vec3& localPos,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {
        outHeight = 0.0f;
        outNormal = { 0.0f, 1.0f, 0.0f };
        const float chunkSize = GetChunkWorldSize();
        const float cellSize = chunkSize /
            static_cast<float>((std::max)(GetChunkCells(), 1u));
        if (!SampleLocalSurfaceHeight(localPos, outHeight)) {
            return false;
        }
        float hL = outHeight;
        float hR = outHeight;
        float hD = outHeight;
        float hU = outHeight;
        const auto sampleEdgeHeight = [&](float sx, float sz, float fallback) {
            float height = fallback;
            if (!SampleLocalSurfaceHeight(
                    { sx, localPos.y, sz }, height)) {
                return fallback;
            }
            return height;
        };
        hL = sampleEdgeHeight(
            localPos.x - cellSize, localPos.z, hL);
        hR = sampleEdgeHeight(
            localPos.x + cellSize, localPos.z, hR);
        hD = sampleEdgeHeight(
            localPos.x, localPos.z - cellSize, hD);
        hU = sampleEdgeHeight(
            localPos.x, localPos.z + cellSize, hU);
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

    bool TerrainSurfaceComponent::SampleWorldHeight(
        const MATH::Vec3& worldPos,
        float& outHeight,
        MATH::Vec3& outNormal) const noexcept {
        const MATH::Vec3 localPos = WorldToLocal(worldPos);
        float localHeight = 0.0f;
        MATH::Vec3 localNormal{};
        if (!SampleLocalHeight(localPos, localHeight, localNormal)) {
            outHeight = 0.0f;
            outNormal = { 0.0f, 1.0f, 0.0f };
            return false;
        }
        const MATH::Vec3 surfaceWorld = LocalToWorld({
            localPos.x, localHeight, localPos.z });
        outHeight = surfaceWorld.y;
        const GameObject* owner = GetOwner();
        if (owner == nullptr) {
            outNormal = localNormal;
            return true;
        }
        const MATH::Mat4 normalMatrix = MATH::BuildNormalMatrixFromWorld(
            owner->GetTransform().GetWorldMatrix());
        const MATH::Vec4 transformed = normalMatrix.TransformPoint({
            localNormal.x, localNormal.y, localNormal.z, 0.0f });
        const MATH::Vec3 worldNormal{
            transformed.x, transformed.y, transformed.z };
        outNormal = MATH::Dot(worldNormal, worldNormal) > 1.0e-12f
            ? MATH::Normalize(worldNormal)
            : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
        return true;
    }

    bool TerrainSurfaceComponent::RaycastWorld(
        const MATH::Vec3& rayOrigin,
        const MATH::Vec3& rayDirection,
        float maxDistance,
        MATH::Vec3& outPosition,
        MATH::Vec3& outNormal,
        bool allowBasePlaneFallback) const noexcept {
        const float directionLengthSq =
            MATH::Dot(rayDirection, rayDirection);
        if (directionLengthSq <= 1.0e-12f || maxDistance <= 0.0f) {
            return false;
        }
        const MATH::Vec3 direction = MATH::Normalize(rayDirection);
        const MATH::Vec3 localOrigin = WorldToLocal(rayOrigin);
        const MATH::Vec3 localNext = WorldToLocal(rayOrigin + direction);
        const MATH::Vec3 localDirection = localNext - localOrigin;

        const auto evaluate = [&](float t, float& above,
            MATH::Vec3& localPoint, bool& hasSurface) {
            localPoint = localOrigin + localDirection * t;
            float height = 0.0f;
            hasSurface = SampleLocalSurfaceHeight(localPoint, height);
            if (!hasSurface && !allowBasePlaneFallback) {
                return false;
            }
            above = localPoint.y - height;
            return true;
        };

        float marchDistance = maxDistance;
        if (localDirection.y < -1.0e-6f) {
            float minimumHeight = 0.0f;
            for (const auto& [key, chunk] : surface_.chunks) {
                (void)key;
                if (chunk.heights.empty()) {
                    continue;
                }
                minimumHeight = (std::min)(
                    minimumHeight,
                    *std::min_element(
                        chunk.heights.begin(), chunk.heights.end()));
            }
            minimumHeight -= (std::max)(
                surface_.shoreline.edgeDepth, 0.0f);
            const float distanceToLowest =
                (minimumHeight - localOrigin.y) / localDirection.y;
            if (distanceToLowest >= 0.0f) {
                marchDistance = (std::min)(
                    maxDistance,
                    distanceToLowest +
                        GetChunkWorldSize() * 0.02f);
            }
        }
        const float cellSize = (std::max)(
            surface_.chunkCellSize, 0.01f);
        const float horizontalDistance = std::sqrt(
            localDirection.x * localDirection.x +
            localDirection.z * localDirection.z) * marchDistance;
        const uint32_t marchSteps = std::clamp(
            static_cast<uint32_t>(std::ceil(
                horizontalDistance / (cellSize * 0.5f))),
            1u,
            192u);
        float previousT = 0.0f;
        float previousAbove = 0.0f;
        MATH::Vec3 previousPoint{};
        bool previousSurface = false;
        bool hasPrevious = evaluate(
            previousT, previousAbove, previousPoint,
            previousSurface);
        for (uint32_t step = 1u; step <= marchSteps; ++step) {
            const float t = marchDistance *
                static_cast<float>(step) /
                static_cast<float>(marchSteps);
            float above = 0.0f;
            MATH::Vec3 point{};
            bool hasSurface = false;
            const bool valid = evaluate(
                t, above, point, hasSurface);
            if (hasPrevious && valid &&
                previousAbove >= 0.0f && above <= 0.0f) {
                float lo = previousT;
                float hi = t;
                MATH::Vec3 hitPoint = point;
                bool hitSurface = hasSurface;
                for (uint32_t iteration = 0u;
                    iteration < 16u;
                    ++iteration) {
                    const float mid = (lo + hi) * 0.5f;
                    float midAbove = 0.0f;
                    MATH::Vec3 midPoint{};
                    bool midSurface = false;
                    (void)evaluate(
                        mid, midAbove, midPoint,
                        midSurface);
                    if (midAbove > 0.0f) {
                        lo = mid;
                    } else {
                        hi = mid;
                        hitPoint = midPoint;
                        hitSurface = midSurface;
                    }
                }
                float height = 0.0f;
                MATH::Vec3 hitNormal{ 0.0f, 1.0f, 0.0f };
                if (!SampleLocalHeight(
                        hitPoint, height, hitNormal)) {
                    height = 0.0f;
                    hitNormal = { 0.0f, 1.0f, 0.0f };
                    hitSurface = false;
                }
                hitPoint.y = height;
                outPosition = LocalToWorld(hitPoint);
                const GameObject* owner = GetOwner();
                if (owner == nullptr) {
                    outNormal = hitNormal;
                } else {
                    const MATH::Mat4 normalMatrix =
                        MATH::BuildNormalMatrixFromWorld(
                            owner->GetTransform().GetWorldMatrix());
                    const MATH::Vec4 transformed =
                        normalMatrix.TransformPoint({
                            hitNormal.x, hitNormal.y,
                            hitNormal.z, 0.0f });
                    const MATH::Vec3 worldNormal{
                        transformed.x, transformed.y, transformed.z };
                    outNormal = MATH::Dot(worldNormal, worldNormal) > 1.0e-12f
                        ? MATH::Normalize(worldNormal)
                        : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
                }
                (void)hitSurface;
                return true;
            }
            previousT = t;
            previousAbove = above;
            previousPoint = point;
            previousSurface = hasSurface;
            hasPrevious = valid;
        }
        return false;
    }

    void TerrainSurfaceComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void TerrainSurfaceComponent::SerializeSettings(
        nlohmann::json& out) const {
        const TERRAIN::TerrainSurface& s = surface_;
        out = {
            { "_schemaVersion", 6u },
            { "presetId", s.presetId },
            { "heightScale", s.heightScale },
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
                { "responseResolutionScale",
                    s.deform.responseResolutionScale },
                { "restDepthScale", s.deform.restDepthScale },
                { "minimumDepth", s.deform.minimumDepth },
                { "settleStartRatio", s.deform.settleStartRatio },
                { "settleSeconds", s.deform.settleSeconds },
                { "impactDepthScale", s.deform.impactDepthScale },
                { "rimWidthScale", s.deform.rimWidthScale },
                { "refillPerSecond", s.deform.refillPerSecond },
                { "diffusionPerSecond", s.deform.diffusionPerSecond },
                { "plasticity", s.deform.plasticity },
                { "plasticRefillPerSecond",
                    s.deform.plasticRefillPerSecond },
                { "recoveryDelaySeconds",
                    s.deform.recoveryDelaySeconds },
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
        };
    }

    void TerrainSurfaceComponent::Serialize(nlohmann::json& out) const {
        const TERRAIN::TerrainSurface& s = surface_;
        SerializeSettings(out);
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
        out["chunked"] = true;
        out["chunkCells"] = s.chunkCells;
        out["chunkCellSize"] = s.chunkCellSize;
        out["chunks"] = std::move(chunks);
    }

    void TerrainSurfaceComponent::Deserialize(const nlohmann::json& in) {
        if (!in.is_object()) {
            return;
        }
        const uint32_t schemaVersion = in.value("_schemaVersion", 0u);
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
            surface.deform.responseResolutionScale =
                static_cast<uint32_t>(deform->value(
                    "responseResolutionScale",
                    surface.deform.responseResolutionScale));
            surface.deform.restDepthScale = JSONREAD::NumberMemberOr(
                *deform, "restDepthScale", surface.deform.restDepthScale);
            surface.deform.minimumDepth = JSONREAD::NumberMemberOr(
                *deform, "minimumDepth", surface.deform.minimumDepth);
            surface.deform.settleStartRatio = JSONREAD::NumberMemberOr(
                *deform,
                "settleStartRatio",
                surface.deform.settleStartRatio);
            surface.deform.settleSeconds = JSONREAD::NumberMemberOr(
                *deform, "settleSeconds", surface.deform.settleSeconds);
            surface.deform.impactDepthScale = JSONREAD::NumberMemberOr(
                *deform, "impactDepthScale", surface.deform.impactDepthScale);
            surface.deform.rimWidthScale = JSONREAD::NumberMemberOr(
                *deform, "rimWidthScale", surface.deform.rimWidthScale);
            surface.deform.refillPerSecond = JSONREAD::NumberMemberOr(
                *deform, "refillPerSecond", surface.deform.refillPerSecond);
            surface.deform.diffusionPerSecond = JSONREAD::NumberMemberOr(
                *deform, "diffusionPerSecond", surface.deform.diffusionPerSecond);
            surface.deform.plasticity = JSONREAD::NumberMemberOr(
                *deform, "plasticity", surface.deform.plasticity);
            surface.deform.plasticRefillPerSecond =
                JSONREAD::NumberMemberOr(
                    *deform,
                    "plasticRefillPerSecond",
                    surface.deform.plasticRefillPerSecond);
            surface.deform.recoveryDelaySeconds =
                JSONREAD::NumberMemberOr(
                    *deform,
                    "recoveryDelaySeconds",
                    surface.deform.recoveryDelaySeconds);
            // schema 3 の 1.2 は旧既定値で、単一点の痕跡を約 0.57 秒で
            // 平坦化していた。明示的な新設定が無い既存 scene だけを
            // 新しい elastic-flow 既定値へ移行する。
            if (schemaVersion < 4u &&
                std::fabs(surface.deform.diffusionPerSecond - 1.2f) <
                    1.0e-5f) {
                surface.deform.diffusionPerSecond = 0.12f;
            }
            if (schemaVersion < 6u) {
                const auto migrateDefault = [](
                    float& value,
                    float legacyValue,
                    float currentValue) noexcept {
                    if (std::fabs(value - legacyValue) < 1.0e-5f) {
                        value = currentValue;
                    }
                };
                // schema 5 までの既定 profile は接触直後の見える痕跡が
                // 小さく、2 秒後から elastic が急にぼけていた。ユーザーが
                // 明示変更した値は維持し、旧既定値だけを animation profileへ移す。
                migrateDefault(surface.deform.stampRadius, 0.42f, 0.58f);
                migrateDefault(surface.deform.rimStrength, 0.28f, 0.32f);
                migrateDefault(
                    surface.deform.restDepthScale, 0.30f, 0.36f);
                migrateDefault(surface.deform.minimumDepth, 0.04f, 0.055f);
                migrateDefault(
                    surface.deform.settleStartRatio, 0.65f, 0.72f);
                migrateDefault(
                    surface.deform.settleSeconds, 0.12f, 0.18f);
                migrateDefault(
                    surface.deform.impactDepthScale, 0.025f, 0.03f);
                migrateDefault(
                    surface.deform.rimWidthScale, 0.32f, 0.42f);
                migrateDefault(
                    surface.deform.refillPerSecond, 0.018f, 0.008f);
                migrateDefault(
                    surface.deform.diffusionPerSecond, 0.12f, 0.04f);
                migrateDefault(
                    surface.deform.plasticity, 0.85f, 0.93f);
                migrateDefault(
                    surface.deform.plasticRefillPerSecond,
                    0.002f,
                    0.0003f);
                migrateDefault(
                    surface.deform.recoveryDelaySeconds, 2.0f, 8.0f);
            }
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
