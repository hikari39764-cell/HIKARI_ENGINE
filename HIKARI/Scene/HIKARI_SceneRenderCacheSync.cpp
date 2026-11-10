#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include <optional>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Assets/Geometry/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Render3D/Water/HIKARI_WaterSurfaceRenderData.h"
#include "Render3D/Terrain/HIKARI_TerrainSurfaceRenderData.h"
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
#include "Scene/Terrain/HIKARI_TerrainSurfaceMaterialBinder.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceConversion.h"
#include "Scene/Terrain/HIKARI_TerrainGrassRenderProjection.h"
#include "Scene/Terrain/HIKARI_TerrainRenderProjection.h"

namespace HIKARI {

namespace {
using namespace TERRAIN::RENDER;
constexpr float kPi = 3.14159265358979323846f;
RENDER3D::WATER::WaterSurfaceRenderData BuildWaterSurfaceRenderData(const WATER::WaterSurfaceSettings& authoredSettings,
                                                                    const ProceduralMeshComponent& procedural,
                                                                    const MATH::Mat4& world) {
    const ProceduralMeshSettings mesh = SanitizeProceduralMeshSettings(procedural.GetSettings());
    const float spacingX = mesh.width / static_cast<float>((std::max)(mesh.segmentsX, 1u));
    const float spacingZ = mesh.depth / static_cast<float>((std::max)(mesh.segmentsY, 1u));
    const WATER::WaterSurfaceSettings settings =
        WATER::ResolveWaterSurfaceRuntimeSettings(authoredSettings, (std::max)(spacingX, spacingZ), world);

    RENDER3D::WATER::WaterSurfaceRenderData data{};
    data.shadingModel = settings.preset == WATER::WaterSurfacePreset::RealisticClearSummer
                            ? RENDER3D::WATER::WaterSurfaceShadingModel::RealisticClearSummer
                            : RENDER3D::WATER::WaterSurfaceShadingModel::AnimeSummer;
    data.geometryMode = settings.geometryMode == WATER::WaterSurfaceGeometryMode::CameraCenteredOcean
                            ? RENDER3D::WATER::WaterSurfaceGeometryMode::CameraCenteredOcean
                            : RENDER3D::WATER::WaterSurfaceGeometryMode::FiniteAuthored;
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
    data.windDirectionRadians = settings.windDirectionDeg * (kPi / 180.0f);
    data.windSpeed = settings.windSpeed;
    data.shortWaveStrength = settings.shortWaveStrength;
    data.capillaryStrength = settings.capillaryStrength;
    data.geometryParams = {0.0f, 0.0f, (std::max)(mesh.width * 0.5f, 0.001f), (std::max)(mesh.depth * 0.5f, 0.001f)};
    for (size_t index = 0u; index < data.waves.size(); ++index) {
        const WATER::WaterGerstnerWave& source = settings.waves[index];
        RENDER3D::WATER::WaterWaveRenderData& destination = data.waves[index];
        destination.directionRadians = source.directionDeg * (kPi / 180.0f);
        destination.amplitude = source.amplitude;
        destination.wavelength = source.wavelength;
        destination.steepness = source.steepness;
    }
    return data;
}

RENDER3D::TERRAIN::TerrainSurfaceRenderData BuildTerrainSurfaceRenderData(
    const TERRAIN::TerrainSurfaceSettings& authoredSettings, const ProceduralMeshComponent& procedural) {
    const ProceduralMeshSettings mesh = SanitizeProceduralMeshSettings(procedural.GetSettings());
    const TERRAIN::TerrainSurfaceSettings settings = TERRAIN::SanitizeTerrainSurfaceSettings(authoredSettings);

    RENDER3D::TERRAIN::TerrainSurfaceRenderData data{};
    data.gridResolution = settings.gridResolution;
    data.extents = {mesh.width, mesh.depth};
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

RENDER3D::RUNTIME::SceneRenderObjectId ResolveSceneRenderObjectId(const GameObject& object) {
    return RENDER3D::RUNTIME::SceneRenderObjectId{object.GetRenderStableId()};
}

std::string ResolveClusteredGeometryPath(const GameObject& object, const ModelComponent& model,
                                         const AssetRegistry* assetRegistry, const std::filesystem::path& projectRoot) {
    if (const auto* procedural = object.GetComponent<ProceduralMeshComponent>()) {
        return PROCEDURAL::GetOrCreateClusteredGeometryPath(procedural->GetSettings(), projectRoot);
    }

    if (assetRegistry == nullptr || model.GetAssetId().empty()) {
        return {};
    }

    const auto* descriptor = assetRegistry->FindAs<ModelAssetDescriptor>(AssetGuid{model.GetAssetId()});
    if (descriptor == nullptr || descriptor->clusteredGeometryPath.empty()) {
        return {};
    }

    std::filesystem::path path = descriptor->clusteredGeometryPath;
    if (!path.is_absolute() && !projectRoot.empty()) {
        path = projectRoot / path;
    }
    return path.lexically_normal().generic_string();
}

// 校验模式（環境変数 HIKARI_VALIDATE_RENDER_SYNC=1）：dirty でない物体の描画記述を作り直してキャッシュと比べ、
// 違えば改動通知の漏れとして記録する。テストと検証場景で使い、通常は何もしない。
bool RenderSyncValidationEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("HIKARI_VALIDATE_RENDER_SYNC");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

// 描画の結果を変える項目のうち、違っていた最初の項目名（同じなら nullptr）。地形・水面・骨格動画は別の経路で更新する。
const char* FindRenderStateDifference(const RENDER3D::RUNTIME::SceneRenderObjectDesc& cached,
                                      const RENDER3D::RUNTIME::SceneRenderObjectDesc& rebuilt) {
    const MATH::Mat4 cachedWorld = cached.worldTransform.GetWorldMatrix();
    const MATH::Mat4 rebuiltWorld = rebuilt.worldTransform.GetWorldMatrix();
    if (std::memcmp(&cachedWorld, &rebuiltWorld, sizeof(MATH::Mat4)) != 0) {
        return "worldTransform";
    }
    if (cached.model != rebuilt.model || cached.clusteredGeometryPath != rebuilt.clusteredGeometryPath) {
        return "model";
    }
    if (cached.visible != rebuilt.visible || cached.nodeVisibilityMask != rebuilt.nodeVisibilityMask) {
        return "visible";
    }
    if (cached.isStatic != rebuilt.isStatic || cached.castShadow != rebuilt.castShadow ||
        cached.receiveShadow != rebuilt.receiveShadow || cached.postGroupMask != rebuilt.postGroupMask) {
        return "flags";
    }
    const auto sameMaterial = [](const RENDER3D::RUNTIME::SceneRenderMaterialOverride& a,
                                 const RENDER3D::RUNTIME::SceneRenderMaterialOverride& b) {
        return a.materialIndex == b.materialIndex && a.material == b.material &&
               a.materialRevision == b.materialRevision;
    };
    if (!std::equal(cached.materialOverrides.begin(), cached.materialOverrides.end(), rebuilt.materialOverrides.begin(),
                    rebuilt.materialOverrides.end(), sameMaterial)) {
        return "materials";
    }
    if (cached.materialFxProfileId != rebuilt.materialFxProfileId ||
        std::memcmp(cached.materialFxParamValues, rebuilt.materialFxParamValues,
                    sizeof(cached.materialFxParamValues)) != 0) {
        return "materialFx";
    }
    return nullptr;
}

class ClusteredGeometryPathResolver {
public:
    ClusteredGeometryPathResolver(const AssetRegistry* assetRegistry, std::filesystem::path projectRoot)
        : assetRegistry_(assetRegistry), projectRoot_(std::move(projectRoot)) {}

    // 地形メッシュに合わせて procedural 設定を直接解決する。
    // (terrain は desc.model を差し替えるため、既定の object 経由では
    //  旧平面の clustered 幾何が残ってしまう)
    std::string ResolveProceduralSettings(const ProceduralMeshSettings& settings) {
        return PROCEDURAL::GetOrCreateClusteredGeometryPath(settings, projectRoot_);
    }

    const std::filesystem::path& GetProjectRoot() const noexcept { return projectRoot_; }

    std::string Resolve(const GameObject& object, const ModelComponent& model) {
        if (object.GetComponent<ProceduralMeshComponent>() != nullptr) {
            return ResolveClusteredGeometryPath(object, model, assetRegistry_, projectRoot_);
        }
        if (model.GetAssetId().empty()) {
            return {};
        }
        const std::string& assetId = model.GetAssetId();
        const auto found = pathByAssetId_.find(assetId);
        if (found != pathByAssetId_.end()) {
            return found->second;
        }

        std::string path = ResolveClusteredGeometryPath(object, model, assetRegistry_, projectRoot_);
        pathByAssetId_.emplace(assetId, path);
        return path;
    }

private:
    const AssetRegistry* assetRegistry_ = nullptr;
    std::filesystem::path projectRoot_{};
    std::unordered_map<std::string, std::string> pathByAssetId_{};
};

RENDER3D::RUNTIME::SceneRenderObjectDesc BuildSceneRenderObjectDesc(
    const GameObject& object, ModelComponent& model, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
    ClusteredGeometryPathResolver& clusteredGeometryPaths, const PresentationTransformService* presentationTransforms,
    const ANIMATION::AnimationPoseService* animationPoses,
    const TERRAIN::TerrainSurfaceQueryService* terrainSurfaceQuery,
    const RuntimeObjectActivityService* runtimeActivityService, TerrainProjectionState& terrainProjection,
    SceneRenderCacheSync::DebugStats& syncStats, RENDER3D::RenderViewPurpose purpose) {
    RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
    desc.id = ResolveSceneRenderObjectId(object);
    const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
    const bool debugOnly = debugMode == ModelRenderDebugMode::WireOnly || debugMode == ModelRenderDebugMode::BoundsOnly;
    const bool lightingBakeCapture = purpose == RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
                                     purpose == RENDER3D::RenderViewPurpose::LightProbeCapture;
    desc.visible = model.IsVisible() && !debugOnly &&
                   (runtimeActivityService == nullptr || runtimeActivityService->Get(object.GetRuntimeHandle()) !=
                                                             GameProjectRuntimeActivity::SuspendedHidden) &&
                   (!lightingBakeCapture || model.GetIncludeInLightingBake());

    const ModelAsset* asset = ResolveRenderableModelAsset(object, model);
    desc.model = asset;
    if (asset != nullptr) {
        desc.renderModel = renderModelCache.GetOrCreate(*asset);
    }
    desc.worldTransform = object.GetTransform();
    MATH::Mat4 presentationWorld{};
    if (presentationTransforms != nullptr &&
        presentationTransforms->TryGetWorldMatrix(object.GetRuntimeHandle(), presentationWorld)) {
        desc.worldTransform.useExplicitMatrix = true;
        desc.worldTransform.explicitMatrix = presentationWorld;
    }
    desc.localBounds = RENDER3D::RUNTIME::ResolveLocalBounds(desc.model, desc.renderModel);
    desc.worldBounds = BOUNDS::TransformBounds(desc.localBounds, desc.worldTransform.GetWorldMatrix());

    desc.isStatic = model.IsRenderStatic();
    desc.clusteredGeometryPath = clusteredGeometryPaths.Resolve(object, model);
    desc.castShadow = model.GetCastShadow();
    desc.receiveShadow = model.GetReceiveShadow();
    if (animationPoses != nullptr) {
        desc.animationPose = animationPoses->Find(object.GetRuntimeHandle());
        desc.hasRuntimeAnimation =
            desc.animationPose != nullptr && desc.animationPose->valid &&
            desc.animationPose->localPose.IsValidFor(asset != nullptr ? asset->nodes.size() : 0u);
        desc.animationPoseRevision = desc.animationPose != nullptr ? desc.animationPose->revision : 0u;
    }
    desc.hasSpecialRenderDebug = false;
    desc.allowStaticCachedForward = !desc.hasRuntimeAnimation;
    for (const RuntimeMaterialOverrideSlot& slot : model.GetRuntimeMaterialOverrides()) {
        const Material* runtimeMaterial = slot.material.get();
        if (runtimeMaterial == nullptr) {
            continue;
        }
        desc.materialOverrides.push_back(RENDER3D::RUNTIME::SceneRenderMaterialOverride{
            slot.materialIndex, runtimeMaterial, runtimeMaterial->GetRevision()});
    }
    std::sort(desc.materialOverrides.begin(), desc.materialOverrides.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.materialIndex < rhs.materialIndex; });
    desc.postGroupMask = model.GetPostGroupMask();

    const auto* procedural = object.GetComponent<ProceduralMeshComponent>();
    const auto* terrainSurface = object.GetComponent<TerrainSurfaceComponent>();
    if (terrainSurface != nullptr && procedural != nullptr) {
        const ProceduralMeshKind meshKind = procedural->GetSettings().kind;
        if (meshKind == ProceduralMeshKind::Plane || meshKind == ProceduralMeshKind::GridPlane) {
            const TERRAIN::TerrainSurface& surface = terrainSurface->GetSurface();
            if (!surface.chunks.empty()) {
                const float chunkSize = terrainSurface->GetChunkWorldSize();
                const uint32_t cells = terrainSurface->GetChunkCells();

                desc.terrainSurface = true;
                desc.terrainSurfaceData =
                    BuildTerrainSurfaceRenderData(TERRAIN::ToTerrainSurfaceSettings(surface), *procedural);
                TerrainProjectionOutput projectionOutput{};
                BakeTerrainChunksForRender(surface, *terrainSurface, object.GetRuntimeHandle(), terrainSurfaceQuery,
                                           clusteredGeometryPaths.GetProjectRoot(), chunkSize, cells, terrainProjection,
                                           syncStats, projectionOutput, desc);
                const ProceduralMeshSettings& terrainSettings = projectionOutput.meshSettings;
                const ModelAsset* terrainModel = projectionOutput.warmedModel != nullptr
                                                     ? projectionOutput.warmedModel
                                                     : PROCEDURAL::GetOrCreateModel(terrainSettings);
                if (terrainModel != nullptr) {
                    desc.model = terrainModel;
                    desc.renderModel = renderModelCache.GetOrCreate(*terrainModel);
                    desc.localBounds = terrainModel->bounds;
                    desc.localBounds.min.y = (std::min)(desc.localBounds.min.y, projectionOutput.stableBoundsMinY);
                    desc.localBounds.max.y = (std::max)(desc.localBounds.max.y, projectionOutput.stableBoundsMaxY);
                    desc.worldBounds = BOUNDS::TransformBounds(desc.localBounds, desc.worldTransform.GetWorldMatrix());
                    // CPU と meshlet の両経路で同じ手続き型モデルを使う。
                    desc.clusteredGeometryPath = projectionOutput.warmedClusteredGeometryPath.empty()
                                                     ? clusteredGeometryPaths.ResolveProceduralSettings(terrainSettings)
                                                     : projectionOutput.warmedClusteredGeometryPath;
                }
            }
        }
    }

    const auto* water = object.GetComponent<WaterSurfaceComponent>();
    if (water != nullptr && procedural != nullptr) {
        const ProceduralMeshKind meshKind = procedural->GetSettings().kind;
        if (meshKind == ProceduralMeshKind::Plane || meshKind == ProceduralMeshKind::GridPlane) {
            desc.waterSurface = true;
            desc.waterSurfaceData =
                BuildWaterSurfaceRenderData(water->GetSettings(), *procedural, desc.worldTransform.GetWorldMatrix());
        }
    }

    if (!desc.waterSurface) {
        if (const MaterialFxComponent* materialFx = object.GetComponent<MaterialFxComponent>()) {
            desc.materialFxProfileId = materialFx->GetProfileId();
            desc.materialFxValuesInitialized = materialFx->AreValuesInitialized();
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                desc.materialFxParamValues[i] = materialFx->GetParamValues()[i];
            }
        }
    }
    if (desc.model != nullptr) {
        desc.nodeVisibilityMask = model.ResolveRuntimeNodeVisibilityMask(*desc.model);
    }
    return desc;
}
} // namespace

struct SceneRenderCacheSync::State {
    DebugStats debugStats{};
    std::unordered_map<const RENDER3D::RUNTIME::SceneRenderCache*, std::unordered_map<uint64_t, TerrainProjectionState>>
        terrainProjections{};
    std::unordered_map<const RENDER3D::RUNTIME::SceneRenderCache*, TERRAIN::TerrainGrassRenderProjectionState>
        terrainGrassProjections{};
    std::unordered_set<std::string> presentationMissingAssetsLogged{};
    std::unordered_set<std::string> presentationMissingNodesLogged{};
    std::unordered_set<std::string> presentationAttachmentDiagnosticsLogged{};
    std::unordered_set<std::string> presentationMaterialDiagnosticsLogged{};
    std::unordered_map<const RENDER3D::RUNTIME::SceneRenderCache*, PRESENTATION_MODEL::MaterialStorageByRenderObject>
        presentationMaterialOverrides{};

    uint64_t validationMisses = 0u;

    template <class BuildDesc>
    void ValidateCleanObjects(World& world, const RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
                              BuildDesc&& buildDesc) {
        std::unordered_set<const GameObject*> dirty(world.GetRenderDirtyObjects().begin(),
                                                    world.GetRenderDirtyObjects().end());
        world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
            if (dirty.contains(&object) || object.GetComponent<TerrainSurfaceComponent>() != nullptr ||
                object.GetComponent<WaterSurfaceComponent>() != nullptr) {
                return;
            }
            const RENDER3D::RUNTIME::SceneRenderObject* cached =
                sceneRenderCache.Find(ResolveSceneRenderObjectId(object));
            const RENDER3D::RUNTIME::SceneRenderObjectDesc rebuilt = buildDesc(object, model);
            const char* difference = cached == nullptr ? "missing" : FindRenderStateDifference(cached->desc, rebuilt);
            if (difference == nullptr || rebuilt.hasRuntimeAnimation) {
                return;
            }
            if (++validationMisses <= 32u) {
                HIKARI_LOG_ERROR("[RenderSync] missed change: object='" + object.GetName() + "' field=" + difference);
            }
        });
    }

    void Synchronize(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                     RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache, uint64_t frameIndex,
                     const AssetRegistry* assetRegistry, std::filesystem::path projectRoot,
                     RENDER3D::RenderViewPurpose purpose, ModelManager* modelManager, bool fullSync);
};

SceneRenderCacheSync::SceneRenderCacheSync() : state_(std::make_unique<State>()) {}

SceneRenderCacheSync::~SceneRenderCacheSync() = default;

void SceneRenderCacheSync::Clear() noexcept {
    state_->terrainProjections.clear();
    state_->terrainGrassProjections.clear();
    state_->presentationMissingAssetsLogged.clear();
    state_->presentationMissingNodesLogged.clear();
    state_->presentationAttachmentDiagnosticsLogged.clear();
    state_->presentationMaterialDiagnosticsLogged.clear();
    state_->presentationMaterialOverrides.clear();
}

const SceneRenderCacheSync::DebugStats& SceneRenderCacheSync::GetDebugStats() const noexcept {
    return state_->debugStats;
}

void SceneRenderCacheSync::State::Synchronize(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                                              RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
                                              uint64_t frameIndex, const AssetRegistry* assetRegistry,
                                              std::filesystem::path projectRoot, RENDER3D::RenderViewPurpose purpose,
                                              ModelManager* modelManager, bool fullSync) {
    PROCEDURAL::PumpClusteredGeometryWarmUps();
    auto& presentationMaterials = presentationMaterialOverrides[&sceneRenderCache];
    auto& cacheTerrainProjections = terrainProjections[&sceneRenderCache];
    auto& grassProjections = terrainGrassProjections[&sceneRenderCache];
    if (fullSync) {
        sceneRenderCache.BeginSync(frameIndex);
        cacheTerrainProjections.clear();
        grassProjections.owners.clear();
        grassProjections.activeRenderInstanceCount = 0u;
    } else {
        sceneRenderCache.BeginPatchSync(frameIndex);
        for (const uint64_t removedId : world.GetRemovedRenderObjectIds()) {
            sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{removedId});
            cacheTerrainProjections.erase(removedId);
            TERRAIN::RemoveTerrainGrassRenderObjects(sceneRenderCache, grassProjections, removedId);
        }
    }
    ClusteredGeometryPathResolver clusteredGeometryPaths(assetRegistry, std::move(projectRoot));
    const PresentationTransformService* presentationTransforms = world.Services().Find<PresentationTransformService>();
    const ANIMATION::AnimationPoseService* animationPoses = world.Services().Find<ANIMATION::AnimationPoseService>();
    const TERRAIN::TerrainSurfaceQueryService* terrainSurfaceQuery =
        world.Services().Find<TERRAIN::TerrainSurfaceQueryService>();
    const RuntimeObjectActivityService* runtimeActivityService = world.Services().Find<RuntimeObjectActivityService>();

    PresentationModelService* presentationModels = world.Services().Find<PresentationModelService>();
    if (presentationModels != nullptr) {
        std::vector<uint64_t> occupiedObjectIds{};
        if (presentationModels->HasRecords()) {
            occupiedObjectIds.reserve(world.GetObjects().size());
            for (const auto& object : world.GetObjects()) {
                if (object != nullptr) {
                    occupiedObjectIds.push_back(object->GetRenderStableId());
                }
            }
        }
        static const auto prepareTimer = CPU_PROFILE::RegisterDynamicPass("Cache.PresentationPrepare");
        const CPU_PROFILE::ScopedDynamicCpuTimer prepareScope(prepareTimer);
        presentationModels->PrepareForRender(frameIndex, occupiedObjectIds);
        for (const uint64_t removedId : presentationModels->GetRemovedRenderObjectIds()) {
            sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{removedId});
            presentationMaterials.erase(removedId);
        }
    }

    const auto upsertModel = [&](GameObject& object, ModelComponent& model) {
        const RENDER3D::RUNTIME::SceneRenderObjectId objectId = ResolveSceneRenderObjectId(object);
        // 投影状態は地形だけが持つ。通常 model ごとに空の状態を作らない。
        TerrainProjectionState unusedProjection{};
        TerrainProjectionState& terrainProjection = object.GetComponent<TerrainSurfaceComponent>() != nullptr
                                                        ? cacheTerrainProjections[objectId.value]
                                                        : unusedProjection;
        sceneRenderCache.Upsert(BuildSceneRenderObjectDesc(
            object, model, renderModelCache, clusteredGeometryPaths, presentationTransforms, animationPoses,
            terrainSurfaceQuery, runtimeActivityService, terrainProjection, debugStats, purpose));
    };
    const auto syncObject = [&](GameObject& object, ModelComponent& model) {
        // 全量・差分のどちらも、再構築した地形へのマテリアルを保証する。
        if (assetRegistry != nullptr && object.GetComponent<TerrainSurfaceComponent>() != nullptr) {
            (void)TERRAIN::EnsureTerrainSurfaceMaterialOverride(model, *assetRegistry);
        }
        upsertModel(object, model);
        if (const TerrainSurfaceComponent* terrain = object.GetComponent<TerrainSurfaceComponent>()) {
            // 草は authored 入力が変わった時だけ作り直す (runtime 応答の dirty では維持)。
            TERRAIN::TerrainGrassRenderProjectionStats grassStats{};
            TERRAIN::SyncTerrainGrassRenderObjects(object, model, *terrain, renderModelCache,
                                                   clusteredGeometryPaths.GetProjectRoot(), presentationTransforms,
                                                   grassProjections, sceneRenderCache, purpose, grassStats);
            debugStats.terrainGrassRenderInstanceCount += grassStats.renderedCount;
            debugStats.terrainGrassRenderSkippedCount += grassStats.skippedCount;
        } else {
            TERRAIN::RemoveTerrainGrassRenderObjects(sceneRenderCache, grassProjections, object.GetRenderStableId());
        }
    };
    if (fullSync) {
        world.ForEachObjectWith<ModelComponent>(syncObject);
    } else {
        for (GameObject* object : world.GetRenderDirtyObjects()) {
            if (object == nullptr)
                continue;
            const uint64_t ownerId = object->GetRenderStableId();
            if (ModelComponent* model = object->GetComponent<ModelComponent>()) {
                syncObject(*object, *model);
            } else {
                TERRAIN::RemoveTerrainGrassRenderObjects(sceneRenderCache, grassProjections, ownerId);
                sceneRenderCache.Remove(RENDER3D::RUNTIME::SceneRenderObjectId{ownerId});
                cacheTerrainProjections.erase(ownerId);
            }
        }
    }

    static const auto presentationTimer = CPU_PROFILE::RegisterDynamicPass("Cache.PresentationSync");
    std::optional<CPU_PROFILE::ScopedDynamicCpuTimer> presentationScope;
    presentationScope.emplace(presentationTimer);
    PRESENTATION_MODEL::SyncRenderObjects(world, presentationModels, renderModelCache, sceneRenderCache, assetRegistry,
                                          modelManager, clusteredGeometryPaths.GetProjectRoot(), presentationTransforms,
                                          animationPoses, presentationMaterials, presentationMissingAssetsLogged,
                                          presentationMissingNodesLogged, presentationAttachmentDiagnosticsLogged,
                                          presentationMaterialDiagnosticsLogged, fullSync);

    presentationScope.reset();
    static const auto endTimer = CPU_PROFILE::RegisterDynamicPass("Cache.EndSync");
    const CPU_PROFILE::ScopedDynamicCpuTimer endScope(endTimer);
    if (fullSync) {
        sceneRenderCache.EndSync();
    } else {
        // 材質の原地変更（版本の更新）は ModelComponent が材質の通知を受けて物体を dirty にするので、ここで全物体を
        // 見直さない。残るのは地形の非同期 warm-up の完了だけで、warm 待ちの地形投影だけを確かめる。
        for (auto& entry : cacheTerrainProjections) {
            TerrainProjectionState& projection = entry.second;
            if (!projection.warmPending || !projection.warmSettingsValid) {
                continue;
            }
            const ModelAsset* warmedModel = nullptr;
            std::string warmedPath{};
            if (!PROCEDURAL::TryGetWarmedClusteredGeometry(projection.warmSettings, projection.warmProjectRoot,
                                                           warmedModel, warmedPath)) {
                continue;
            }
            GameObject* object = world.FindObject(projection.sourceObject);
            if (ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr) {
                upsertModel(*object, *model);
            }
        }
        if (RenderSyncValidationEnabled()) {
            ValidateCleanObjects(world, sceneRenderCache, [&](GameObject& object, ModelComponent& model) {
                TerrainProjectionState unusedProjection{};
                return BuildSceneRenderObjectDesc(object, model, renderModelCache, clusteredGeometryPaths,
                                                  presentationTransforms, animationPoses, terrainSurfaceQuery,
                                                  runtimeActivityService, unusedProjection, debugStats, purpose);
            });
        }
        sceneRenderCache.EndPatchSync();
    }
    sceneRenderCache.PreRenderSync();
    if (fullSync)
        world.AcknowledgeAllRenderObjects();
    else
        world.AcknowledgeRenderDirtyObjects();
    if (presentationModels != nullptr) {
        presentationModels->AcknowledgeRenderFrame();
    }
}

void SceneRenderCacheSync::Sync(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                                RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache, uint64_t frameIndex,
                                const AssetRegistry* assetRegistry, std::filesystem::path projectRoot,
                                RENDER3D::RenderViewPurpose purpose, ModelManager* modelManager) {
    state_->Synchronize(world, renderModelCache, sceneRenderCache, frameIndex, assetRegistry, std::move(projectRoot),
                        purpose, modelManager, true);
}

void SceneRenderCacheSync::SyncDirty(World& world, RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                                     RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache, uint64_t frameIndex,
                                     const AssetRegistry* assetRegistry, std::filesystem::path projectRoot,
                                     RENDER3D::RenderViewPurpose purpose, ModelManager* modelManager) {
    state_->Synchronize(world, renderModelCache, sceneRenderCache, frameIndex, assetRegistry, std::move(projectRoot),
                        purpose, modelManager, false);
}

} // namespace HIKARI
