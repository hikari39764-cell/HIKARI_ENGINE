#include "Scene/HIKARI_SceneRenderCacheSync.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"
#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_PresentationModelRenderProjection.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceMaterialBinder.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"
#include "Scene/Terrain/HIKARI_TerrainGrassRenderProjection.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"


namespace HIKARI {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTerrainAtlasContentScale = 1008.0f / 2048.0f;
        constexpr float kTerrainAtlasGutterBias = 8.0f / 2048.0f;

        MATH::Vec4 ResolveTerrainAtlasScaleBias(uint32_t tile) noexcept {
            const uint32_t safeTile = (std::min)(tile, 3u);
            return {
                kTerrainAtlasContentScale,
                kTerrainAtlasContentScale,
                static_cast<float>(safeTile & 1u) * 0.5f +
                    kTerrainAtlasGutterBias,
                static_cast<float>(safeTile >> 1u) * 0.5f +
                    kTerrainAtlasGutterBias,
            };
        }

        RENDER3D::SAND::SandSurfaceLayerRenderStyle
            BuildTerrainLayerRenderStyle(
                const TERRAIN::SurfacePreset& preset,
                const TERRAIN::TerrainStylizationSettings& authoredSettings)
                noexcept {

            const TERRAIN::SurfaceLayerMaterialParams& authored =
                preset.layerMaterial;
            const TERRAIN::TerrainStylizationSettings stylization =
                TERRAIN::SanitizeTerrainStylizationSettings(authoredSettings);
            RENDER3D::SAND::SandSurfaceLayerRenderStyle style{};
            style.lightTint = preset.tint.light;
            style.textureStrength = std::clamp(
                authored.textureStrength * stylization.textureStrength,
                0.0f,
                2.0f);
            style.darkTint = preset.tint.dark;
            style.normalStrength = std::clamp(
                authored.normalStrength * stylization.normalStrength,
                0.0f,
                2.0f);
            style.atlasScaleBias = ResolveTerrainAtlasScaleBias(
                authored.atlasTile);
            style.tilesPerMeter = 1.0f / (std::max)(
                authored.metersPerTile, 0.05f) * stylization.textureScale;
            style.roughness = std::clamp(
                authored.roughness, 0.0f, 1.0f);
            const float saturation =
                authored.kind == TERRAIN::SurfaceLayerMaterialKind::Rock
                    ? stylization.rockSaturation
                    : authored.saturation * stylization.saturation;
            style.saturation = std::clamp(
                saturation, 0.0f, 2.0f);
            style.materialKind = static_cast<uint32_t>(authored.kind);
            style.shadowStrength = std::clamp(
                authored.shadowStrength * stylization.shadowStrength,
                0.0f,
                1.0f);
            style.granularStrength =
                authored.kind == TERRAIN::SurfaceLayerMaterialKind::Sand ||
                authored.kind == TERRAIN::SurfaceLayerMaterialKind::Snow
                    ? 1.0f : 0.0f;
            style.facetSizeMeters = stylization.facetSizeMeters;
            style.facetStrength = stylization.facetStrength;
            return style;
        }

        std::string_view ResolveLegacyTerrainLayerPresetId(
            uint32_t layer) noexcept {
            constexpr std::array<std::string_view, 5u> kIds{
                "sand", "snow", "dirt", "grass", "rock" };
            return layer < kIds.size() ? kIds[layer] : kIds.front();
        }

        RENDER3D::WATER::WaterSurfaceRenderData
            BuildWaterSurfaceRenderData(
                const WATER::WaterSurfaceSettings& authoredSettings,
                const ProceduralMeshComponent& procedural,
                const MATH::Mat4& world) {

            const ProceduralMeshSettings mesh =
                SanitizeProceduralMeshSettings(
                    procedural.GetSettings());
            const float spacingX = mesh.width /
                static_cast<float>((std::max)(mesh.segmentsX, 1u));
            const float spacingZ = mesh.depth /
                static_cast<float>((std::max)(mesh.segmentsY, 1u));
            const WATER::WaterSurfaceSettings settings =
                WATER::ResolveWaterSurfaceRuntimeSettings(
                    authoredSettings,
                    (std::max)(spacingX, spacingZ),
                    world);

            RENDER3D::WATER::WaterSurfaceRenderData data{};
            data.shadingModel = settings.preset ==
                    WATER::WaterSurfacePreset::RealisticClearSummer
                ? RENDER3D::WATER::WaterSurfaceShadingModel::
                    RealisticClearSummer
                : RENDER3D::WATER::WaterSurfaceShadingModel::AnimeSummer;
            data.geometryMode = settings.geometryMode ==
                    WATER::WaterSurfaceGeometryMode::CameraCenteredOcean
                ? RENDER3D::WATER::WaterSurfaceGeometryMode::
                    CameraCenteredOcean
                : RENDER3D::WATER::WaterSurfaceGeometryMode::
                    FiniteAuthored;
            data.timeScale = settings.timeScale;
            data.choppiness = settings.choppiness;
            data.microWaveStrength = settings.microWaveStrength;
            data.depthTransmittance = settings.depthTransmittance;
            data.scatteringColor = settings.scatteringColor;
            data.shallowClearDepth = settings.shallowClearDepth;
            data.shallowBlendDepth = settings.shallowBlendDepth;
            data.shallowGraphicWeight = settings.shallowGraphicWeight;
            data.foamColor = settings.foamColor;
            data.foamAmount = settings.foamAmount;
            data.absorptionDistance = settings.absorptionDistance;
            data.surfaceRoughness = settings.surfaceRoughness;
            data.directSunStrength = settings.directSunStrength;
            data.transmissionStrength = settings.transmissionStrength;
            data.shadowStrength = settings.shadowStrength;
            data.reflectionStrength = settings.reflectionStrength;
            data.sunGlintStrength = settings.sunGlintStrength;
            data.glintDensity = settings.glintDensity;
            data.glintFlowSpeed = settings.glintFlowSpeed;
            data.ribbonWidth = settings.ribbonWidth;
            data.windDirectionRadians = settings.windDirectionDeg *
                (kPi / 180.0f);
            data.windSpeed = settings.windSpeed;
            data.shortWaveStrength = settings.shortWaveStrength;
            data.capillaryStrength = settings.capillaryStrength;
            data.geometryParams = {
                0.0f,
                0.0f,
                (std::max)(mesh.width * 0.5f, 0.001f),
                (std::max)(mesh.depth * 0.5f, 0.001f)
            };
            for (size_t index = 0u; index < data.waves.size(); ++index) {
                const WATER::WaterGerstnerWave& source =
                    settings.waves[index];
                RENDER3D::WATER::WaterWaveRenderData& destination =
                    data.waves[index];
                destination.directionRadians =
                    source.directionDeg * (kPi / 180.0f);
                destination.amplitude = source.amplitude;
                destination.wavelength = source.wavelength;
                destination.steepness = source.steepness;
            }
            return data;
        }

        RENDER3D::SAND::SandSurfaceRenderData
            BuildSandSurfaceRenderData(
                const SAND::SandSurfaceSettings& authoredSettings,
                const ProceduralMeshComponent& procedural) {

            const ProceduralMeshSettings mesh =
                SanitizeProceduralMeshSettings(
                    procedural.GetSettings());
            const SAND::SandSurfaceSettings settings =
                SAND::SanitizeSandSurfaceSettings(authoredSettings);

            RENDER3D::SAND::SandSurfaceRenderData data{};
            data.gridResolution = settings.gridResolution;
            data.extents = { mesh.width, mesh.depth };
            data.heightScale = settings.heightScale;
            data.rimStrength = settings.rimStrength;
            data.lightTint = settings.lightTint;
            data.sparkleStrength = settings.sparkleStrength;
            data.darkTint = settings.darkTint;
            data.rippleAmount = settings.rippleAmount;
            data.pitDarkening = settings.pitDarkening;
            data.toonBandCount = settings.toonBandCount;
            data.toonBandHardness = settings.toonBandHardness;
            data.edgeDepth = settings.edgeDepth;
            return data;
        }

        struct TerrainProjectionLayout {
            int32_t minKeyX = 0;
            int32_t minKeyZ = 0;
            int32_t maxKeyX = 0;
            int32_t maxKeyZ = 0;
            float chunkSize = 0.0f;
            uint32_t authoredCells = 0u;
            uint32_t gridResolution = 0u;
            uint32_t layerCount = 1u;

            bool operator==(const TerrainProjectionLayout&) const = default;
        };

        struct TerrainProjectionState {
            struct AuthoredChunkState {
                uint64_t revision = 0u;
                float minHeight = 0.0f;
                float maxHeight = 0.0f;
            };

            RuntimeObjectHandle sourceObject{};
            TerrainProjectionLayout layout{};
            bool layoutValid = false;
            uint64_t authoredRevision = 0u;
            std::map<TERRAIN::TerrainChunkKey, AuthoredChunkState>
                authoredChunks{};
            std::map<TERRAIN::TerrainChunkKey, uint64_t> runtimeRevisions{};
            std::map<
                TERRAIN::TerrainChunkKey,
                SAND::SandChunkRuntimeSnapshotPtr> runtimeByChunk{};
            std::vector<TERRAIN::TerrainChunkKey> changedChunks{};
            std::shared_ptr<std::vector<float>> heights{};
            std::shared_ptr<std::vector<float>> deformations{};
            std::shared_ptr<std::vector<float>> coverage{};
            std::shared_ptr<std::vector<float>> layerWeights{};
            uint64_t projectionRevision = 0u;
            float authoredMinHeight = 0.0f;
            float authoredMaxHeight = 0.0f;
            float stableBoundsMinY = 0.0f;
            float stableBoundsMaxY = 0.0f;
            bool stableBoundsValid = false;
            ProceduralMeshSettings warmSettings{};
            std::filesystem::path warmProjectRoot{};
            bool warmSettingsValid = false;
            bool warmPending = false;
        };

        void AppendChangedChunk(
            std::vector<TERRAIN::TerrainChunkKey>& chunks,
            TERRAIN::TerrainChunkKey key) {

            if (std::find(chunks.begin(), chunks.end(), key) ==
                chunks.end()) {
                chunks.push_back(key);
            }
        }

        uint32_t ResolveTerrainProjectionGrid(
            uint32_t totalCellsX,
            uint32_t totalCellsZ,
            uint32_t projectionScale,
            uint32_t layerCount,
            bool includeDeformation) {

            constexpr uint32_t kMaxGrid = 512u;
            const std::size_t fieldsPerVertex =
                2u + // total height + coverage
                (includeDeformation ? 1u : 0u) +
                (layerCount > 1u
                    ? static_cast<std::size_t>(layerCount)
                    : 0u);
            const std::size_t fixedPoolFloats =
                RENDER3D::RUNTIME::kSceneSurfaceNeutralFloatCount +
                (layerCount > 1u
                    ? static_cast<std::size_t>(layerCount) *
                        RENDER3D::SAND::
                            kSandSurfaceLayerStyleFloatCount
                    : 0u);
            const std::size_t availableVertexFloats =
                RENDER3D::SAND::kSandSurfaceMaxHeightCells > fixedPoolFloats
                    ? RENDER3D::SAND::kSandSurfaceMaxHeightCells -
                        fixedPoolFloats
                    : 0u;
            const std::size_t vertexBudget = fieldsPerVertex > 0u
                ? availableVertexFloats / fieldsPerVertex
                : 0u;
            const double budgetStride = std::sqrt(
                static_cast<double>(vertexBudget));
            const uint32_t budgetGrid = budgetStride >= 2.0
                ? static_cast<uint32_t>(budgetStride) - 1u
                : 1u;
            const uint64_t desiredGrid =
                static_cast<uint64_t>((std::max)(
                    totalCellsX, totalCellsZ)) *
                (std::max)(projectionScale, 1u);
            return (std::max)(1u, (std::min)(
                static_cast<uint32_t>((std::min)(
                    desiredGrid,
                    static_cast<uint64_t>(kMaxGrid))),
                budgetGrid));
        }

        bool SampleTerrainGrid(
            const std::vector<float>& values,
            std::size_t baseOffset,
            uint32_t sourceCells,
            float sourceCellSize,
            float localX,
            float localZ,
            float& outValue) {

            if (sourceCells == 0u ||
                !std::isfinite(sourceCellSize) ||
                sourceCellSize <= 1.0e-4f) {
                return false;
            }
            const uint32_t sourceStride = sourceCells + 1u;
            const std::size_t sourceCount =
                static_cast<std::size_t>(sourceStride) * sourceStride;
            if (baseOffset > values.size() ||
                sourceCount > values.size() - baseOffset) {
                return false;
            }
            const float fx = std::clamp(
                localX / sourceCellSize,
                0.0f,
                static_cast<float>(sourceCells));
            const float fz = std::clamp(
                localZ / sourceCellSize,
                0.0f,
                static_cast<float>(sourceCells));
            const uint32_t x0 = static_cast<uint32_t>(fx);
            const uint32_t z0 = static_cast<uint32_t>(fz);
            const uint32_t x1 = (std::min)(x0 + 1u, sourceCells);
            const uint32_t z1 = (std::min)(z0 + 1u, sourceCells);
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            const auto sample = [&](uint32_t x, uint32_t z) {
                return values[
                    baseOffset +
                    static_cast<std::size_t>(z) * sourceStride + x];
            };
            outValue = std::lerp(
                std::lerp(sample(x0, z0), sample(x1, z0), tx),
                std::lerp(sample(x0, z1), sample(x1, z1), tx),
                tz);
            return std::isfinite(outValue);
        }

        struct TerrainProjectionOutput {
            const ModelAsset* warmedModel = nullptr;
            std::string warmedClusteredGeometryPath{};
            ProceduralMeshSettings meshSettings{};
            float stableBoundsMinY = 0.0f;
            float stableBoundsMaxY = 0.0f;
        };

        void BakeTerrainChunksForRender(
            const TERRAIN::TerrainSurface& surface,
            const TerrainSurfaceComponent& terrain,
            RuntimeObjectHandle objectHandle,
            const SAND::SandSurfaceQueryService* sandSurfaceQuery,
            const std::filesystem::path& projectRoot,
            float chunkSize,
            uint32_t cells,
            TerrainProjectionState& state,
            SceneRenderCacheSync::DebugStats& stats,
            TerrainProjectionOutput& output,
            RENDER3D::RUNTIME::SceneRenderObjectDesc& desc) {

            desc.sandSurfaceHeights.reset();
            desc.sandSurfaceDeformations.reset();
            desc.sandSurfaceLayerWeights.reset();
            desc.sandSurfaceCoverage.reset();
            const TERRAIN::TerrainChunkMap& chunks = surface.chunks;
            if (chunks.empty()) {
                desc.sandSurface = false;
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
            const uint32_t totalCellsX = (std::max)(1u,
                static_cast<uint32_t>(maxKeyX - minKeyX + 1) * cells);
            const uint32_t totalCellsZ = (std::max)(1u,
                static_cast<uint32_t>(maxKeyZ - minKeyZ + 1) * cells);

            state.changedChunks.clear();
            const uint64_t authoredRevision = terrain.GetRevision();
            if (!state.layoutValid ||
                state.authoredRevision != authoredRevision) {

                for (const auto& [key, chunk] : chunks) {
                    const uint64_t chunkRevision =
                        terrain.GetChunkRevision(key);
                    const auto old = state.authoredChunks.find(key);
                    if (old == state.authoredChunks.end() ||
                        old->second.revision != chunkRevision) {
                        AppendChangedChunk(state.changedChunks, key);
                        TerrainProjectionState::AuthoredChunkState next{};
                        next.revision = chunkRevision;
                        float chunkMin =
                            (std::numeric_limits<float>::max)();
                        float chunkMax =
                            (std::numeric_limits<float>::lowest)();
                        for (const float height : chunk.heights) {
                            if (!std::isfinite(height)) {
                                continue;
                            }
                            chunkMin = (std::min)(chunkMin, height);
                            chunkMax = (std::max)(chunkMax, height);
                        }
                        next.minHeight = std::isfinite(chunkMin)
                            ? chunkMin : 0.0f;
                        next.maxHeight = std::isfinite(chunkMax)
                            ? chunkMax : 0.0f;
                        state.authoredChunks[key] = next;
                    }
                }
                for (auto it = state.authoredChunks.begin();
                    it != state.authoredChunks.end();) {

                    if (chunks.find(it->first) != chunks.end()) {
                        ++it;
                        continue;
                    }
                    AppendChangedChunk(state.changedChunks, it->first);
                    it = state.authoredChunks.erase(it);
                }
                float authoredMin =
                    (std::numeric_limits<float>::max)();
                float authoredMax =
                    (std::numeric_limits<float>::lowest)();
                for (const auto& [key, chunkState] :
                    state.authoredChunks) {
                    (void)key;
                    authoredMin = (std::min)(
                        authoredMin, chunkState.minHeight);
                    authoredMax = (std::max)(
                        authoredMax, chunkState.maxHeight);
                }
                state.authoredMinHeight = std::isfinite(authoredMin)
                    ? authoredMin : 0.0f;
                state.authoredMaxHeight = std::isfinite(authoredMax)
                    ? authoredMax : 0.0f;
                state.authoredRevision = authoredRevision;
            }

            // Runtime snapshot は stateRevision だけを比較し、変更 chunk の
            // immutable storage だけ差し替える。fixed tick ごとの大配列複製は
            // Sand owner 側に閉じ、render bridge は参照を保持する。
            bool responseGridActive = false;
            uint32_t responseProjectionScale = 1u;
            for (const auto& [key, chunk] : chunks) {
                (void)chunk;
                SAND::SandChunkRuntimeSnapshotPtr runtime{};
                const bool runtimeValid = sandSurfaceQuery != nullptr &&
                    sandSurfaceQuery->TryGetChunkState(
                        objectHandle, key, runtime) &&
                    runtime && runtime->cells > 0u &&
                    std::isfinite(runtime->cellSize) &&
                    runtime->cellSize > 1.0e-4f;
                const auto oldRevision = state.runtimeRevisions.find(key);
                if (!runtimeValid) {
                    if (oldRevision != state.runtimeRevisions.end()) {
                        AppendChangedChunk(state.changedChunks, key);
                        state.runtimeRevisions.erase(oldRevision);
                        state.runtimeByChunk.erase(key);
                    }
                    continue;
                }

                if (oldRevision == state.runtimeRevisions.end() ||
                    oldRevision->second != runtime->stateRevision) {
                    AppendChangedChunk(state.changedChunks, key);
                    state.runtimeRevisions[key] = runtime->stateRevision;
                    state.runtimeByChunk[key] = runtime;
                }
                responseGridActive = responseGridActive ||
                    runtime->responseGridActive;
                const uint32_t runtimeAuthoredCells =
                    (std::max)(runtime->authoredCells, 1u);
                const uint64_t responseScale =
                    (static_cast<uint64_t>(runtime->cells) +
                        runtimeAuthoredCells - 1u) /
                    runtimeAuthoredCells;
                responseProjectionScale = (std::max)(
                    responseProjectionScale,
                    static_cast<uint32_t>((std::max)(
                        responseScale,
                        uint64_t{ 1u })));
            }
            for (auto it = state.runtimeRevisions.begin();
                it != state.runtimeRevisions.end();) {

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

            const uint32_t authoredGrid = ResolveTerrainProjectionGrid(
                totalCellsX, totalCellsZ, 1u, layerCount, false);
            const uint32_t configuredResponseScale = (std::clamp)(
                surface.deform.responseResolutionScale, 1u, 4u);
            const uint32_t desiredResponseScale = responseGridActive
                ? responseProjectionScale
                : configuredResponseScale;
            const uint32_t responseGrid = ResolveTerrainProjectionGrid(
                totalCellsX,
                totalCellsZ,
                desiredResponseScale,
                layerCount,
                true);

            ProceduralMeshSettings responseSettings{};
            responseSettings.kind = ProceduralMeshKind::GridPlane;
            responseSettings.width = extentX;
            responseSettings.depth = extentZ;
            responseSettings.segmentsX = responseGrid;
            responseSettings.segmentsY = responseGrid;
            // uv0 の材質 tile 密度は authored lattice に固定する。
            // response grid が高密度へ切り替わっても、地面全体の
            // テクスチャ尺度は変えない。uv1 は正規化 field 座標のまま。
            responseSettings.uvTilesX = static_cast<float>(
                (std::max)(totalCellsX, 1u));
            responseSettings.uvTilesY = static_cast<float>(
                (std::max)(totalCellsZ, 1u));
            bool responseModelReady = responseGrid <= authoredGrid;
            if (responseGrid > authoredGrid && !projectRoot.empty()) {
                if (!state.warmSettingsValid ||
                    state.warmSettings != responseSettings ||
                    state.warmProjectRoot != projectRoot) {

                    state.warmSettings = responseSettings;
                    state.warmProjectRoot = projectRoot;
                    state.warmSettingsValid = true;
                    ++stats.terrainResponseWarmRequestCount;
                }
                PROCEDURAL::RequestClusteredGeometryWarmUp(
                    responseSettings, projectRoot);
                responseModelReady =
                    PROCEDURAL::TryGetWarmedClusteredGeometry(
                        responseSettings,
                        projectRoot,
                        output.warmedModel,
                        output.warmedClusteredGeometryPath);
            }
            state.warmPending =
                responseGrid > authoredGrid && !responseModelReady;
            if (state.warmPending) {
                ++stats.terrainResponseWarmPendingCount;
            }

            // float pool budget で response grid が authored より小さくなる場合、
            // 最初の接触を理由に地形全体を粗い topology へ切り替えない。
            // 同じ grid 以上だけを response model として採用し、凹み更新時も
            // authored topology を安定させる。
            const bool useResponseGrid = responseModelReady &&
                responseGrid >= authoredGrid &&
                (responseGridActive || responseGrid > authoredGrid);
            const uint32_t gridRes = useResponseGrid
                ? responseGrid
                : authoredGrid;
            if (!responseModelReady ||
                responseGrid <= authoredGrid) {
                output.warmedModel = nullptr;
                output.warmedClusteredGeometryPath.clear();
            }
            const uint32_t stride = gridRes + 1u;
            const std::size_t gridCount =
                static_cast<std::size_t>(stride) * stride;

            const TerrainProjectionLayout layout{
                minKeyX,
                minKeyZ,
                maxKeyX,
                maxKeyZ,
                chunkSize,
                cells,
                gridRes,
                layerCount,
            };
            const bool fullProjection =
                !state.layoutValid || state.layout != layout;
            const auto ensureField = [&](
                std::shared_ptr<std::vector<float>>& field,
                std::size_t requiredCount,
                bool enabled) {

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
            ensureField(
                state.layerWeights,
                gridCount * static_cast<std::size_t>(layerCount),
                layerCount > 1u);

            const float gridCellX = extentX / static_cast<float>(gridRes);
            const float gridCellZ = extentZ / static_cast<float>(gridRes);
            const float originX = static_cast<float>(minKeyX) * chunkSize;
            const float originZ = static_cast<float>(minKeyZ) * chunkSize;

            // bbox の最大端は floor すると「次の chunk」になる。存在する左/下
            // chunk も候補にし、最終頂点を 0 coverage へ落とさない。
            const auto findChunkAt = [&] (
                float localSurfaceX,
                float localSurfaceZ,
                TERRAIN::TerrainChunkKey& outKey,
                const TERRAIN::TerrainChunk*& outChunk,
                float& outLocalX,
                float& outLocalZ) {
                const TERRAIN::TerrainChunkKey base =
                    TERRAIN::TerrainChunkKeyFromWorld(
                        localSurfaceX, localSurfaceZ, chunkSize);
                constexpr int32_t offsets[2] = { 0, -1 };
                for (const int32_t dz : offsets) {
                    for (const int32_t dx : offsets) {
                        const TERRAIN::TerrainChunkKey candidate{
                            base.x + dx, base.z + dz };
                        const auto found = chunks.find(candidate);
                        if (found == chunks.end()) {
                            continue;
                        }
                        const float cx = localSurfaceX -
                            static_cast<float>(candidate.x) * chunkSize;
                        const float cz = localSurfaceZ -
                            static_cast<float>(candidate.z) * chunkSize;
                        constexpr float epsilon = 1.0e-4f;
                        if (cx < -epsilon || cx > chunkSize + epsilon ||
                            cz < -epsilon || cz > chunkSize + epsilon) {
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

            const auto projectRect = [&] (
                uint32_t minGridX,
                uint32_t minGridZ,
                uint32_t maxGridX,
                uint32_t maxGridZ) {

                for (uint32_t gz = minGridZ; gz <= maxGridZ; ++gz) {
                    const float wz =
                        originZ + static_cast<float>(gz) * gridCellZ;
                    for (uint32_t gx = minGridX; gx <= maxGridX; ++gx) {
                    const float wx = originX + static_cast<float>(gx) * gridCellX;
                    const std::size_t dst = static_cast<std::size_t>(gz) * stride + gx;
                    (*state.heights)[dst] = 0.0f;
                    (*state.coverage)[dst] = 0.0f;
                    (*state.deformations)[dst] = 0.0f;
                    if (layerCount > 1u) {
                        for (uint32_t layer = 0u;
                            layer < layerCount;
                            ++layer) {

                            (*state.layerWeights)[
                                static_cast<std::size_t>(layer) *
                                    gridCount + dst] = 0.0f;
                        }
                    }
                    TERRAIN::TerrainChunkKey key{};
                    const TERRAIN::TerrainChunk* chunk = nullptr;
                    float localX = 0.0f;
                    float localZ = 0.0f;
                    if (!findChunkAt(
                            wx, wz, key, chunk, localX, localZ)) {
                        continue;
                    }
                    const auto runtimeFound = state.runtimeByChunk.find(key);
                    const SAND::SandChunkRuntimeState* runtime =
                        runtimeFound != state.runtimeByChunk.end()
                            ? runtimeFound->second.get()
                            : nullptr;
                    float cov = 0.0f;
                    const bool sampledRuntimeCoverage = runtime != nullptr &&
                        SampleTerrainGrid(
                            runtime->coverage,
                            0u,
                            runtime->cells,
                            runtime->cellSize,
                            localX,
                            localZ,
                            cov);
                    if (!sampledRuntimeCoverage) {
                        const uint32_t authoredCells =
                            (std::max)(chunk->cells, 1u);
                        const float authoredCellSize = chunkSize /
                            static_cast<float>(authoredCells);
                        (void)SampleTerrainGrid(
                            chunk->coverage,
                            0u,
                            authoredCells,
                            authoredCellSize,
                            localX,
                            localZ,
                            cov);
                    }
                    (*state.coverage)[dst] = cov;
                    if (cov <= 1.0e-4f) {
                        continue;
                    }

                    float totalHeight = 0.0f;
                    bool sampledTotal = runtime != nullptr &&
                        SampleTerrainGrid(
                            runtime->heights,
                            0u,
                            runtime->cells,
                            runtime->cellSize,
                            localX,
                            localZ,
                            totalHeight);
                    const uint32_t authoredCells =
                        (std::max)(chunk->cells, 1u);
                    const float authoredCellSize = chunkSize /
                        static_cast<float>(authoredCells);
                    if (!sampledTotal) {
                        sampledTotal = SampleTerrainGrid(
                            chunk->heights,
                            0u,
                            authoredCells,
                            authoredCellSize,
                            localX,
                            localZ,
                            totalHeight);
                    }
                    if (sampledTotal) {
                        (*state.heights)[dst] = totalHeight;
                    }

                    if (responseGridActive && runtime != nullptr) {
                        float deformation = 0.0f;
                        if (SampleTerrainGrid(
                                runtime->deformHeights,
                                0u,
                                runtime->cells,
                                runtime->cellSize,
                                localX,
                                localZ,
                                deformation)) {
                            (*state.deformations)[dst] = deformation;
                        }
                    }

                    if (layerCount <= 1u) {
                        continue;
                    }
                    const std::size_t authoredVertexCount =
                        TERRAIN::TerrainChunkVertexCount(authoredCells);
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        const std::size_t lsrc =
                            static_cast<std::size_t>(layer) *
                            authoredVertexCount;
                        float weight = 0.0f;
                        if (SampleTerrainGrid(
                                chunk->layerWeights,
                                lsrc,
                                authoredCells,
                                authoredCellSize,
                                localX,
                                localZ,
                                weight)) {
                            (*state.layerWeights)[
                                static_cast<std::size_t>(layer) *
                                    gridCount + dst] = weight;
                        }
                    }
                }
                }
                stats.terrainProjectedVertexCount +=
                    static_cast<uint64_t>(maxGridX - minGridX + 1u) *
                    static_cast<uint64_t>(maxGridZ - minGridZ + 1u);
            };

            if (fullProjection) {
                projectRect(0u, 0u, gridRes, gridRes);
                ++stats.terrainFullProjectionCount;
            } else if (!state.changedChunks.empty()) {
                for (const TERRAIN::TerrainChunkKey key :
                    state.changedChunks) {

                    const float chunkOriginX =
                        static_cast<float>(key.x) * chunkSize;
                    const float chunkOriginZ =
                        static_cast<float>(key.z) * chunkSize;
                    const int32_t minGridX = static_cast<int32_t>(std::floor(
                        (chunkOriginX - originX) / gridCellX)) - 1;
                    const int32_t minGridZ = static_cast<int32_t>(std::floor(
                        (chunkOriginZ - originZ) / gridCellZ)) - 1;
                    const int32_t maxGridX = static_cast<int32_t>(std::ceil(
                        (chunkOriginX + chunkSize - originX) / gridCellX)) + 1;
                    const int32_t maxGridZ = static_cast<int32_t>(std::ceil(
                        (chunkOriginZ + chunkSize - originZ) / gridCellZ)) + 1;
                    projectRect(
                        static_cast<uint32_t>((std::clamp)(
                            minGridX, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(
                            minGridZ, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(
                            maxGridX, 0, static_cast<int32_t>(gridRes))),
                        static_cast<uint32_t>((std::clamp)(
                            maxGridZ, 0, static_cast<int32_t>(gridRes))));
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
            desc.sandSurfaceData.gridResolution = gridRes;
            desc.sandSurfaceData.extents = { extentX, extentZ };
            desc.sandSurfaceData.heightScale = surface.heightScale;
            desc.sandSurfaceData.rimStrength = surface.deform.rimStrength;
            desc.sandSurfaceData.lightTint = surface.material.lightTint;
            desc.sandSurfaceData.darkTint = surface.material.darkTint;
            desc.sandSurfaceData.sparkleStrength = surface.material.sparkleStrength;
            desc.sandSurfaceData.rippleAmount = surface.material.rippleAmount;
            desc.sandSurfaceData.pitDarkening = surface.material.pitDarkening;
            desc.sandSurfaceData.toonBandCount = surface.material.toonBandCount;
            desc.sandSurfaceData.toonBandHardness = surface.material.toonBandHardness;
            desc.sandSurfaceData.layerCount = layerCount;
            desc.sandSurfaceData.layerWeights.clear();
            desc.sandSurfaceData.coverage.clear();
            desc.sandSurfaceData.wetness = surface.shoreline.wetness;
            desc.sandSurfaceData.edgeDepth = surface.shoreline.edgeDepth;

            // 全レイヤーの atlas/toon style を解決する。layerPresetIds を持たない
            // 旧 scene は従来の sand/snow/dirt registry 順を維持する。
            TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
            auto& styles = desc.sandSurfaceData.layerStyles;
            styles.assign(
                layerCount,
                BuildTerrainLayerRenderStyle(
                    TERRAIN::MakeSandPreset(), surface.stylization));
            for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                const std::string_view id =
                    layer < surface.layerPresetIds.size() &&
                            !surface.layerPresetIds[layer].empty()
                        ? std::string_view(surface.layerPresetIds[layer])
                        : (layer == 0u && !surface.presetId.empty()
                            ? std::string_view(surface.presetId)
                            : ResolveLegacyTerrainLayerPresetId(layer));
                const TERRAIN::SurfacePreset* preset =
                    id.empty() ? nullptr
                        : TERRAIN::SurfaceLayerRegistry::Get().Find(id);
                if (preset != nullptr) {
                    styles[layer] = BuildTerrainLayerRenderStyle(
                        *preset, surface.stylization);
                } else if (layer == 0u) {
                    TERRAIN::SurfacePreset fallback =
                        TERRAIN::MakeSandPreset();
                    fallback.tint.light = surface.material.lightTint;
                    fallback.tint.dark = surface.material.darkTint;
                    styles[layer] = BuildTerrainLayerRenderStyle(
                        fallback, surface.stylization);
                }
            }

            // 生成グリッドは原点中心なので、地形オブジェクトの本来の world
            // 行列に chunk bbox のローカル中心を合成する。
            const MATH::Mat4 objectWorld =
                desc.worldTransform.GetWorldMatrix();
            desc.worldTransform.useExplicitMatrix = true;
            desc.worldTransform.explicitMatrix = objectWorld *
                MATH::Mat4::Translate({
                    originX + extentX * 0.5f,
                    0.0f,
                    originZ + extentZ * 0.5f });

            desc.sandSurfaceHeights = state.heights;
            desc.sandSurfaceLayerWeights = state.layerWeights;
            desc.sandSurfaceCoverage = state.coverage;
            if (responseGridActive) {
                desc.sandSurfaceDeformations = state.deformations;
            }
            desc.sandSurfaceProjectionRevision = state.projectionRevision;

            // authored 高さと response profile から保守 bounds を作り、8m 単位で
            // 量子化して只拡張する。dent tick や通常の brush sample が毎回
            // topology rebuild を起こさず、極値を越えた時だけ安全側へ広がる。
            const float rippleMargin = std::clamp(
                desc.sandSurfaceData.rippleAmount, 0.0f, 3.0f) *
                std::clamp(
                    desc.sandSurfaceData.heightScale, 0.0f, 2.0f) *
                0.021f;
            const float desiredMin = state.authoredMinHeight -
                (std::max)(surface.shoreline.edgeDepth, 0.0f) -
                (std::max)(surface.deform.maxDepth, 0.0f) -
                rippleMargin;
            const float desiredMax = state.authoredMaxHeight +
                (std::max)(surface.deform.maxDepth, 0.0f) *
                    (std::max)(surface.deform.rimStrength, 0.0f) +
                rippleMargin;
            constexpr float kBoundsQuantum = 8.0f;
            const float quantizedMin =
                std::floor(desiredMin / kBoundsQuantum) * kBoundsQuantum;
            const float quantizedMax =
                std::ceil(desiredMax / kBoundsQuantum) * kBoundsQuantum;
            if (!state.stableBoundsValid) {
                state.stableBoundsMinY = quantizedMin;
                state.stableBoundsMaxY = quantizedMax;
                state.stableBoundsValid = true;
            } else {
                state.stableBoundsMinY = (std::min)(
                    state.stableBoundsMinY, quantizedMin);
                state.stableBoundsMaxY = (std::max)(
                    state.stableBoundsMaxY, quantizedMax);
            }
            output.meshSettings = responseSettings;
            output.meshSettings.segmentsX = gridRes;
            output.meshSettings.segmentsY = gridRes;
            output.stableBoundsMinY = state.stableBoundsMinY;
            output.stableBoundsMaxY = state.stableBoundsMaxY;
        }


        RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
            RENDER3D::RUNTIME::SceneRenderObjectId id{ object.GetDocumentId().value };
            if (!id.IsValid()) {
                id.value = reinterpret_cast<uint64_t>(&object);
            }
            return id;
        }

        std::string ResolveClusteredGeometryPath(
            const GameObject& object,
            const ModelComponent& model,
            const AssetRegistry* assetRegistry,
            const std::filesystem::path& projectRoot) {

            if (const auto* procedural =
                    object.GetComponent<ProceduralMeshComponent>()) {
                return PROCEDURAL::GetOrCreateClusteredGeometryPath(
                    procedural->GetSettings(),
                    projectRoot);
            }

            if (assetRegistry == nullptr ||
                model.GetAssetId().empty()) {
                return {};
            }

            const auto* descriptor =
                assetRegistry->FindAs<ModelAssetDescriptor>(AssetId{ model.GetAssetId() });
            if (descriptor == nullptr || descriptor->clusteredGeometryPath.empty()) {
                return {};
            }

            std::filesystem::path path = descriptor->clusteredGeometryPath;
            if (!path.is_absolute() && !projectRoot.empty()) {
                path = projectRoot / path;
            }
            return path.lexically_normal().generic_string();
        }

        Bounds ResolveLocalBounds(
            const ModelAsset* model,
            const RENDER3D::RUNTIME::RenderModelAsset* renderModel) {

            if (renderModel != nullptr && renderModel->valid && BOUNDS::IsUsable(renderModel->localBounds)) {
                return renderModel->localBounds;
            }
            if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
                return model->bounds;
            }
            return {};
        }

        bool HasRuntimeMaterialRevisionChange(
            const RENDER3D::RUNTIME::SceneRenderObjectDesc& cached,
            const ModelComponent& model) {

            size_t liveMaterialCount = 0u;
            for (const RuntimeMaterialOverrideSlot& slot :
                model.GetRuntimeMaterialOverrides()) {

                const Material* material = slot.material.get();
                if (material == nullptr) {
                    continue;
                }
                ++liveMaterialCount;
                const auto found = std::find_if(
                    cached.materialOverrides.begin(),
                    cached.materialOverrides.end(),
                    [&slot](
                        const RENDER3D::RUNTIME::
                            SceneRenderMaterialOverride& cachedSlot) {
                        return cachedSlot.materialIndex == slot.materialIndex;
                    });
                if (found == cached.materialOverrides.end() ||
                    found->material != material ||
                    found->materialRevision != material->GetRevision()) {
                    return true;
                }
            }
            return liveMaterialCount != cached.materialOverrides.size();
        }

        class ClusteredGeometryPathResolver {
        public:
            ClusteredGeometryPathResolver(
                const AssetRegistry* assetRegistry,
                std::filesystem::path projectRoot)
                : assetRegistry_(assetRegistry)
                , projectRoot_(std::move(projectRoot)) {
            }

            // 地形メッシュに合わせて procedural 設定を直接解決する。
            // (terrain は desc.model を差し替えるため、既定の object 経由では
            //  旧平面の clustered 幾何が残ってしまう)
            std::string ResolveProceduralSettings(
                const ProceduralMeshSettings& settings) {
                return PROCEDURAL::GetOrCreateClusteredGeometryPath(
                    settings, projectRoot_);
            }

            const std::filesystem::path& GetProjectRoot() const noexcept {
                return projectRoot_;
            }

            std::string Resolve(
                const GameObject& object,
                const ModelComponent& model) {
                if (object.GetComponent<ProceduralMeshComponent>() != nullptr) {
                    return ResolveClusteredGeometryPath(
                        object, model, assetRegistry_, projectRoot_);
                }
                if (model.GetAssetId().empty()) {
                    return {};
                }
                const std::string& assetId = model.GetAssetId();
                const auto found = pathByAssetId_.find(assetId);
                if (found != pathByAssetId_.end()) {
                    return found->second;
                }

                std::string path = ResolveClusteredGeometryPath(
                    object, model, assetRegistry_, projectRoot_);
                pathByAssetId_.emplace(assetId, path);
                return path;
            }

        private:
            const AssetRegistry* assetRegistry_ = nullptr;
            std::filesystem::path projectRoot_{};
            std::unordered_map<std::string, std::string> pathByAssetId_{};
        };

        RENDER3D::RUNTIME::SceneRenderObjectDesc BuildSceneRenderObjectDesc(
            const GameObject& object,
            ModelComponent& model,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            ClusteredGeometryPathResolver& clusteredGeometryPaths,
            const PresentationTransformService* presentationTransforms,
            const ANIMATION::AnimationPoseService* animationPoses,
            const SAND::SandSurfaceQueryService* sandSurfaceQuery,
            const RuntimeObjectActivityService* runtimeActivityService,
            TerrainProjectionState& terrainProjection,
            SceneRenderCacheSync::DebugStats& syncStats,
            RENDER3D::RenderViewPurpose purpose) {

            RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
            desc.id = ResolveSceneRenderObjectId(object);
            const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
            const bool debugOnly =
                debugMode == ModelRenderDebugMode::WireOnly ||
                debugMode == ModelRenderDebugMode::BoundsOnly;
            const bool lightingBakeCapture =
                purpose ==
                    RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
                purpose ==
                    RENDER3D::RenderViewPurpose::LightProbeCapture;
            desc.visible =
                model.IsVisible() &&
                !debugOnly &&
                (runtimeActivityService == nullptr ||
                    runtimeActivityService->Get(
                        object.GetRuntimeHandle()) !=
                        GameProjectRuntimeActivity::SuspendedHidden) &&
                (!lightingBakeCapture ||
                    model.GetIncludeInLightingBake());

            const ModelAsset* asset =
                ResolveRenderableModelAsset(object, model);
            desc.model = asset;
            if (asset != nullptr) {
                desc.renderModel = renderModelCache.GetOrCreate(*asset);
            }

            desc.worldTransform = object.GetTransform();
            MATH::Mat4 presentationWorld{};
            if (presentationTransforms != nullptr &&
                presentationTransforms->TryGetWorldMatrix(
                    object.GetRuntimeHandle(),
                    presentationWorld)) {
                desc.worldTransform.useExplicitMatrix = true;
                desc.worldTransform.explicitMatrix = presentationWorld;
            }
            desc.localBounds = ResolveLocalBounds(desc.model, desc.renderModel);
            desc.worldBounds = BOUNDS::TransformBounds(
                desc.localBounds,
                desc.worldTransform.GetWorldMatrix());

            desc.isStatic = model.IsRenderStatic();
            desc.clusteredGeometryPath = clusteredGeometryPaths.Resolve(
                object, model);
            desc.castShadow = model.GetCastShadow();
            desc.receiveShadow = model.GetReceiveShadow();
            if (animationPoses != nullptr) {
                desc.animationPose = animationPoses->Find(
                    object.GetRuntimeHandle());
                desc.hasRuntimeAnimation =
                    desc.animationPose != nullptr &&
                    desc.animationPose->valid &&
                    desc.animationPose->localPose.IsValidFor(
                        asset != nullptr
                            ? asset->nodes.size()
                            : 0u);
                desc.animationPoseRevision =
                    desc.animationPose != nullptr
                        ? desc.animationPose->revision
                        : 0u;
            }
            desc.hasSpecialRenderDebug = false;
            desc.allowStaticCachedForward =
                !desc.hasRuntimeAnimation;
            for (const RuntimeMaterialOverrideSlot& slot :
                model.GetRuntimeMaterialOverrides()) {

                const Material* runtimeMaterial = slot.material.get();
                if (runtimeMaterial == nullptr) {
                    continue;
                }
                desc.materialOverrides.push_back(
                    RENDER3D::RUNTIME::SceneRenderMaterialOverride{
                        slot.materialIndex,
                        runtimeMaterial,
                        runtimeMaterial->GetRevision()
                    });
            }
            std::sort(
                desc.materialOverrides.begin(),
                desc.materialOverrides.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.materialIndex < rhs.materialIndex;
                });
            desc.postGroupMask = model.GetPostGroupMask();

            const auto* procedural =
                object.GetComponent<ProceduralMeshComponent>();
            const auto* terrainSurface =
                object.GetComponent<TerrainSurfaceComponent>();
            if (terrainSurface != nullptr && procedural != nullptr) {
                const ProceduralMeshKind meshKind =
                    procedural->GetSettings().kind;
                if (meshKind == ProceduralMeshKind::Plane ||
                    meshKind == ProceduralMeshKind::GridPlane) {
                    const TERRAIN::TerrainSurface& surface =
                        terrainSurface->GetSurface();
                    if (!surface.chunks.empty()) {
                        const float chunkSize = terrainSurface->GetChunkWorldSize();
                        const uint32_t cells = terrainSurface->GetChunkCells();

                        desc.sandSurface = true;
                        desc.sandSurfaceData = BuildSandSurfaceRenderData(
                            TERRAIN::ToSandSurfaceSettings(surface), *procedural);
                        TerrainProjectionOutput projectionOutput{};
                        BakeTerrainChunksForRender(
                            surface, *terrainSurface,
                            object.GetRuntimeHandle(),
                            sandSurfaceQuery,
                            clusteredGeometryPaths.GetProjectRoot(),
                            chunkSize, cells,
                            terrainProjection,
                            syncStats,
                            projectionOutput,
                            desc);
                        const ProceduralMeshSettings& terrainSettings =
                            projectionOutput.meshSettings;
                        const ModelAsset* terrainModel =
                            projectionOutput.warmedModel != nullptr
                                ? projectionOutput.warmedModel
                                : PROCEDURAL::GetOrCreateModel(
                                    terrainSettings);
                        if (terrainModel != nullptr) {
                            desc.model = terrainModel;
                            desc.renderModel =
                                renderModelCache.GetOrCreate(*terrainModel);
                            desc.localBounds = terrainModel->bounds;
                            desc.localBounds.min.y = (std::min)(
                                desc.localBounds.min.y,
                                projectionOutput.stableBoundsMinY);
                            desc.localBounds.max.y = (std::max)(
                                desc.localBounds.max.y,
                                projectionOutput.stableBoundsMaxY);
                            desc.worldBounds = BOUNDS::TransformBounds(
                                desc.localBounds,
                                desc.worldTransform.GetWorldMatrix());
                            // CPU と meshlet の両経路で同じ手続き型モデルを使う。
                            desc.clusteredGeometryPath =
                                projectionOutput.warmedClusteredGeometryPath.empty()
                                    ? clusteredGeometryPaths.
                                        ResolveProceduralSettings(
                                            terrainSettings)
                                    : projectionOutput.
                                        warmedClusteredGeometryPath;
                        }
                    }
                }
            }

            const auto* water =
                object.GetComponent<WaterSurfaceComponent>();
            if (water != nullptr && procedural != nullptr) {
                const ProceduralMeshKind meshKind =
                    procedural->GetSettings().kind;
                if (meshKind == ProceduralMeshKind::Plane ||
                    meshKind == ProceduralMeshKind::GridPlane) {
                    desc.waterSurface = true;
                    desc.waterSurfaceData = BuildWaterSurfaceRenderData(
                        water->GetSettings(),
                        *procedural,
                        desc.worldTransform.GetWorldMatrix());
                }
            }

            if (!desc.waterSurface) {
                if (const MaterialFxComponent* materialFx =
                        object.GetComponent<MaterialFxComponent>()) {

                    desc.materialFxProfileId =
                        materialFx->GetProfileId();
                    desc.materialFxValuesInitialized =
                        materialFx->AreValuesInitialized();
                    for (size_t i = 0;
                        i < VFX::kMaterialFxUserCount;
                        ++i) {

                        desc.materialFxParamValues[i] =
                            materialFx->GetParamValues()[i];
                    }
                }
            }
            return desc;
        }
    }

    struct SceneRenderCacheSync::State {
        DebugStats debugStats{};
        std::unordered_map<
            const RENDER3D::RUNTIME::SceneRenderCache*,
            std::unordered_map<uint64_t, TerrainProjectionState>>
            terrainProjections{};
        std::unordered_map<
            const RENDER3D::RUNTIME::SceneRenderCache*,
            TERRAIN::TerrainGrassRenderProjectionState>
            terrainGrassProjections{};
        std::unordered_set<std::string>
            presentationMissingAssetsLogged{};
        std::unordered_set<std::string>
            presentationAttachmentDiagnosticsLogged{};
        std::unordered_set<std::string>
            presentationMaterialDiagnosticsLogged{};
        std::unordered_map<
            const RENDER3D::RUNTIME::SceneRenderCache*,
            PRESENTATION_MODEL::MaterialStorageByRenderObject>
            presentationMaterialOverrides{};
    };

    SceneRenderCacheSync::SceneRenderCacheSync()
        : state_(std::make_unique<State>()) {
    }

    SceneRenderCacheSync::~SceneRenderCacheSync() = default;

    void SceneRenderCacheSync::Clear() noexcept {
        state_->terrainProjections.clear();
        state_->terrainGrassProjections.clear();
        state_->presentationMissingAssetsLogged.clear();
        state_->presentationAttachmentDiagnosticsLogged.clear();
        state_->presentationMaterialDiagnosticsLogged.clear();
        state_->presentationMaterialOverrides.clear();
    }

    const SceneRenderCacheSync::DebugStats&
        SceneRenderCacheSync::GetDebugStats() const noexcept {

        return state_->debugStats;
    }

    void SceneRenderCacheSync::ResetDebugStats() noexcept {
        state_->debugStats = {};
    }

    void SceneRenderCacheSync::Sync(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose,
        ModelManager* modelManager) {

        PROCEDURAL::PumpClusteredGeometryWarmUps();
        sceneRenderCache.BeginSync(frameIndex);
        auto& presentationMaterials =
            state_->presentationMaterialOverrides[&sceneRenderCache];
        auto& terrainProjections =
            state_->terrainProjections[&sceneRenderCache];
        terrainProjections.clear();
        auto& terrainGrassProjections =
            state_->terrainGrassProjections[&sceneRenderCache];
        terrainGrassProjections.renderObjectIdsByOwner.clear();
        terrainGrassProjections.activeRenderInstanceCount = 0u;
        ClusteredGeometryPathResolver clusteredGeometryPaths(
            assetRegistry,
            std::move(projectRoot));
        const PresentationTransformService* presentationTransforms =
            world.Services().Find<PresentationTransformService>();
        const ANIMATION::AnimationPoseService* animationPoses =
            world.Services().Find<ANIMATION::AnimationPoseService>();
        const SAND::SandSurfaceQueryService* sandSurfaceQuery =
            world.Services().Find<SAND::SandSurfaceQueryService>();
        const RuntimeObjectActivityService* runtimeActivityService =
            world.Services().Find<RuntimeObjectActivityService>();

        std::vector<uint64_t> occupiedObjectIds{};
        occupiedObjectIds.reserve(world.GetObjects().size());
        for (const auto& object : world.GetObjects()) {
            if (object != nullptr) {
                occupiedObjectIds.push_back(object->GetRenderStableId());
            }
        }
        PresentationModelService* presentationModels = world.Services().Find<
            PresentationModelService>();
        if (presentationModels != nullptr) {
            presentationModels->PrepareForRender(
                frameIndex,
                occupiedObjectIds);
            for (const uint64_t removedId :
                    presentationModels->GetRemovedRenderObjectIds()) {
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{ removedId });
                presentationMaterials.erase(removedId);
            }
        }

        world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
            // World 蜀肴ｧ狗ｯ峨�ｮ繧ｿ繧､繝溘Φ繧ｰ縺ｫ萓晏ｭ倥○縺壹∵緒逕ｻ蜷梧悄縺ｮ縺溘�ｳ縺ｫ
            // 豐吝慍繝槭ユ繝ｪ繧｢繝ｫ繧剃ｿ晁ｨｼ縺吶ｋ縲�Ensure 縺ｯ縺吶〒縺ｫ驕ｩ逕ｨ貂医∩縺ｪ繧�
            // 菴輔ｂ縺帙★縲√ヵ繝ｬ繝ｼ繝豈弱�ｮ蜀阪い繝�繝励Ο繝ｼ繝峨ｒ驕ｿ縺代ｋ縲�
            if (assetRegistry != nullptr &&
                object.GetComponent<TerrainSurfaceComponent>() != nullptr) {
                (void)SAND::EnsureSandSurfaceMaterialOverride(
                    model,
                    *assetRegistry);
            }
            const RENDER3D::RUNTIME::SceneRenderObjectId objectId =
                ResolveSceneRenderObjectId(object);
            TerrainProjectionState& terrainProjection =
                terrainProjections[objectId.value];
            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                object,
                model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                sandSurfaceQuery,
                runtimeActivityService,
                terrainProjection,
                state_->debugStats,
                purpose));
            if (const TerrainSurfaceComponent* terrain =
                    object.GetComponent<TerrainSurfaceComponent>()) {
                TERRAIN::TerrainGrassRenderProjectionStats grassStats{};
                TERRAIN::SyncTerrainGrassRenderObjects(
                    object,
                    model,
                    *terrain,
                    renderModelCache,
                    clusteredGeometryPaths.GetProjectRoot(),
                    presentationTransforms,
                    terrainGrassProjections,
                    sceneRenderCache,
                    purpose,
                    grassStats);
                state_->debugStats.terrainGrassRenderInstanceCount +=
                    grassStats.renderedCount;
                state_->debugStats.terrainGrassRenderSkippedCount +=
                    grassStats.skippedCount;
            }
        });

        PRESENTATION_MODEL::SyncRenderObjects(
            world,
            presentationModels,
            renderModelCache,
            sceneRenderCache,
            assetRegistry,
            modelManager,
            clusteredGeometryPaths.GetProjectRoot(),
            presentationTransforms,
            animationPoses,
            presentationMaterials,
            state_->presentationMissingAssetsLogged,
            state_->presentationAttachmentDiagnosticsLogged,
            state_->presentationMaterialDiagnosticsLogged);

        sceneRenderCache.EndSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeAllRenderObjects();
        if (presentationModels != nullptr) {
            presentationModels->AcknowledgeRenderFrame();
        }
    }

    void SceneRenderCacheSync::SyncDirty(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose,
        ModelManager* modelManager) {

        PROCEDURAL::PumpClusteredGeometryWarmUps();
        sceneRenderCache.BeginPatchSync(frameIndex);
        auto& presentationMaterials =
            state_->presentationMaterialOverrides[&sceneRenderCache];
        auto& terrainProjections =
            state_->terrainProjections[&sceneRenderCache];
        auto& terrainGrassProjections =
            state_->terrainGrassProjections[&sceneRenderCache];
        for (const uint64_t removedObjectId : world.GetRemovedRenderObjectIds()) {
            sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{ removedObjectId });
            terrainProjections.erase(removedObjectId);
            TERRAIN::RemoveTerrainGrassRenderObjects(
                sceneRenderCache,
                terrainGrassProjections,
                removedObjectId);
        }

        ClusteredGeometryPathResolver clusteredGeometryPaths(
            assetRegistry,
            std::move(projectRoot));
        const PresentationTransformService* presentationTransforms =
            world.Services().Find<PresentationTransformService>();
        const ANIMATION::AnimationPoseService* animationPoses =
            world.Services().Find<ANIMATION::AnimationPoseService>();
        const SAND::SandSurfaceQueryService* sandSurfaceQuery =
            world.Services().Find<SAND::SandSurfaceQueryService>();
        const RuntimeObjectActivityService* runtimeActivityService =
            world.Services().Find<RuntimeObjectActivityService>();

        std::vector<uint64_t> occupiedObjectIds{};
        occupiedObjectIds.reserve(world.GetObjects().size());
        for (const auto& object : world.GetObjects()) {
            if (object != nullptr) {
                occupiedObjectIds.push_back(object->GetRenderStableId());
            }
        }
        PresentationModelService* presentationModels = world.Services().Find<
            PresentationModelService>();
        if (presentationModels != nullptr) {
            presentationModels->PrepareForRender(
                frameIndex,
                occupiedObjectIds);
            for (const uint64_t removedId :
                    presentationModels->GetRemovedRenderObjectIds()) {
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{ removedId });
                presentationMaterials.erase(removedId);
            }
        }

        // built-in material の公開 revision は必ずしも GameObject を dirty にしない。
        // patch 前に live な terrain owner を再 projection し、置換前の runtime
        // Material pointer を保持する descriptor を残さない。
        for (GameObject* object : world.GetRenderDirtyObjects()) {
            if (object == nullptr) {
                continue;
            }

            const uint64_t ownerStableId = object->GetRenderStableId();
            TERRAIN::RemoveTerrainGrassRenderObjects(
                sceneRenderCache,
                terrainGrassProjections,
                ownerStableId);

            ModelComponent* model = object->GetComponent<ModelComponent>();
            if (model == nullptr) {
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{ object->GetRenderStableId() });
                terrainProjections.erase(object->GetRenderStableId());
                continue;
            }

            // Sync と同様に、再構築した地形メッシュへ沙地マテリアルを
            // 保証する。これを欠くと incremental 更新で無地になってしまう。
            if (assetRegistry != nullptr &&
                object->GetComponent<TerrainSurfaceComponent>() != nullptr) {
                (void)SAND::EnsureSandSurfaceMaterialOverride(
                    *model, *assetRegistry);
            }

            const RENDER3D::RUNTIME::SceneRenderObjectId objectId =
                ResolveSceneRenderObjectId(*object);
            TerrainProjectionState& terrainProjection =
                terrainProjections[objectId.value];
            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                *object,
                *model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                sandSurfaceQuery,
                runtimeActivityService,
                terrainProjection,
                state_->debugStats,
                purpose));
            if (const TerrainSurfaceComponent* terrain =
                    object->GetComponent<TerrainSurfaceComponent>()) {
                TERRAIN::TerrainGrassRenderProjectionStats grassStats{};
                TERRAIN::SyncTerrainGrassRenderObjects(
                    *object,
                    *model,
                    *terrain,
                    renderModelCache,
                    clusteredGeometryPaths.GetProjectRoot(),
                    presentationTransforms,
                    terrainGrassProjections,
                    sceneRenderCache,
                    purpose,
                    grassStats);
                state_->debugStats.terrainGrassRenderInstanceCount +=
                    grassStats.renderedCount;
                state_->debugStats.terrainGrassRenderSkippedCount +=
                    grassStats.skippedCount;
            }
        }

        PRESENTATION_MODEL::SyncRenderObjects(
            world,
            presentationModels,
            renderModelCache,
            sceneRenderCache,
            assetRegistry,
            modelManager,
            clusteredGeometryPaths.GetProjectRoot(),
            presentationTransforms,
            animationPoses,
            presentationMaterials,
            state_->presentationMissingAssetsLogged,
            state_->presentationAttachmentDiagnosticsLogged,
            state_->presentationMaterialDiagnosticsLogged);

        // Material asset は既存 runtime Material object へ publish する。
        // revision の変化だけでは owning GameObject は dirty にならないため、
        // この frame の SceneRenderCache snapshot が authoritative GPU Scene source
        // になる前に明示的に検出する。
        world.ForEachObjectWith<ModelComponent>(
            [&](GameObject& object, ModelComponent& model) {
                const RENDER3D::RUNTIME::SceneRenderObjectId objectId =
                    ResolveSceneRenderObjectId(object);
                const RENDER3D::RUNTIME::SceneRenderObject* cached =
                    sceneRenderCache.Find(objectId);
                TerrainProjectionState& terrainProjection =
                    terrainProjections[objectId.value];
                bool responseWarmReady = false;
                if (terrainProjection.warmPending &&
                    terrainProjection.warmSettingsValid) {

                    const ModelAsset* warmedModel = nullptr;
                    std::string warmedPath{};
                    responseWarmReady =
                        PROCEDURAL::TryGetWarmedClusteredGeometry(
                            terrainProjection.warmSettings,
                            terrainProjection.warmProjectRoot,
                            warmedModel,
                            warmedPath);
                }
                if (cached != nullptr &&
                    !HasRuntimeMaterialRevisionChange(cached->desc, model) &&
                    !responseWarmReady) {
                    return;
                }
                sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                    object,
                    model,
                    renderModelCache,
                    clusteredGeometryPaths,
                    presentationTransforms,
                            animationPoses,
                            sandSurfaceQuery,
                            runtimeActivityService,
                            terrainProjection,
                    state_->debugStats,
                    purpose));
            });

        sceneRenderCache.EndPatchSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeRenderDirtyObjects();
        if (presentationModels != nullptr) {
            presentationModels->AcknowledgeRenderFrame();
        }
    }

}
