#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"

namespace HIKARI {
class ModelAsset;
}
namespace HIKARI::ASSETS::GEOMETRY {
struct ClusteredGeometryCookSettings;
}
namespace HIKARI::RENDER3D::CLUSTER {
struct ClusterGeometryPackedBytes;
}

namespace HIKARI::PROCEDURAL {

// 実行時に生成する clustered geometry（手続き形状・地形の草など）の HCMESH 置き場：
// <project>/Library/Generated/<category>/<sourceId>-hcmesh<版>.hcmesh。Shipping では同梱物があればそれを使い、
// 無ければ利用者のキャッシュ（<runtime user>/Cache/Generated/...）へ書く。
std::filesystem::path ResolveGeneratedClusterGeometryPath(const std::filesystem::path& projectRoot,
                                                          std::string_view category, std::string_view sourceId);

// model を clustered geometry に cook して outputPath へ HCMESH を書き、GPU 用の packed データを outPacked へ返す。
// cook と書き込みの失敗は false。pack だけ失敗した時は outPacked を空のまま true（ファイルからは読める）。
bool CookGeneratedClusterGeometry(const ModelAsset& model, std::string_view sourceId,
                                  const ASSETS::GEOMETRY::ClusteredGeometryCookSettings& settings,
                                  const std::filesystem::path& outputPath,
                                  RENDER3D::CLUSTER::ClusterGeometryPackedBytes& outPacked);

struct ProceduralModelDebugStats {
    size_t cacheHitCount = 0;
    size_t cacheMissCount = 0;
    size_t generatedModelCount = 0;
    size_t generatedVertexCount = 0;
    size_t generatedIndexCount = 0;
};

const ModelAsset* GetOrCreateModel(const ProceduralMeshSettings& settings);
std::string GetOrCreateClusteredGeometryPath(const ProceduralMeshSettings& settings,
                                             const std::filesystem::path& projectRoot);
// 高密度 terrain response 用 model/hcmesh を worker で準備する。
// Try は main thread を cook せず、model と path の両方が publish 済みの
// ときだけ true を返す。
void RequestClusteredGeometryWarmUp(const ProceduralMeshSettings& settings, const std::filesystem::path& projectRoot);
bool TryGetWarmedClusteredGeometry(const ProceduralMeshSettings& settings, const std::filesystem::path& projectRoot,
                                   const ModelAsset*& outModel, std::string& outPath);
void PumpClusteredGeometryWarmUps();
// デフォルト六種のプロシージャルプリミティブを、1 フレーム 1 種ずつ
// ウォームアップする。プロジェクトルートが変わると再開する。
void WarmUpDefaultPrimitiveClusters(const std::filesystem::path& projectRoot);
void ClearCache();
const ProceduralModelDebugStats& GetDebugStats();

} // namespace HIKARI::PROCEDURAL
