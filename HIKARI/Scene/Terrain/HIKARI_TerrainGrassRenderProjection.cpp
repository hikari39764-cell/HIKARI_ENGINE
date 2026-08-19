#include "Scene/Terrain/HIKARI_TerrainGrassRenderProjection.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/Terrain/HIKARI_TerrainGrassModelFactory.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI::TERRAIN {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kReferenceBladeHeight = 0.34f;
        constexpr float kReferenceBladeWidth = 0.028f;
        constexpr uint64_t kSyntheticIdTag = 0x5445525247524153ull;

        static_assert(
            kTerrainGrassVariantCount == kTerrainGrassModelVariantCount);

        uint64_t MakeSyntheticId(
            uint64_t ownerStableId,
            const TerrainGrassPlacement& placement) noexcept {
            uint64_t hash = HASH::kFnv1a64OffsetBasis;
            hash = HASH::Fnv1aBytes(
                hash, &kSyntheticIdTag, sizeof(kSyntheticIdTag));
            hash = HASH::Fnv1aBytes(
                hash, &ownerStableId, sizeof(ownerStableId));
            hash = HASH::Fnv1aBytes(
                hash, &placement.stableHash, sizeof(placement.stableHash));
            hash = HASH::Fnv1aBytes(
                hash, &placement.variantIndex,
                sizeof(placement.variantIndex));
            hash |= (uint64_t{ 1u } << 63u);
            return hash != 0u ? hash : (uint64_t{ 1u } << 63u);
        }

        Bounds ResolveLocalBounds(
            const ModelAsset* model,
            const RENDER3D::RUNTIME::RenderModelAsset* renderModel) {
            if (renderModel != nullptr && renderModel->valid &&
                BOUNDS::IsUsable(renderModel->localBounds)) {
                return renderModel->localBounds;
            }
            if (model != nullptr && BOUNDS::IsUsable(model->bounds)) {
                return model->bounds;
            }
            return {};
        }

        bool IsLightingBakeCapture(
            RENDER3D::RenderViewPurpose purpose) noexcept {
            return purpose ==
                    RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
                purpose == RENDER3D::RenderViewPurpose::LightProbeCapture;
        }
    }

    void RemoveTerrainGrassRenderObjects(
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        TerrainGrassRenderProjectionState& state,
        uint64_t ownerStableId) {
        const auto found = state.renderObjectIdsByOwner.find(ownerStableId);
        if (found == state.renderObjectIdsByOwner.end()) {
            return;
        }
        for (const uint64_t id : found->second) {
            sceneRenderCache.Remove(
                RENDER3D::RUNTIME::SceneRenderObjectId{ id });
        }
        state.activeRenderInstanceCount =
            state.activeRenderInstanceCount >= found->second.size()
                ? state.activeRenderInstanceCount - found->second.size()
                : 0u;
        state.renderObjectIdsByOwner.erase(found);
    }

    void SyncTerrainGrassRenderObjects(
        const GameObject& object,
        const ModelComponent& terrainModel,
        const TerrainSurfaceComponent& terrain,
        RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
        const std::filesystem::path& projectRoot,
        const PresentationTransformService* presentationTransforms,
        TerrainGrassRenderProjectionState& state,
        RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
        RENDER3D::RenderViewPurpose purpose,
        TerrainGrassRenderProjectionStats& stats) {

        const uint64_t ownerStableId = object.GetRenderStableId();
        RemoveTerrainGrassRenderObjects(
            sceneRenderCache, state, ownerStableId);

        const ProceduralMeshComponent* procedural =
            object.GetComponent<ProceduralMeshComponent>();
        if (procedural == nullptr ||
            (procedural->GetSettings().kind != ProceduralMeshKind::Plane &&
             procedural->GetSettings().kind !=
                ProceduralMeshKind::GridPlane)) {
            return;
        }

        const TerrainGrassSettings settings =
            SanitizeTerrainGrassSettings(terrain.GetGrassSettings());
        const TerrainGrassPlacementSnapshot snapshot =
            terrain.BuildGrassPlacementSnapshot();
        if (!snapshot.enabled || snapshot.placements.empty()) {
            return;
        }

        MATH::Mat4 terrainWorld = object.GetTransform().GetWorldMatrix();
        if (presentationTransforms != nullptr) {
            MATH::Mat4 presentationWorld{};
            if (presentationTransforms->TryGetWorldMatrix(
                    object.GetRuntimeHandle(), presentationWorld)) {
                terrainWorld = presentationWorld;
            }
        }

        if (state.clusteredGeometryProjectRoot != projectRoot) {
            state.clusteredGeometryProjectRoot = projectRoot;
            state.clusteredGeometryPaths.fill({});
            state.clusteredGeometryAttempted.fill(false);
        }

        std::array<const ModelAsset*, kTerrainGrassVariantCount> models{};
        auto& ids = state.renderObjectIdsByOwner[ownerStableId];
        ids.reserve((std::min)(
            (std::min)(
                snapshot.placements.size(),
                static_cast<std::size_t>(settings.maxClumps)),
            state.activeRenderInstanceCount <
                    kTerrainGrassMaxProjectedInstances
                ? static_cast<std::size_t>(
                    kTerrainGrassMaxProjectedInstances -
                    state.activeRenderInstanceCount)
                : 0u));

        const float authoredHeightScale =
            settings.bladeHeight / kReferenceBladeHeight;
        const float authoredWidthScale =
            settings.bladeWidth / kReferenceBladeWidth;
        for (const TerrainGrassPlacement& placement : snapshot.placements) {
            if (!IsTerrainGrassPlacementValid(placement)) {
                ++stats.skippedCount;
                continue;
            }
            if (ids.size() >= settings.maxClumps) {
                ++stats.skippedCount;
                continue;
            }
            if (state.activeRenderInstanceCount >=
                kTerrainGrassMaxProjectedInstances) {
                ++stats.skippedCount;
                continue;
            }

            // Placement snapshot の y は authored height であり、coverage
            // shoreline の edge sink を含まない。描画位置は terrain の
            // authoritative surface query に再解決し、穴/可視 edge で
            // surface が無い場合は草を投影しない。sink の式をここで
            // 複製せず、CPU query と GPU terrain deform の共有実装を使う。
            float resolvedSurfaceHeight = 0.0f;
            if (!terrain.SampleLocalSurfaceHeight(
                    { placement.localPosition.x, 0.0f,
                        placement.localPosition.z },
                    resolvedSurfaceHeight) ||
                !std::isfinite(resolvedSurfaceHeight)) {
                ++stats.skippedCount;
                continue;
            }

            const uint32_t variant = placement.variantIndex %
                kTerrainGrassVariantCount;
            if (models[variant] == nullptr) {
                models[variant] = GetOrCreateTerrainGrassModel(variant);
            }
            const ModelAsset* model = models[variant];
            if (model == nullptr) {
                ++stats.skippedCount;
                continue;
            }
            const auto* renderModel = renderModelCache.GetOrCreate(*model);
            const Bounds localBounds = ResolveLocalBounds(model, renderModel);
            if (renderModel == nullptr || !BOUNDS::IsUsable(localBounds)) {
                ++stats.skippedCount;
                continue;
            }
            if (!state.clusteredGeometryAttempted[variant]) {
                state.clusteredGeometryAttempted[variant] = true;
                state.clusteredGeometryPaths[variant] =
                    GetOrCreateTerrainGrassClusteredGeometryPath(
                        variant, projectRoot);
            }
            // Grass has no authored-object fallback: its generated clump is
            // admitted only when the shared clustered geometry artifact is
            // available, keeping Mesh Shader clustered geometry as the sole
            // render mainline.
            if (state.clusteredGeometryPaths[variant].empty()) {
                ++stats.skippedCount;
                continue;
            }

            const float yawRadians = placement.yawDegrees * (kPi / 180.0f);
            const MATH::Vec3 scale{
                authoredWidthScale * placement.widthScale,
                authoredHeightScale * placement.heightScale,
                authoredWidthScale * placement.widthScale,
            };
            const MATH::Mat4 localTransform = MATH::Mat4::TRS(
                {
                    placement.localPosition.x,
                    resolvedSurfaceHeight + 0.003f,
                    placement.localPosition.z,
                },
                MATH::Quat::FromEulerXYZ(0.0f, yawRadians, 0.0f),
                scale);

            RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
            desc.id = { MakeSyntheticId(ownerStableId, placement) };
            desc.model = model;
            desc.renderModel = renderModel;
            desc.worldTransform.useExplicitMatrix = true;
            desc.worldTransform.explicitMatrix = terrainWorld * localTransform;
            desc.localBounds = localBounds;
            desc.worldBounds = BOUNDS::TransformBounds(
                localBounds, desc.worldTransform.explicitMatrix);
            desc.worldBoundsPadding = {
                settings.windStrength,
                settings.windStrength * 0.08f,
                settings.windStrength,
            };
            desc.visible = terrainModel.IsVisible() &&
                !IsLightingBakeCapture(purpose);
            desc.isStatic = false;
            // Dynamic foliage is intentionally not a shadow caster. This keeps
            // it out of the legacy ShadowStaticVS route, while clustered
            // Mesh Shader geometry remains the only grass render mainline.
            desc.castShadow = false;
            desc.receiveShadow = terrainModel.GetReceiveShadow();
            desc.allowStaticCachedForward = false;
            desc.clusteredGeometryPath =
                state.clusteredGeometryPaths[variant];
            desc.postGroupMask = terrainModel.GetPostGroupMask();
            desc.foliageWind = true;
            desc.foliageWindParams = {
                settings.windStrength,
                settings.windSpeed,
                settings.spatialFrequency,
                0.0f,
            };
            sceneRenderCache.Upsert(desc);
            ids.push_back(desc.id.value);
            ++state.activeRenderInstanceCount;
            ++stats.renderedCount;
        }
        if (ids.empty()) {
            state.renderObjectIdsByOwner.erase(ownerStableId);
        }
    }

} // namespace HIKARI::TERRAIN
