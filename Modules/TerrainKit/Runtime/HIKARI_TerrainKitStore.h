#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "HIKARI/Project/WorldView.h"
#include "HIKARI_TerrainKitGpuErosion.h"
#include "HIKARI_TerrainKitRivers.h"
#include "HIKARI_TerrainKitRuleBaker.h"
#include "HIKARI_TerrainKitSurfaces.h"
#include "TerrainKit/Data/HIKARI_TerrainData.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"

namespace HIKARI::TERRAINKIT {

// 部品の設定（場面の JSON に保存する）。
struct TerrainSettings {
    TERRAIN::DATA::TerrainGrid grid{};
    int32_t firstTileX = 0;
    int32_t firstTileZ = 0;
    int32_t tilesX = 8;
    int32_t tilesZ = 8;
    bool operator==(const TerrainSettings&) const = default;
};

inline constexpr uint32_t kTileLeafDivisions = 8u; // 瓦片一枚の葉の節の数（辺あたり）

// 焼いた瓦片。高さと法線は描画へ送る形、min/max は遠近分級の境界。
struct TileRuntime {
    uint32_t layer = 0u;
    bool uploadPending = true;
    uint64_t revision = 0u; // 焼くたびに増える（道案内の塊の版）
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<uint16_t> heights{};
    std::vector<int16_t> normalMips{}; // 全段を続けて持つ（xz の 2 成分）
    std::vector<float> leafMinMax{};   // kTileLeafDivisions² × 2
    // 一部だけ焼き直した範囲（瓦片の中の標本番号、0 段）。uploadPending が立っていれば全部送るので要らない。
    uint32_t pendingX0 = 0u;
    uint32_t pendingZ0 = 0u;
    uint32_t pendingX1 = 0u;
    uint32_t pendingZ1 = 0u;
    bool rectPending = false;
    // 制御図の一部だけ送る範囲（筆で塗った所）。
    uint32_t controlX0 = 0u;
    uint32_t controlZ0 = 0u;
    uint32_t controlX1 = 0u;
    uint32_t controlZ1 = 0u;
    bool controlRectPending = false;
    std::vector<uint8_t> colorMap{}; // 色の図（全段を続けて、RGBA8）
    bool colorMapPending = false;
    bool colorMapUploaded = false;
};

// 積雪と湿り（材質の名前によらず全面に掛かる）。
struct TerrainWeather {
    float snowLineMeters = 170.0f;
    float snowAmount = 0.0f;
    float wetness = 0.0f;
    bool operator==(const TerrainWeather&) const = default;
};

// 瓦片の光線追跡の代理（実体は引擎）。
struct TerrainRayProxy {
    uint32_t resource = 0u;
    uint64_t revision = 0u;
    uint64_t builtFrame = 0u;
};

// 描画の資源の番号（実体は引擎）。文書の世界と Play の世界で同じものを使う。
struct TerrainRenderResources {
    uint32_t heightTexture = 0u;
    uint32_t normalTexture = 0u;
    uint32_t indexBuffer = 0u;
    uint32_t indexCount = 0u;
    uint32_t controlTexture = 0u;
    uint32_t surfaceColor = 0u;
    uint32_t surfaceDetail = 0u;
    uint32_t surfaceParams = 0u;
    uint32_t colorMap = 0u;
    uint32_t colorMapsUploaded = 0u;
    uint64_t surfaceRevision = 0u;
    uint32_t surfacesUploaded = 0u; // 送り終えた枠の数。全部終わるまで着色器へ渡さない。
    uint64_t layoutRevision = 0u;
    std::map<TERRAIN::DATA::TileCoord, TerrainRayProxy> rayProxies{};
    uint64_t frame = 0u;
};

// 一つの場面の地形の編集データと焼いた結果。文書の世界と Play の世界で共有する（プロセスに一つ）。
class TerrainStore {
public:
    static TerrainStore& Get();

    // 場面のファイルに添えた内容を読む。同じ場面なら何もしない。無ければ空の地形を設定どおり作る。
    bool Open(const std::filesystem::path& scenePath, const TerrainSettings& settings, std::string* error = nullptr);
    void Close();
    bool IsOpen() const noexcept { return data_ != nullptr; }
    const std::filesystem::path& ScenePath() const noexcept { return scenePath_; }
    // 地形の部品が載っている世界（実例集の読み書きに使う）。
    void BindWorld(const WorldView& world) noexcept { world_ = world; }
    const WorldView& World() const noexcept { return world_; }
    const TerrainSettings& Settings() const noexcept { return settings_; }

    // 今の格子で、噪声の層と彫る層だけの新しい編集データを作る。
    std::unique_ptr<TERRAIN::DATA::TerrainData> MakeGeneratedData(const TERRAIN::DATA::NoiseLayerSettings& noise,
                                                                  std::string& error) const;
    // 編集データを丸ごと入れ替えて焼き直し、前のものを返す（撤销で戻すため）。
    std::unique_ptr<TERRAIN::DATA::TerrainData> ReplaceData(std::unique_ptr<TERRAIN::DATA::TerrainData> data);

    TERRAIN::DATA::TerrainData& Data() noexcept { return *data_; }
    const TERRAIN::DATA::TerrainData& Data() const noexcept { return *data_; }
    uint32_t SculptLayerId() const noexcept { return sculptLayer_; }

    // 層の記述が変わった時：足りない画像を読み、全部を焼き直して保存の対象にする。
    void RebakeAfterStackChange();
    // 層の画像がまだ読まれていなければ読む。読めない画像は理由を返す（他の層は続ける）。
    void LoadLayerImages(std::string* error = nullptr);
    // 場面の属するプロジェクトの根（画像の相対パスの起点、書き出し先の親）。
    std::filesystem::path ProjectRoot() const;

    // 変わった瓦片を焼き直す。keys が空なら全部。
    void RebakeTiles(std::span<const TERRAIN::DATA::TileCoord> tiles);
    // 筆刷の一度分：矩形とその周り 1 標本だけを焼き直し、その範囲だけを描画へ送る。
    void RebakeRect(const TERRAIN::DATA::SampleRect& rect);
    // 曲線の層を変えた時：変わった範囲だけ焼き直し、層の記述を保存の対象にする。
    void RebakeAfterSplineChange(const TERRAIN::DATA::SampleRect& rect);
    // 制御図を塗った範囲を描画へ送り、保存の対象にする（高さは焼き直さない）。
    void MarkControlChanged(const TERRAIN::DATA::SampleRect& rect);

    // 地表材質の枠。枠の名前は場面の .cells に保存する。
    TerrainSurfaceLibrary& Surfaces() noexcept { return surfaces_; }
    const TerrainSurfaceLibrary& Surfaces() const noexcept { return surfaces_; }
    void SetSurfaceSlots(std::vector<std::string> names);
    // 自動の材質規則。変えると全部の瓦片を背景で計算し直す。
    const TERRAIN::DATA::SurfaceRuleSet& SurfaceRules() const noexcept { return surfaceRules_; }
    void SetSurfaceRules(TERRAIN::DATA::SurfaceRuleSet rules);
    TerrainRuleBaker& RuleBaker() noexcept { return ruleBaker_; }
    // 背景の計算の結果を受け取る。手塗りの標本は今の値を残す。
    void ApplyRuleResult(TERRAIN::DATA::TileCoord tile, std::vector<uint32_t> control, std::vector<uint8_t> colorMap);
    const TerrainWeather& Weather() const noexcept { return weather_; }
    void SetWeather(const TerrainWeather& weather);
    // 背景の thread を全部止める。DLL を外す前に呼ぶ。
    void ShutdownWorkers();
    // 変わった瓦片とその周り（法線が縁の標本を見る）を焼き直し、保存の対象にする。
    void RebakeChanged(std::span<const TERRAIN::DATA::TileCoord> tiles);
    void MarkSaveDirty(std::span<const TERRAIN::DATA::TileCoord> tiles);
    bool HasUnsavedTiles() const noexcept { return !unsavedTiles_.empty() || stackDirty_; }

    // 場面の .cells へ書く。変わった瓦片と層の記述だけ。
    bool Save(const std::filesystem::path& scenePath, std::string& error);

    const std::map<TERRAIN::DATA::TileCoord, TileRuntime>& Tiles() const noexcept { return tiles_; }
    std::map<TERRAIN::DATA::TileCoord, TileRuntime>& MutableTiles() noexcept { return tiles_; }
    // 描画の資源の作り直しなどで全部送り直す。
    void MarkAllForUpload() noexcept;
    uint64_t LayoutRevision() const noexcept { return layoutRevision_; }
    TerrainRenderResources& Render() noexcept { return render_; }
    GpuErosionJob& Erosion() noexcept { return erosion_; }
    TerrainRivers& Rivers() noexcept { return rivers_; }

    // 世界 XZ の高さ（焼いた瓦片の双線形）。瓦片が無ければ空。
    std::optional<float> HeightAt(float worldX, float worldZ) const;
    // 光線と地面の交点（前へ進めて二分で詰める）。
    bool Raycast(const float origin[3], const float direction[3], float maxDistance, float outHit[3]) const;

private:
    WorldView world_{};
    void BuildTileList();
    void BakeTile(TERRAIN::DATA::TileCoord tile, const TERRAIN::DATA::HeightStackEvaluator& evaluator);
    void BakeTileRect(TERRAIN::DATA::TileCoord tile, const TERRAIN::DATA::SampleRect& area,
                      const TERRAIN::DATA::HeightStackEvaluator& evaluator);
    bool LoadFromDisk(const std::filesystem::path& scenePath, std::string* error);

    std::filesystem::path scenePath_{};
    TerrainSettings settings_{};
    std::unique_ptr<TERRAIN::DATA::TerrainData> data_{};
    uint32_t sculptLayer_ = 0u;
    std::map<TERRAIN::DATA::TileCoord, TileRuntime> tiles_{};
    std::vector<TERRAIN::DATA::TileCoord> unsavedTiles_{};
    bool stackDirty_ = false;
    uint64_t layoutRevision_ = 1u;
    TerrainRenderResources render_{};
    GpuErosionJob erosion_{};
    TerrainRivers rivers_{};
    TerrainSurfaceLibrary surfaces_{};
    bool surfaceSlotsDirty_ = false;
    TERRAIN::DATA::SurfaceRuleSet surfaceRules_{};
    TerrainRuleBaker ruleBaker_{};
    TerrainWeather weather_{};
    std::map<uint32_t, std::string> loadedImageKeys_{};
};

std::filesystem::path TerrainCellDirectory(const std::filesystem::path& scenePath);
// モジュールの DLL のある場所（Content と Shaders の親）。
std::filesystem::path TerrainModuleDirectory();

} // namespace HIKARI::TERRAINKIT
