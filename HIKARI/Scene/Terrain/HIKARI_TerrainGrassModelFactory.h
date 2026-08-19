#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::TERRAIN {

    // Grass is a terrain render projection, not an authored Primitive kind.
    // These four immutable model variants are shared by all generated clumps.
    constexpr uint32_t kTerrainGrassModelVariantCount = 4u;
    // Bump when the generated topology, vertex semantics, or cluster cook
    // policy changes. It is independent from ProceduralMeshFactory revisions.
    constexpr uint32_t kTerrainGrassClusterGeometryRevision = 1u;

    const ModelAsset* GetOrCreateTerrainGrassModel(
        uint32_t variantIndex);

    // Returns a stable Mesh-Shader clustered geometry artifact path and cooks
    // the artifact on first use. The path is rooted at
    // <projectRoot>/Library/Generated/TerrainGrass (or relative Library when
    // projectRoot is empty) and contains both revision and variant identity.
    std::string GetOrCreateTerrainGrassClusteredGeometryPath(
        uint32_t variantIndex,
        const std::filesystem::path& projectRoot);

    void ClearTerrainGrassModelCache() noexcept;

} // namespace HIKARI::TERRAIN
