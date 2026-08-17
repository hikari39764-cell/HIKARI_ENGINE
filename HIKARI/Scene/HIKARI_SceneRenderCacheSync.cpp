#include "Scene/HIKARI_SceneRenderCacheSync.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"
#include "Render3D/Sand/HIKARI_SandSurfaceRenderData.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceMaterialBinder.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"


namespace HIKARI {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        RENDER3D::WATER::WaterSurfaceRenderData
            BuildWaterSurfaceRenderData(
                const WATER::WaterSurfaceSettings& authoredSettings,
                const ProceduralMeshComponent& procedural) {

            const ProceduralMeshSettings mesh =
                SanitizeProceduralMeshSettings(
                    procedural.GetSettings());
            const float spacingX = mesh.width /
                static_cast<float>((std::max)(mesh.segmentsX, 1u));
            const float spacingZ = mesh.depth /
                static_cast<float>((std::max)(mesh.segmentsY, 1u));
            const WATER::WaterSurfaceSettings settings =
                WATER::ResolveWaterSurfaceSettingsForMesh(
                    authoredSettings,
                    (std::max)(spacingX, spacingZ));

            RENDER3D::WATER::WaterSurfaceRenderData data{};
            data.shadingModel = settings.preset ==
                    WATER::WaterSurfacePreset::RealisticClearSummer
                ? RENDER3D::WATER::WaterSurfaceShadingModel::
                    RealisticClearSummer
                : RENDER3D::WATER::WaterSurfaceShadingModel::AnimeSummer;
            data.timeScale = settings.timeScale;
            data.choppiness = settings.choppiness;
            data.microWaveStrength = settings.microWaveStrength;
            data.depthTransmittance = settings.depthTransmittance;
            data.scatteringColor = settings.scatteringColor;
            data.foamColor = settings.foamColor;
            data.foamAmount = settings.foamAmount;
            data.absorptionDistance = settings.absorptionDistance;
            data.surfaceRoughness = settings.surfaceRoughness;
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
            return data;
        }

        // Rebuild a 1:1 vertex grid whose segments equal the baked height-field
        // resolution and whose extent equals the chunk bounding box. uv0 tiles the
        // sand-grain texture once per grid cell (same density as the old
        // GridPlane fx*segments) so the material keeps its anime look; uv1 stays
        // normalized [0,1] because the SAND-only pixel shader reads coverage and
        // layer weights from uv1 and height from localPosition/extents, so a tiled
        // uv0 never reaches the coverage sampler and cannot cause a discard.
        const ModelAsset* RebuildTerrainMesh(
            uint64_t objectId,
            uint32_t gridX,
            uint32_t gridZ,
            float extentX,
            float extentZ,
            float cellPitch,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache) {

            struct TerrainMeshEntry {
                std::unique_ptr<ModelAsset> model{};
                const RENDER3D::RUNTIME::RenderModelAsset* renderModel = nullptr;
                uint32_t gridX = 0u;
                uint32_t gridZ = 0u;
                float extentX = 0.0f;
                float extentZ = 0.0f;
                float cellPitch = 0.0f;
            };
            static std::unordered_map<uint64_t, TerrainMeshEntry> sTerrainMeshes{};

            TerrainMeshEntry& entry = sTerrainMeshes[objectId];
            if (entry.renderModel != nullptr &&
                entry.gridX == gridX && entry.gridZ == gridZ &&
                entry.extentX == extentX && entry.extentZ == extentZ &&
                entry.cellPitch == cellPitch) {
                return entry.model.get();
            }

            auto model = std::make_unique<ModelAsset>();
            model->SetName("terrain-grid");
            model->SetSourcePath("procedural://terrain-grid");
            model->SetState(ModelAsset::State::Loaded);

            MaterialAsset material{};
            material.name = "Terrain Default";
            material.slotId = ASSETS::MODELS::MakeMaterialSlotId(
                ASSETS::MODELS::MaterialSlotImporterSemantic::Procedural,
                material.name,
                0u);
            material.shaderProfileId = "PBR";
            material.doubleSided = true;
            model->materials.push_back(material);

            MeshPrimitive primitive{};
            primitive.name = "Terrain Grid";
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = 0;
            primitive.staticVertices.reserve(
                static_cast<std::size_t>(gridX + 1u) * (gridZ + 1u));
            Bounds bounds{};
            bool boundsInit = false;
            const float cellsAcrossX = extentX / (std::max)(cellPitch, 1.0e-4f);
            const float cellsAcrossZ = extentZ / (std::max)(cellPitch, 1.0e-4f);
            for (uint32_t gz = 0u; gz <= gridZ; ++gz) {
                const float fz = static_cast<float>(gz) / static_cast<float>(gridZ);
                for (uint32_t gx = 0u; gx <= gridX; ++gx) {
                    const float fx = static_cast<float>(gx) / static_cast<float>(gridX);
                    Vertex3D v{};
                    v.position = {
                        -extentX * 0.5f + extentX * fx,
                        0.0f,
                        -extentZ * 0.5f + extentZ * fz
                    };
                    v.normal = { 0.0f, 1.0f, 0.0f };
                    v.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                    v.uv0 = { fx * cellsAcrossX, fz * cellsAcrossZ };
                    v.uv1 = { fx, fz };
                    primitive.staticVertices.push_back(v);
                    if (!boundsInit) {
                        bounds.min = v.position;
                        bounds.max = v.position;
                        boundsInit = true;
                    } else {
                        bounds.min.x = (std::min)(bounds.min.x, v.position.x);
                        bounds.min.y = (std::min)(bounds.min.y, v.position.y);
                        bounds.min.z = (std::min)(bounds.min.z, v.position.z);
                        bounds.max.x = (std::max)(bounds.max.x, v.position.x);
                        bounds.max.y = (std::max)(bounds.max.y, v.position.y);
                        bounds.max.z = (std::max)(bounds.max.z, v.position.z);
                    }
                }
            }
            primitive.indices.reserve(static_cast<std::size_t>(gridX) * gridZ * 6u);
            for (uint32_t gz = 0u; gz < gridZ; ++gz) {
                for (uint32_t gx = 0u; gx < gridX; ++gx) {
                    const uint32_t row0 = gz * (gridX + 1u);
                    const uint32_t row1 = (gz + 1u) * (gridX + 1u);
                    const uint32_t i0 = row0 + gx;
                    const uint32_t i1 = row0 + gx + 1u;
                    const uint32_t i2 = row1 + gx;
                    const uint32_t i3 = row1 + gx + 1u;
                    primitive.indices.insert(
                        primitive.indices.end(), { i0, i2, i1, i1, i2, i3 });
                }
            }
            primitive.bounds = bounds;

            MeshAsset mesh{};
            mesh.name = "Terrain Mesh";
            mesh.bounds = bounds;
            mesh.primitives.push_back(std::move(primitive));
            model->meshes.push_back(std::move(mesh));
            model->bounds = bounds;

            ModelNode node{};
            node.name = "Terrain Root";
            node.meshIndex = 0;
            model->nodes.push_back(node);
            model->defaultSceneRootNode = 0;

            const RENDER3D::RUNTIME::RenderModelAsset* renderModel =
                renderModelCache.GetOrCreate(*model);
            entry.model = std::move(model);
            entry.renderModel = renderModel;
            entry.gridX = gridX;
            entry.gridZ = gridZ;
            entry.extentX = extentX;
            entry.extentZ = extentZ;
            entry.cellPitch = cellPitch;
            return renderModel != nullptr ? entry.model.get() : nullptr;
        }

        // Bake the sparse chunk set into one square (gridRes+1)^2 height / coverage /
        // layer-weight field. Positions outside every chunk keep coverage 0 so the
        // pixel shader discards them (real holes), which is the only intended source
        // of transparency. Buffers are persistent per object because Upsert copies the
        // desc but the renderer keeps reading sandSurfaceHeights until the next bake.
        void BakeTerrainChunksForRender(
            const TERRAIN::TerrainSurface& surface,
            const TerrainSurfaceComponent& terrain,
            uint64_t objectId,
            RuntimeObjectHandle objectHandle,
            const SAND::SandSurfaceQueryService* sandSurfaceQuery,
            float chunkSize,
            uint32_t cells,
            float cellSize,
            RENDER3D::RUNTIME::SceneRenderObjectDesc& desc,
            uint32_t* outTotalCellsX = nullptr,
            uint32_t* outTotalCellsZ = nullptr) {

            const TERRAIN::TerrainChunkMap& chunks = surface.chunks;
            if (chunks.empty()) {
                desc.sandSurface = false;
                return;
            }

            int32_t minKeyX = (std::numeric_limits<int32_t>::max)();
            int32_t minKeyZ = (std::numeric_limits<int32_t>::max)();
            int32_t maxKeyX = (std::numeric_limits<int32_t>::min)();
            int32_t maxKeyZ = (std::numeric_limits<int32_t>::min)();
            for (const auto& [key, chunk] : chunks) {
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
            if (outTotalCellsX != nullptr) { *outTotalCellsX = totalCellsX; }
            if (outTotalCellsZ != nullptr) { *outTotalCellsZ = totalCellsZ; }

            // The GPU height field is a single square gridResolution x gridResolution
            // (capped at 512). A non-square chunk span is resampled isotropically into
            // that square; uv1 = local/extents+0.5 stays the exact 1:1 mapping.
            constexpr uint32_t kMaxGrid = 512u;
            const uint32_t gridRes = (std::max)(1u, (std::min)(
                (std::max)(totalCellsX, totalCellsZ), kMaxGrid));
            const uint32_t stride = gridRes + 1u;
            const std::size_t gridCount = static_cast<std::size_t>(stride) * stride;

            uint32_t layerCount = 1u;
            for (const auto& [key, chunk] : chunks) {
                layerCount = (std::max)(layerCount, chunk.layerCount);
            }

            struct BakedTerrainBuffers {
                std::vector<float> heights{};
                std::vector<float> coverage{};
                std::vector<float> layerWeights{};
            };
            static std::unordered_map<uint64_t, BakedTerrainBuffers> sBakedByObject{};
            BakedTerrainBuffers& baked = sBakedByObject[objectId];
            std::vector<float>& bakedHeights = baked.heights;
            std::vector<float>& bakedCoverage = baked.coverage;
            std::vector<float>& bakedLayerWeights = baked.layerWeights;
            bakedHeights.assign(gridCount, 0.0f);
            bakedCoverage.assign(gridCount, 0.0f);
            bakedLayerWeights.assign(
                gridCount * static_cast<std::size_t>(layerCount), 0.0f);

            const float gridCellX = extentX / static_cast<float>(gridRes);
            const float gridCellZ = extentZ / static_cast<float>(gridRes);
            const float originX = static_cast<float>(minKeyX) * chunkSize;
            const float originZ = static_cast<float>(minKeyZ) * chunkSize;
            const uint32_t chunkStride = cells + 1u;
            const std::size_t chunkVertCount =
                static_cast<std::size_t>(chunkStride) * chunkStride;

            // Resolve the live (base+deform) heights per chunk when the runtime
            // simulation has them, so ball imprints show through. Otherwise bake the
            // authored base heights.
            std::map<TERRAIN::TerrainChunkKey, const std::vector<float>*> resolvedHeights{};
            for (const auto& [key, chunk] : chunks) {
                const SAND::SandChunkRuntimeState* runtime = nullptr;
                const std::vector<float>* src = &chunk.heights;
                if (sandSurfaceQuery != nullptr &&
                    sandSurfaceQuery->TryGetChunkState(objectHandle, key, runtime) &&
                    runtime != nullptr &&
                    runtime->heights.size() == chunkVertCount) {
                    src = &runtime->heights;
                }
                if (src->size() == chunkVertCount) {
                    resolvedHeights.emplace(key, src);
                }
            }

            const auto sampleChunkCoverage =
                [&](const TERRAIN::TerrainChunk& chunk, float localX, float localZ) {
                    const float fx = std::clamp(localX / cellSize, 0.0f, static_cast<float>(cells));
                    const float fz = std::clamp(localZ / cellSize, 0.0f, static_cast<float>(cells));
                    const uint32_t x0 = static_cast<uint32_t>(fx);
                    const uint32_t z0 = static_cast<uint32_t>(fz);
                    const uint32_t x1 = (std::min)(x0 + 1u, cells);
                    const uint32_t z1 = (std::min)(z0 + 1u, cells);
                    const float tx = fx - static_cast<float>(x0);
                    const float tz = fz - static_cast<float>(z0);
                    const auto C = [&](uint32_t xx, uint32_t zz) {
                        return chunk.coverage[static_cast<std::size_t>(zz) * chunkStride + xx];
                    };
                    return std::lerp(
                        std::lerp(C(x0, z0), C(x1, z0), tx),
                        std::lerp(C(x0, z1), C(x1, z1), tx), tz);
                };
            const auto sampleChunkHeight =
                [&](const std::vector<float>& heights, float localX, float localZ) {
                    const float fx = std::clamp(localX / cellSize, 0.0f, static_cast<float>(cells));
                    const float fz = std::clamp(localZ / cellSize, 0.0f, static_cast<float>(cells));
                    const uint32_t x0 = static_cast<uint32_t>(fx);
                    const uint32_t z0 = static_cast<uint32_t>(fz);
                    const uint32_t x1 = (std::min)(x0 + 1u, cells);
                    const uint32_t z1 = (std::min)(z0 + 1u, cells);
                    const float tx = fx - static_cast<float>(x0);
                    const float tz = fz - static_cast<float>(z0);
                    const auto H = [&](uint32_t xx, uint32_t zz) {
                        return heights[static_cast<std::size_t>(zz) * chunkStride + xx];
                    };
                    return std::lerp(
                        std::lerp(H(x0, z0), H(x1, z0), tx),
                        std::lerp(H(x0, z1), H(x1, z1), tx), tz);
                };

            for (uint32_t gz = 0u; gz <= gridRes; ++gz) {
                const float wz = originZ + static_cast<float>(gz) * gridCellZ;
                for (uint32_t gx = 0u; gx <= gridRes; ++gx) {
                    const float wx = originX + static_cast<float>(gx) * gridCellX;
                    const std::size_t dst = static_cast<std::size_t>(gz) * stride + gx;
                    const TERRAIN::TerrainChunkKey key =
                        TERRAIN::TerrainChunkKeyFromWorld(wx, wz, chunkSize);
                    const auto cit = chunks.find(key);
                    if (cit == chunks.end()) {
                        continue; // no chunk -> coverage stays 0 -> discard (real hole)
                    }
                    const TERRAIN::TerrainChunk& chunk = cit->second;
                    const float localX = wx - static_cast<float>(key.x) * chunkSize;
                    const float localZ = wz - static_cast<float>(key.z) * chunkSize;
                    const float cov = sampleChunkCoverage(chunk, localX, localZ);
                    bakedCoverage[dst] = cov;
                    if (cov <= 1.0e-4f) {
                        continue;
                    }
                    const auto hit = resolvedHeights.find(key);
                    if (hit == resolvedHeights.end()) {
                        continue;
                    }
                    bakedHeights[dst] = sampleChunkHeight(*hit->second, localX, localZ);
                    for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                        const std::size_t lsrc =
                            static_cast<std::size_t>(layer) * chunkVertCount;
                        if (lsrc >= chunk.layerWeights.size()) { break; }
                        const float fx = std::clamp(localX / cellSize, 0.0f, static_cast<float>(cells));
                        const float fz = std::clamp(localZ / cellSize, 0.0f, static_cast<float>(cells));
                        const uint32_t x0 = static_cast<uint32_t>(fx);
                        const uint32_t z0 = static_cast<uint32_t>(fz);
                        const uint32_t x1 = (std::min)(x0 + 1u, cells);
                        const uint32_t z1 = (std::min)(z0 + 1u, cells);
                        const float tx = fx - static_cast<float>(x0);
                        const float tz = fz - static_cast<float>(z0);
                        const auto W = [&](uint32_t xx, uint32_t zz) {
                            return chunk.layerWeights[lsrc + static_cast<std::size_t>(zz) * chunkStride + xx];
                        };
                        bakedLayerWeights[static_cast<std::size_t>(layer) * gridCount + dst] =
                            std::lerp(
                                std::lerp(W(x0, z0), W(x1, z0), tx),
                                std::lerp(W(x0, z1), W(x1, z1), tx), tz);
                    }
                }
            }

            desc.sandSurfaceData.gridResolution = gridRes;
            desc.sandSurfaceData.extents = { extentX, extentZ };
            desc.sandSurfaceData.layerCount = layerCount;
            desc.sandSurfaceData.layerWeights = bakedLayerWeights;
            desc.sandSurfaceData.coverage = bakedCoverage;
            desc.sandSurfaceData.wetness = surface.shoreline.wetness;

            // Resolve per-layer tints from the registry; layers without a preset fall
            // back to the surface material tints so the built-in sand look is kept.
            TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
            auto& lights = desc.sandSurfaceData.layerLightTints;
            auto& darks = desc.sandSurfaceData.layerDarkTints;
            lights.assign(layerCount, surface.material.lightTint);
            darks.assign(layerCount, surface.material.darkTint);
            for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                const std::string id =
                    layer < surface.layerPresetIds.size()
                        ? surface.layerPresetIds[layer] : std::string{};
                const TERRAIN::SurfacePreset* preset =
                    id.empty() ? nullptr
                        : TERRAIN::SurfaceLayerRegistry::Get().Find(id);
                if (preset != nullptr) {
                    lights[layer] = preset->tint.light;
                    darks[layer] = preset->tint.dark;
                }
            }

            // Recenter the transform onto the middle of the chunk bounding box; the
            // local plane is centered, so the world origin must move with the span.
            const float centerX =
                (static_cast<float>(minKeyX) * chunkSize) + extentX * 0.5f;
            const float centerZ =
                (static_cast<float>(minKeyZ) * chunkSize) + extentZ * 0.5f;
            desc.worldTransform.position = {
                centerX, desc.worldTransform.position.y, centerZ };
            desc.worldTransform.useExplicitMatrix = false;

            desc.sandSurfaceHeights = &bakedHeights;
            (void)terrain;
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
                        const float cellSize =
                            chunkSize / static_cast<float>((std::max)(cells, 1u));

                        desc.sandSurface = true;
                        desc.sandSurfaceData = BuildSandSurfaceRenderData(
                            TERRAIN::ToSandSurfaceSettings(surface), *procedural);
                        uint32_t totalCellsX = cells;
                        uint32_t totalCellsZ = cells;
                        BakeTerrainChunksForRender(
                            surface, *terrainSurface, desc.id.value,
                            object.GetRuntimeHandle(), sandSurfaceQuery,
                            chunkSize, cells, cellSize, desc,
                            &totalCellsX, &totalCellsZ);
                        const float ex = desc.sandSurfaceData.extents.x;
                        const float ez = desc.sandSurfaceData.extents.y;
                        const uint32_t gridRes = desc.sandSurfaceData.gridResolution;
                        if (const ModelAsset* terrainModel =
                                RebuildTerrainMesh(
                                    desc.id.value, gridRes, gridRes, ex, ez,
                                    cellSize, renderModelCache)) {
                            desc.model = terrainModel;
                            desc.renderModel =
                                renderModelCache.GetOrCreate(*terrainModel);
                            desc.localBounds = terrainModel->bounds;
                            desc.worldBounds = BOUNDS::TransformBounds(
                                desc.localBounds,
                                desc.worldTransform.GetWorldMatrix());
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
                        water->GetSettings(), *procedural);
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

    void SceneRenderCacheSync::Sync(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose) {

        sceneRenderCache.BeginSync(frameIndex);
        ClusteredGeometryPathResolver clusteredGeometryPaths(
            assetRegistry,
            std::move(projectRoot));
        const PresentationTransformService* presentationTransforms =
            world.Services().Find<PresentationTransformService>();
        const ANIMATION::AnimationPoseService* animationPoses =
            world.Services().Find<ANIMATION::AnimationPoseService>();
        const SAND::SandSurfaceQueryService* sandSurfaceQuery =
            world.Services().Find<SAND::SandSurfaceQueryService>();

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
            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                object,
                model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                sandSurfaceQuery,
                purpose));
        });

        sceneRenderCache.EndSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeAllRenderObjects();
    }

    void SceneRenderCacheSync::SyncDirty(
        World& world,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        uint64_t frameIndex,
        const AssetRegistry* assetRegistry,
        std::filesystem::path projectRoot,
        RENDER3D::RenderViewPurpose purpose) {

        sceneRenderCache.BeginPatchSync(frameIndex);
        for (const uint64_t removedObjectId : world.GetRemovedRenderObjectIds()) {
            sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{ removedObjectId });
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

        for (GameObject* object : world.GetRenderDirtyObjects()) {
            if (object == nullptr) {
                continue;
            }

            ModelComponent* model = object->GetComponent<ModelComponent>();
            if (model == nullptr) {
                sceneRenderCache.Remove(
                    RENDER3D::RUNTIME::SceneRenderObjectId{ object->GetRenderStableId() });
                continue;
            }

            // Sync と同様に、再構築した地形メッシュへ沙地マテリアルを
            // 保証する。これを欠くと incremental 更新で無地になってしまう。
            if (assetRegistry != nullptr &&
                object->GetComponent<TerrainSurfaceComponent>() != nullptr) {
                (void)SAND::EnsureSandSurfaceMaterialOverride(
                    *model, *assetRegistry);
            }

            sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
                *object,
                *model,
                renderModelCache,
                clusteredGeometryPaths,
                presentationTransforms,
                animationPoses,
                sandSurfaceQuery,
                purpose));
        }

        // Material assets publish into the existing runtime Material object.
        // That revision change does not inherently dirty the owning GameObject,
        // so observe it explicitly before the SceneRenderCache snapshot becomes
        // the authoritative GPU Scene source for this frame.
        world.ForEachObjectWith<ModelComponent>(
            [&](GameObject& object, ModelComponent& model) {
                const RENDER3D::RUNTIME::SceneRenderObjectId objectId =
                    ResolveSceneRenderObjectId(object);
                const RENDER3D::RUNTIME::SceneRenderObject* cached =
                    sceneRenderCache.Find(objectId);
                if (cached != nullptr &&
                    !HasRuntimeMaterialRevisionChange(cached->desc, model)) {
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
                    purpose));
            });

        sceneRenderCache.EndPatchSync();
        sceneRenderCache.PreRenderSync();
        world.AcknowledgeRenderDirtyObjects();
    }

}
