#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::PROCEDURAL {

    struct ProceduralModelDebugStats {
        size_t cacheHitCount = 0;
        size_t cacheMissCount = 0;
        size_t generatedModelCount = 0;
        size_t generatedVertexCount = 0;
        size_t generatedIndexCount = 0;
    };

    const ModelAsset* GetOrCreateModel(const ProceduralMeshSettings& settings);
    std::string GetOrCreateClusteredGeometryPath(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot);
    // デフォルト六種のプロシージャルプリミティブを、1 フレーム 1 種ずつ
    // ウォームアップする。プロジェクトルートが変わると再開する。
    void WarmUpDefaultPrimitiveClusters(
        const std::filesystem::path& projectRoot);
    void ClearCache();
    const ProceduralModelDebugStats& GetDebugStats();

} // namespace HIKARI::PROCEDURAL
