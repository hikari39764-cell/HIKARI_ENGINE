#include "HIKARI_TerrainRenderProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "Core/Math/HIKARI_GridBilinearSample.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceMaterialBinder.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"

namespace HIKARI::TERRAIN::RENDER {
constexpr float kTerrainAtlasContentScale = 1008.0f / 2048.0f;
constexpr float kTerrainAtlasGutterBias = 8.0f / 2048.0f;

MATH::Vec4 ResolveTerrainAtlasScaleBias(uint32_t tile) noexcept {
    const uint32_t safeTile = (std::min)(tile, 3u);
    return {
        kTerrainAtlasContentScale,
        kTerrainAtlasContentScale,
        static_cast<float>(safeTile & 1u) * 0.5f + kTerrainAtlasGutterBias,
        static_cast<float>(safeTile >> 1u) * 0.5f + kTerrainAtlasGutterBias,
    };
}

RENDER3D::TERRAIN::TerrainSurfaceLayerRenderStyle BuildTerrainLayerRenderStyle(
    const TERRAIN::SurfacePreset& preset, const TERRAIN::TerrainStylizationSettings& authoredSettings) noexcept {
    const TERRAIN::SurfaceLayerMaterialParams& authored = preset.layerMaterial;
    const TERRAIN::TerrainStylizationSettings stylization =
        TERRAIN::SanitizeTerrainStylizationSettings(authoredSettings);
    RENDER3D::TERRAIN::TerrainSurfaceLayerRenderStyle style{};
    style.lightTint = preset.tint.light;
    style.textureStrength = std::clamp(authored.textureStrength * stylization.textureStrength, 0.0f, 2.0f);
    style.darkTint = preset.tint.dark;
    style.normalStrength = std::clamp(authored.normalStrength * stylization.normalStrength, 0.0f, 2.0f);
    style.atlasScaleBias = ResolveTerrainAtlasScaleBias(authored.atlasTile);
    style.tilesPerMeter = 1.0f / (std::max)(authored.metersPerTile, 0.05f) * stylization.textureScale;
    style.roughness = std::clamp(authored.roughness, 0.0f, 1.0f);
    const float saturation = authored.kind == TERRAIN::SurfaceLayerMaterialKind::Rock
                                 ? stylization.rockSaturation
                                 : authored.saturation * stylization.saturation;
    style.saturation = std::clamp(saturation, 0.0f, 2.0f);
    style.materialKind = static_cast<uint32_t>(authored.kind);
    style.shadowStrength = std::clamp(authored.shadowStrength * stylization.shadowStrength, 0.0f, 1.0f);
    style.granularStrength = authored.kind == TERRAIN::SurfaceLayerMaterialKind::Sand ||
                                     authored.kind == TERRAIN::SurfaceLayerMaterialKind::Snow
                                 ? 1.0f
                                 : 0.0f;
    style.facetSizeMeters = stylization.facetSizeMeters;
    style.facetStrength = stylization.facetStrength;
    return style;
}

void AppendChangedChunk(std::vector<TERRAIN::TerrainChunkKey>& chunks, TERRAIN::TerrainChunkKey key) {
    if (std::find(chunks.begin(), chunks.end(), key) == chunks.end()) {
        chunks.push_back(key);
    }
}

uint32_t ResolveTerrainProjectionGrid(uint32_t totalCellsX, uint32_t totalCellsZ, uint32_t projectionScale,
                                      uint32_t layerCount, bool includeDeformation) {
    constexpr uint32_t kMaxGrid = 512u;
    const std::size_t fieldsPerVertex = 2u + // total height + coverage
                                        (includeDeformation ? 1u : 0u) +
                                        (layerCount > 1u ? static_cast<std::size_t>(layerCount) : 0u);
    const std::size_t fixedPoolFloats =
        RENDER3D::RUNTIME::kSceneSurfaceNeutralFloatCount +
        (layerCount > 1u ? static_cast<std::size_t>(layerCount) * RENDER3D::TERRAIN::kTerrainSurfaceLayerStyleFloatCount
                         : 0u);
    const std::size_t availableVertexFloats = RENDER3D::TERRAIN::kTerrainSurfaceMaxHeightCells > fixedPoolFloats
                                                  ? RENDER3D::TERRAIN::kTerrainSurfaceMaxHeightCells - fixedPoolFloats
                                                  : 0u;
    const std::size_t vertexBudget = fieldsPerVertex > 0u ? availableVertexFloats / fieldsPerVertex : 0u;
    const double budgetStride = std::sqrt(static_cast<double>(vertexBudget));
    const uint32_t budgetGrid = budgetStride >= 2.0 ? static_cast<uint32_t>(budgetStride) - 1u : 1u;
    const uint64_t desiredGrid =
        static_cast<uint64_t>((std::max)(totalCellsX, totalCellsZ)) * (std::max)(projectionScale, 1u);
    return (std::max)(
        1u, (std::min)(static_cast<uint32_t>((std::min)(desiredGrid, static_cast<uint64_t>(kMaxGrid))), budgetGrid));
}

bool SampleTerrainGrid(const std::vector<float>& values, std::size_t baseOffset, uint32_t sourceCells,
                       float sourceCellSize, float localX, float localZ, float& outValue) {
    if (sourceCells == 0u || !std::isfinite(sourceCellSize) || sourceCellSize <= 1.0e-4f) {
        return false;
    }
    const uint32_t sourceStride = sourceCells + 1u;
    const std::size_t sourceCount = static_cast<std::size_t>(sourceStride) * sourceStride;
    if (baseOffset > values.size() || sourceCount > values.size() - baseOffset) {
        return false;
    }
    const auto sample =
        MATH::GridBilinearSample::Clamped(localX / sourceCellSize, localZ / sourceCellSize, sourceCells);
    outValue = sample.Sample(
        [&](uint32_t x, uint32_t z) { return values[baseOffset + static_cast<std::size_t>(z) * sourceStride + x]; });
    return std::isfinite(outValue);
}

void BakeTerrainChunksForRender(const TERRAIN::TerrainSurface& surface, const TerrainSurfaceComponent& terrain,
                                RuntimeObjectHandle objectHandle,
                                const TERRAIN::TerrainSurfaceQueryService* terrainSurfaceQuery,
                                const std::filesystem::path& projectRoot, float chunkSize, uint32_t cells,
                                TerrainProjectionState& state, SceneRenderCacheSync::DebugStats& stats,
                                TerrainProjectionOutput& output, RENDER3D::RUNTIME::SceneRenderObjectDesc& desc) {
    desc.terrainSurfaceHeights.reset();
    desc.terrainSurfaceDeformations.reset();
    desc.terrainSurfaceLayerWeights.reset();
    desc.terrainSurfaceCoverage.reset();
    const TERRAIN::TerrainChunkMap& chunks = surface.chunks;
    if (chunks.empty()) {
        desc.terrainSurface = false;
        state = {};
        return;
    }
    // Runtime world rebuilds may recreate a component with the same
    // local revision. Projection state is owned by the render bridge,
    // so revision alone is not a valid cache identity across object
    // handle generations.
    if (state.sourceObject != objectHandle) {
        state = {};
        state.sourceObject = objectHandle;
    }

    int32_t minKeyX = (std::numeric_limits<int32_t>::max)();
    int32_t minKeyZ = (std::numeric_limits<int32_t>::max)();
    int32_t maxKeyX = (std::numeric_limits<int32_t>::min)();
    int32_t maxKeyZ = (std::numeric_limits<int32_t>::min)();
    for (const auto& [key, chunk] : chunks) {
        (void)chunk;
        minKeyX = (std::min)(minKeyX, key.x);
        minKeyZ = (std::min)(minKeyZ, key.z);
        maxKeyX = (std::max)(maxKeyX, key.x);
        maxKeyZ = (std::max)(maxKeyZ, key.z);
    }

    const float extentX = static_cast<float>(maxKeyX - minKeyX + 1) * chunkSize;
    const float extentZ = static_cast<float>(maxKeyZ - minKeyZ + 1) * chunkSize;
    const uint32_t totalCellsX = (std::max)(1u, static_cast<uint32_t>(maxKeyX - minKeyX + 1) * cells);
    const uint32_t totalCellsZ = (std::max)(1u, static_cast<uint32_t>(maxKeyZ - minKeyZ + 1) * cells);

    state.changedChunks.clear();
    const uint64_t authoredRevision = terrain.GetRevision();
    if (!state.layoutValid || state.authoredRevision != authoredRevision) {
        for (const auto& [key, chunk] : chunks) {
            const uint64_t chunkRevision = terrain.GetChunkRevision(key);
            const auto old = state.authoredChunks.find(key);
            if (old == state.authoredChunks.end() || old->second.revision != chunkRevision) {
                AppendChangedChunk(state.changedChunks, key);
                TerrainProjectionState::AuthoredChunkState next{};
                next.revision = chunkRevision;
                float chunkMin = (std::numeric_limits<float>::max)();
                float chunkMax = (std::numeric_limits<float>::lowest)();
                for (const float height : chunk.heights) {
                    if (!std::isfinite(height)) {
                        continue;
                    }
                    chunkMin = (std::min)(chunkMin, height);
                    chunkMax = (std::max)(chunkMax, height);
                }
                next.minHeight = std::isfinite(chunkMin) ? chunkMin : 0.0f;
                next.maxHeight = std::isfinite(chunkMax) ? chunkMax : 0.0f;
                state.authoredChunks[key] = next;
            }
        }
        for (auto it = state.authoredChunks.begin(); it != state.authoredChunks.end();) {
            if (chunks.find(it->first) != chunks.end()) {
                ++it;
                continue;
            }
            AppendChangedChunk(state.changedChunks, it->first);
            it = state.authoredChunks.erase(it);
        }
        float authoredMin = (std::numeric_limits<float>::max)();
        float authoredMax = (std::numeric_limits<float>::lowest)();
        for (const auto& [key, chunkState] : state.authoredChunks) {
            (void)key;
            authoredMin = (std::min)(authoredMin, chunkState.minHeight);
            authoredMax = (std::max)(authoredMax, chunkState.maxHeight);
        }
        state.authoredMinHeight = std::isfinite(authoredMin) ? authoredMin : 0.0f;
        state.authoredMaxHeight = std::isfinite(authoredMax) ? authoredMax : 0.0f;
        state.authoredRevision = authoredRevision;
    }

    // Runtime snapshot は stateRevision だけを比較し、変更 chunk の
    // immutable storage だけ差し替える。fixed tick ごとの大配列複製は
    // Sand owner 側に閉じ、render bridge は参照を保持する。
    bool responseGridActive = false;
    uint32_t responseProjectionScale = 1u;
    for (const auto& [key, chunk] : chunks) {
        (void)chunk;
        TERRAIN::TerrainChunkRuntimeSnapshotPtr runtime{};
        const bool runtimeValid =
            terrainSurfaceQuery != nullptr && terrainSurfaceQuery->TryGetChunkState(objectHandle, key, runtime) &&
            runtime && runtime->cells > 0u && std::isfinite(runtime->cellSize) && runtime->cellSize > 1.0e-4f;
        const auto oldRevision = state.runtimeRevisions.find(key);
        if (!runtimeValid) {
            if (oldRevision != state.runtimeRevisions.end()) {
                AppendChangedChunk(state.changedChunks, key);
                state.runtimeRevisions.erase(oldRevision);
                state.runtimeByChunk.erase(key);
            }
            continue;
        }

        if (oldRevision == state.runtimeRevisions.end() || oldRevision->second != runtime->stateRevision) {
            AppendChangedChunk(state.changedChunks, key);
            state.runtimeRevisions[key] = runtime->stateRevision;
            state.runtimeByChunk[key] = runtime;
        }
        responseGridActive = responseGridActive || runtime->responseGridActive;
        const uint32_t runtimeAuthoredCells = (std::max)(runtime->authoredCells, 1u);
        const uint64_t responseScale =
            (static_cast<uint64_t>(runtime->cells) + runtimeAuthoredCells - 1u) / runtimeAuthoredCells;
        responseProjectionScale =
            (std::max)(responseProjectionScale, static_cast<uint32_t>((std::max)(responseScale, uint64_t{1u})));
    }
    for (auto it = state.runtimeRevisions.begin(); it != state.runtimeRevisions.end();) {
        if (chunks.find(it->first) != chunks.end()) {
            ++it;
            continue;
        }
        AppendChangedChunk(state.changedChunks, it->first);
        state.runtimeByChunk.erase(it->first);
        it = state.runtimeRevisions.erase(it);
    }

    uint32_t layerCount = 1u;
    for (const auto& [key, chunk] : chunks) {
        (void)key;
        layerCount = (std::max)(layerCount, chunk.layerCount);
    }

    const uint32_t authoredGrid = ResolveTerrainProjectionGrid(totalCellsX, totalCellsZ, 1u, layerCount, false);
    const uint32_t configuredResponseScale = (std::clamp)(surface.deform.responseResolutionScale, 1u, 4u);
    const uint32_t desiredResponseScale = responseGridActive ? responseProjectionScale : configuredResponseScale;
    const uint32_t responseGrid =
        ResolveTerrainProjectionGrid(totalCellsX, totalCellsZ, desiredResponseScale, layerCount, true);

    ProceduralMeshSettings responseSettings{};
    responseSettings.kind = ProceduralMeshKind::GridPlane;
    responseSettings.width = extentX;
    responseSettings.depth = extentZ;
    responseSettings.segmentsX = responseGrid;
    responseSettings.segmentsY = responseGrid;
    // uv0 の材質 tile 密度は authored lattice に固定する。
    // response grid が高密度へ切り替わっても、地面全体の
    // テクスチャ尺度は変えない。uv1 は正規化 field 座標のまま。
    responseSettings.uvTilesX = static_cast<float>((std::max)(totalCellsX, 1u));
    responseSettings.uvTilesY = static_cast<float>((std::max)(totalCellsZ, 1u));
    bool responseModelReady = responseGrid <= authoredGrid;
    if (responseGrid > authoredGrid && !projectRoot.empty()) {
        if (!state.warmSettingsValid || state.warmSettings != responseSettings ||
            state.warmProjectRoot != projectRoot) {
            state.warmSettings = responseSettings;
            state.warmProjectRoot = projectRoot;
            state.warmSettingsValid = true;
            ++stats.terrainResponseWarmRequestCount;
        }
        PROCEDURAL::RequestClusteredGeometryWarmUp(responseSettings, projectRoot);
        responseModelReady = PROCEDURAL::TryGetWarmedClusteredGeometry(
            responseSettings, projectRoot, output.warmedModel, output.warmedClusteredGeometryPath);
    }
    state.warmPending = responseGrid > authoredGrid && !responseModelReady;
    if (state.warmPending) {
        ++stats.terrainResponseWarmPendingCount;
    }

    // float pool budget で response grid が authored より小さくなる場合、
    // 最初の接触を理由に地形全体を粗い topology へ切り替えない。
    // 同じ grid 以上だけを response model として採用し、凹み更新時も
    // authored topology を安定させる。
    const bool useResponseGrid =
        responseModelReady && responseGrid >= authoredGrid && (responseGridActive || responseGrid > authoredGrid);
    const uint32_t gridRes = useResponseGrid ? responseGrid : authoredGrid;
    if (!responseModelReady || responseGrid <= authoredGrid) {
        output.warmedModel = nullptr;
        output.warmedClusteredGeometryPath.clear();
    }
    const uint32_t stride = gridRes + 1u;
    const std::size_t gridCount = static_cast<std::size_t>(stride) * stride;

    const TerrainProjectionLayout layout{
        minKeyX, minKeyZ, maxKeyX, maxKeyZ, chunkSize, cells, gridRes, layerCount,
    };
    const bool fullProjection = !state.layoutValid || state.layout != layout;
    const auto ensureField = [&](std::shared_ptr<std::vector<float>>& field, std::size_t requiredCount, bool enabled) {
        if (!field) {
            field = std::make_shared<std::vector<float>>();
        }
        if (!enabled) {
            field->clear();
            return;
        }
        if (field->size() != requiredCount) {
            field->assign(requiredCount, 0.0f);
            ++stats.terrainProjectionAllocationCount;
        }
    };
    ensureField(state.heights, gridCount, true);
    // response field も load-time projection と同時に確保して 0 初期化する。
    // 最初の接触では changed chunk だけを書き、全 terrain を作り直さない。
    ensureField(state.deformations, gridCount, true);
    ensureField(state.coverage, gridCount, true);
    ensureField(state.layerWeights, gridCount * static_cast<std::size_t>(layerCount), layerCount > 1u);

    const float gridCellX = extentX / static_cast<float>(gridRes);
    const float gridCellZ = extentZ / static_cast<float>(gridRes);
    const float originX = static_cast<float>(minKeyX) * chunkSize;
    const float originZ = static_cast<float>(minKeyZ) * chunkSize;

    // bbox の最大端は floor すると「次の chunk」になる。存在する左/下
    // chunk も候補にし、最終頂点を 0 coverage へ落とさない。
    const auto findChunkAt = [&](float localSurfaceX, float localSurfaceZ, TERRAIN::TerrainChunkKey& outKey,
                                 const TERRAIN::TerrainChunk*& outChunk, float& outLocalX, float& outLocalZ) {
        const TERRAIN::TerrainChunkKey base =
            TERRAIN::TerrainChunkKeyFromWorld(localSurfaceX, localSurfaceZ, chunkSize);
        constexpr int32_t offsets[2] = {0, -1};
        for (const int32_t dz : offsets) {
            for (const int32_t dx : offsets) {
                const TERRAIN::TerrainChunkKey candidate{base.x + dx, base.z + dz};
                const auto found = chunks.find(candidate);
                if (found == chunks.end()) {
                    continue;
                }
                const float cx = localSurfaceX - static_cast<float>(candidate.x) * chunkSize;
                const float cz = localSurfaceZ - static_cast<float>(candidate.z) * chunkSize;
                constexpr float epsilon = 1.0e-4f;
                if (cx < -epsilon || cx > chunkSize + epsilon || cz < -epsilon || cz > chunkSize + epsilon) {
                    continue;
                }
                outKey = candidate;
                outChunk = &found->second;
                outLocalX = std::clamp(cx, 0.0f, chunkSize);
                outLocalZ = std::clamp(cz, 0.0f, chunkSize);
                return true;
            }
        }
        return false;
    };

    const auto projectRect = [&](uint32_t minGridX, uint32_t minGridZ, uint32_t maxGridX, uint32_t maxGridZ) {
        for (uint32_t gz = minGridZ; gz <= maxGridZ; ++gz) {
            const float wz = originZ + static_cast<float>(gz) * gridCellZ;
            for (uint32_t gx = minGridX; gx <= maxGridX; ++gx) {
                const float wx = originX + static_cast<float>(gx) * gridCellX;
                const std::size_t dst = static_cast<std::size_t>(gz) * stride + gx;
                (*state.heights)[dst] = 0.0f;
                (*state.coverage)[dst] = 0.0f;
                (*state.deformations)[dst] = 0.0f;
                if (layerCount > 1u) {
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        (*state.layerWeights)[static_cast<std::size_t>(layer) * gridCount + dst] = 0.0f;
                    }
                }
                TERRAIN::TerrainChunkKey key{};
                const TERRAIN::TerrainChunk* chunk = nullptr;
                float localX = 0.0f;
                float localZ = 0.0f;
                if (!findChunkAt(wx, wz, key, chunk, localX, localZ)) {
                    continue;
                }
                const auto runtimeFound = state.runtimeByChunk.find(key);
                const TERRAIN::TerrainChunkRuntimeState* runtime =
                    runtimeFound != state.runtimeByChunk.end() ? runtimeFound->second.get() : nullptr;
                float cov = 0.0f;
                const bool sampledRuntimeCoverage =
                    runtime != nullptr &&
                    SampleTerrainGrid(runtime->coverage, 0u, runtime->cells, runtime->cellSize, localX, localZ, cov);
                if (!sampledRuntimeCoverage) {
                    const uint32_t authoredCells = (std::max)(chunk->cells, 1u);
                    const float authoredCellSize = chunkSize / static_cast<float>(authoredCells);
                    (void)SampleTerrainGrid(chunk->coverage, 0u, authoredCells, authoredCellSize, localX, localZ, cov);
                }
                (*state.coverage)[dst] = cov;
                if (cov <= 1.0e-4f) {
                    continue;
                }

                float totalHeight = 0.0f;
                bool sampledTotal =
                    runtime != nullptr && SampleTerrainGrid(runtime->heights, 0u, runtime->cells, runtime->cellSize,
                                                            localX, localZ, totalHeight);
                const uint32_t authoredCells = (std::max)(chunk->cells, 1u);
                const float authoredCellSize = chunkSize / static_cast<float>(authoredCells);
                if (!sampledTotal) {
                    sampledTotal = SampleTerrainGrid(chunk->heights, 0u, authoredCells, authoredCellSize, localX,
                                                     localZ, totalHeight);
                }
                if (sampledTotal) {
                    (*state.heights)[dst] = totalHeight;
                }

                if (responseGridActive && runtime != nullptr) {
                    float deformation = 0.0f;
                    if (SampleTerrainGrid(runtime->deformHeights, 0u, runtime->cells, runtime->cellSize, localX, localZ,
                                          deformation)) {
                        (*state.deformations)[dst] = deformation;
                    }
                }

                if (layerCount <= 1u) {
                    continue;
                }
                const std::size_t authoredVertexCount = TERRAIN::TerrainChunkVertexCount(authoredCells);
                for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                    const std::size_t lsrc = static_cast<std::size_t>(layer) * authoredVertexCount;
                    float weight = 0.0f;
                    if (SampleTerrainGrid(chunk->layerWeights, lsrc, authoredCells, authoredCellSize, localX, localZ,
                                          weight)) {
                        (*state.layerWeights)[static_cast<std::size_t>(layer) * gridCount + dst] = weight;
                    }
                }
            }
        }
        stats.terrainProjectedVertexCount +=
            static_cast<uint64_t>(maxGridX - minGridX + 1u) * static_cast<uint64_t>(maxGridZ - minGridZ + 1u);
    };

    if (fullProjection) {
        projectRect(0u, 0u, gridRes, gridRes);
        ++stats.terrainFullProjectionCount;
    } else if (!state.changedChunks.empty()) {
        for (const TERRAIN::TerrainChunkKey key : state.changedChunks) {
            const float chunkOriginX = static_cast<float>(key.x) * chunkSize;
            const float chunkOriginZ = static_cast<float>(key.z) * chunkSize;
            const int32_t minGridX = static_cast<int32_t>(std::floor((chunkOriginX - originX) / gridCellX)) - 1;
            const int32_t minGridZ = static_cast<int32_t>(std::floor((chunkOriginZ - originZ) / gridCellZ)) - 1;
            const int32_t maxGridX =
                static_cast<int32_t>(std::ceil((chunkOriginX + chunkSize - originX) / gridCellX)) + 1;
            const int32_t maxGridZ =
                static_cast<int32_t>(std::ceil((chunkOriginZ + chunkSize - originZ) / gridCellZ)) + 1;
            projectRect(static_cast<uint32_t>((std::clamp)(minGridX, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(minGridZ, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(maxGridX, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(maxGridZ, 0, static_cast<int32_t>(gridRes))));
        }
        ++stats.terrainIncrementalProjectionCount;
    } else {
        ++stats.terrainProjectionReuseCount;
    }

    if (fullProjection || !state.changedChunks.empty()) {
        ++state.projectionRevision;
    }
    state.layout = layout;
    state.layoutValid = true;

    // chunk 経路でもスタイル/変形パラメータを全同期する。
    // 以前は既定値のままだったため、パネルで変えても反映されなかった。
    desc.terrainSurfaceData.gridResolution = gridRes;
    desc.terrainSurfaceData.extents = {extentX, extentZ};
    desc.terrainSurfaceData.heightScale = surface.heightScale;
    desc.terrainSurfaceData.rimStrength = surface.deform.rimStrength;
    desc.terrainSurfaceData.lightTint = surface.material.lightTint;
    desc.terrainSurfaceData.darkTint = surface.material.darkTint;
    desc.terrainSurfaceData.sparkleStrength = surface.material.sparkleStrength;
    desc.terrainSurfaceData.rippleAmount = surface.material.rippleAmount;
    desc.terrainSurfaceData.pitDarkening = surface.material.pitDarkening;
    desc.terrainSurfaceData.toonBandCount = surface.material.toonBandCount;
    desc.terrainSurfaceData.toonBandHardness = surface.material.toonBandHardness;
    desc.terrainSurfaceData.layerCount = layerCount;
    desc.terrainSurfaceData.layerWeights.clear();
    desc.terrainSurfaceData.coverage.clear();
    desc.terrainSurfaceData.wetness = surface.shoreline.wetness;
    desc.terrainSurfaceData.edgeDepth = surface.shoreline.edgeDepth;

    // 全レイヤーの atlas/toon style を解決する。
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    auto& styles = desc.terrainSurfaceData.layerStyles;
    styles.assign(layerCount, BuildTerrainLayerRenderStyle(TERRAIN::MakeSandPreset(), surface.stylization));
    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
        const std::string_view id =
            TERRAIN::ResolveTerrainLayerPresetId(surface.layerPresetIds, surface.presetId, layer);
        const TERRAIN::SurfacePreset* preset = id.empty() ? nullptr : TERRAIN::SurfaceLayerRegistry::Get().Find(id);
        if (preset != nullptr) {
            styles[layer] = BuildTerrainLayerRenderStyle(*preset, surface.stylization);
        } else if (layer == 0u) {
            TERRAIN::SurfacePreset fallback = TERRAIN::MakeSandPreset();
            fallback.tint.light = surface.material.lightTint;
            fallback.tint.dark = surface.material.darkTint;
            styles[layer] = BuildTerrainLayerRenderStyle(fallback, surface.stylization);
        }
    }

    // 生成グリッドは原点中心なので、地形オブジェクトの本来の world
    // 行列に chunk bbox のローカル中心を合成する。
    const MATH::Mat4 objectWorld = desc.worldTransform.GetWorldMatrix();
    desc.worldTransform.useExplicitMatrix = true;
    desc.worldTransform.explicitMatrix =
        objectWorld * MATH::Mat4::Translate({originX + extentX * 0.5f, 0.0f, originZ + extentZ * 0.5f});

    desc.terrainSurfaceHeights = state.heights;
    desc.terrainSurfaceLayerWeights = state.layerWeights;
    desc.terrainSurfaceCoverage = state.coverage;
    if (responseGridActive) {
        desc.terrainSurfaceDeformations = state.deformations;
    }
    desc.terrainSurfaceProjectionRevision = state.projectionRevision;

    // authored 高さと response profile から保守 bounds を作り、8m 単位で
    // 量子化して只拡張する。dent tick や通常の brush sample が毎回
    // topology rebuild を起こさず、極値を越えた時だけ安全側へ広がる。
    const float rippleMargin = std::clamp(desc.terrainSurfaceData.rippleAmount, 0.0f, 3.0f) *
                               std::clamp(desc.terrainSurfaceData.heightScale, 0.0f, 2.0f) * 0.021f;
    const float desiredMin = state.authoredMinHeight - (std::max)(surface.shoreline.edgeDepth, 0.0f) -
                             (std::max)(surface.deform.maxDepth, 0.0f) - rippleMargin;
    const float desiredMax = state.authoredMaxHeight +
                             (std::max)(surface.deform.maxDepth, 0.0f) * (std::max)(surface.deform.rimStrength, 0.0f) +
                             rippleMargin;
    constexpr float kBoundsQuantum = 8.0f;
    const float quantizedMin = std::floor(desiredMin / kBoundsQuantum) * kBoundsQuantum;
    const float quantizedMax = std::ceil(desiredMax / kBoundsQuantum) * kBoundsQuantum;
    if (!state.stableBoundsValid) {
        state.stableBoundsMinY = quantizedMin;
        state.stableBoundsMaxY = quantizedMax;
        state.stableBoundsValid = true;
    } else {
        state.stableBoundsMinY = (std::min)(state.stableBoundsMinY, quantizedMin);
        state.stableBoundsMaxY = (std::max)(state.stableBoundsMaxY, quantizedMax);
    }
    output.meshSettings = responseSettings;
    output.meshSettings.segmentsX = gridRes;
    output.meshSettings.segmentsY = gridRes;
    output.stableBoundsMinY = state.stableBoundsMinY;
    output.stableBoundsMaxY = state.stableBoundsMaxY;
}

} // namespace HIKARI::TERRAIN::RENDER
