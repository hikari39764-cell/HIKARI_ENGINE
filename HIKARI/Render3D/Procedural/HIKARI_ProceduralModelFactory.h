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
    // 高密度 terrain response 用 model/hcmesh を worker で準備する。
    // Try は main thread を cook せず、model と path の両方が publish 済みの
    // ときだけ true を返す。
    void RequestClusteredGeometryWarmUp(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot);
    bool TryGetWarmedClusteredGeometry(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot,
        const ModelAsset*& outModel,
        std::string& outPath);
    void PumpClusteredGeometryWarmUps();
    // デフォルト六種のプロシージャルプリミティブを、1 フレーム 1 種ずつ
    // ウォームアップする。プロジェクトルートが変わると再開する。
    void WarmUpDefaultPrimitiveClusters(
        const std::filesystem::path& projectRoot);
    void ClearCache();
    const ProceduralModelDebugStats& GetDebugStats();

} // namespace HIKARI::PROCEDURAL
