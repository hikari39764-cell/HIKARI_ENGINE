#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Terrain/HIKARI_TerrainGrass.h"

namespace HIKARI {
class GameObject;
class ModelComponent;
class PresentationTransformService;
class TerrainSurfaceComponent;

namespace RENDER3D::RUNTIME {
class RenderModelCache;
class SceneRenderCache;
} // namespace RENDER3D::RUNTIME

namespace TERRAIN {

struct TerrainGrassRenderProjectionStats {
    uint64_t renderedCount = 0u;
    uint64_t skippedCount = 0u;
};

// 草を最後に投影した authored 入力。runtime 変形だけの dirty では
// 配置も高さも変わらないため、同じ入力なら投影を作り直さない。
struct TerrainGrassRenderSourceKey {
    uint64_t terrainRevision = 0u;
    MATH::Mat4 terrainWorld{};
    std::filesystem::path projectRoot{};
    uint32_t postGroupMask = 0u;
    bool visible = false;
    bool receiveShadow = false;
    bool lightingBakeCapture = false;
};

struct TerrainGrassOwnerProjection {
    TerrainGrassRenderSourceKey source{};
    std::vector<uint64_t> renderObjectIds{};
};

// Grass blades are render projection instances derived from the
// authored Grass terrain layer. They are never serialized GameObjects.
struct TerrainGrassRenderProjectionState {
    std::unordered_map<uint64_t, TerrainGrassOwnerProjection> owners{};
    size_t activeRenderInstanceCount = 0u;
    std::array<std::string, kTerrainGrassVariantCount> clusteredGeometryPaths{};
    // Prevent a failed cook from being retried once per placement.
    // A project-root change invalidates this per-variant attempt state.
    std::array<bool, kTerrainGrassVariantCount> clusteredGeometryAttempted{};
    std::filesystem::path clusteredGeometryProjectRoot{};
};

void RemoveTerrainGrassRenderObjects(RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
                                     TerrainGrassRenderProjectionState& state, uint64_t ownerStableId);

void SyncTerrainGrassRenderObjects(const GameObject& object, const ModelComponent& terrainModel,
                                   const TerrainSurfaceComponent& terrain,
                                   RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                                   const std::filesystem::path& projectRoot,
                                   const PresentationTransformService* presentationTransforms,
                                   TerrainGrassRenderProjectionState& state,
                                   RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
                                   RENDER3D::RenderViewPurpose purpose, TerrainGrassRenderProjectionStats& stats);

} // namespace TERRAIN
} // namespace HIKARI
