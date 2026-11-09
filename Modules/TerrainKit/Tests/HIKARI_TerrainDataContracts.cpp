// 地形データ核の行動テスト：制御図、格子、タイルの縁の共有、層スタックの JSON と評価、筆刷、撤销、侵蝕、セルファイル、
// 高さ画像の読み書き。引数 --bench で 1 km² の侵蝕とタイル評価の時間を出す。環境変数 HIKARI_TERRAIN_IMPORT_SAMPLE に
// Gaea / World Machine などが書き出した EXR / R16 を指すと、それを画像層として評価して統計を出す。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <system_error>
#include <numeric>
#include <string>
#include <vector>

#include "TerrainKit/Data/HIKARI_TerrainData.h"
#include "Scene/Serialization/HIKARI_WorldInstanceFile.h"
#include "TerrainKit/Data/HIKARI_TerrainDataAnalysis.h"
#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"
#include "TerrainKit/Data/HIKARI_TerrainDataErosion.h"
#include "TerrainKit/Data/HIKARI_TerrainDataFile.h"
#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"
#include "TerrainKit/Data/HIKARI_TerrainDataScatter.h"
#include "TerrainKit/Data/HIKARI_TerrainDataValleys.h"
#include "TerrainKit/Data/HIKARI_TerrainDataStamp.h"

using namespace HIKARI::TERRAIN::DATA;
using HIKARI::MATH::Vec2;

namespace {

int gFailures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        ++gFailures;
        std::printf("FAILED line %d: %s\n", line, expression);
    }
}
#define CHECK(condition) Check((condition), #condition, __LINE__)

std::filesystem::path OutputDirectory() {
    const std::filesystem::path path = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "terrain-data";
    std::filesystem::create_directories(path);
    return path;
}

double Milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// 1 m 間隔、64 m タイルの小さな格子。
TerrainGrid SmallGrid() {
    TerrainGrid grid{};
    grid.tileSizeMeters = 64.0f;
    grid.tileCells = 64u;
    grid.heightMin = -256.0f;
    grid.heightRange = 1024.0f;
    return grid;
}

HeightLayer NoiseLayer(float amplitude) {
    HeightLayer layer{};
    layer.name = "Base";
    layer.kind = HeightLayerKind::Noise;
    layer.noise.featureSizeMeters = 48.0f;
    layer.noise.amplitudeMeters = amplitude;
    layer.noise.octaves = 4u;
    return layer;
}

HeightLayer SculptLayer(HeightBlendMode blend = HeightBlendMode::Add) {
    HeightLayer layer{};
    layer.name = "Sculpt";
    layer.kind = HeightLayerKind::Sculpt;
    layer.blend = blend;
    return layer;
}

float Composite(const TerrainData& data, int64_t x, int64_t z) {
    HeightStackEvaluator evaluator(data);
    std::vector<float> out;
    evaluator.EvaluateRect(SampleRect{x, z, 1u, 1u}, out);
    return out.empty() ? 0.0f : out[0];
}

void TestControl() {
    ControlTexel texel{};
    texel.baseMaterial = 31u;
    texel.overlayMaterial = 7u;
    texel.blend = 200u;
    texel.uvRotation = 15u;
    texel.uvScale = 7u;
    texel.hole = true;
    texel.handPainted = true;
    texel.navigationExcluded = true;
    texel.wetness = 15u;
    uint32_t packed = 0u;
    CHECK(PackControlTexel(texel, packed));
    CHECK(UnpackControlTexel(packed) == texel);
    CHECK(UnpackControlTexel(0u) == ControlTexel{});
    // 背景の計算の結果は、計算中の編集（穴・手塗り）を消さない。
    ControlTexel computedTexel{};
    computedTexel.baseMaterial = 4u;
    uint32_t computedValue = 0u;
    CHECK(PackControlTexel(computedTexel, computedValue));
    ControlTexel holeTexel{};
    holeTexel.hole = true;
    uint32_t holeValue = 0u;
    CHECK(PackControlTexel(holeTexel, holeValue));
    const ControlTexel merged = UnpackControlTexel(MergeComputedControl(computedValue, holeValue));
    CHECK(merged.baseMaterial == 4u && merged.hole);
    ControlTexel handTexel{};
    handTexel.baseMaterial = 9u;
    handTexel.handPainted = true;
    uint32_t handValue = 0u;
    CHECK(PackControlTexel(handTexel, handValue));
    CHECK(MergeComputedControl(computedValue, handValue) == handValue);
    CHECK(MergeComputedControl(handValue, computedValue) == computedValue);
    texel.baseMaterial = 32u;
    CHECK(!PackControlTexel(texel, packed));
    // 主材質を塗れば重ねが薄まり、別の材質を塗れば重ねになる。満たせば主材質へ繰り上がる。
    ControlTexel painted = PaintControlMaterial(ControlTexel{}, 3u, 0.5f);
    CHECK(painted.baseMaterial == 0u && painted.overlayMaterial == 3u && painted.blend == 128u && painted.handPainted);
    painted = PaintControlMaterial(painted, 3u, 1.0f);
    CHECK(painted.baseMaterial == 3u && painted.blend == 0u);
}

void TestGrid() {
    const TerrainGrid grid = SmallGrid();
    std::string error;
    CHECK(ValidateTerrainGrid(grid, &error));
    TerrainGrid bad = grid;
    bad.tileCells = 100u;
    CHECK(!ValidateTerrainGrid(bad, &error));
    CHECK(TileOfWorld(grid, -0.5, 63.9) == (TileCoord{-1, 0}));
    CHECK(TileOfWorld(grid, 64.0, -64.0) == (TileCoord{1, -1}));
    const float step = HeightQuantizationStep(grid);
    for (float h : {-256.0f, -1.2345f, 0.0f, 333.3f, 767.99f}) {
        CHECK(std::fabs(DequantizeHeight(grid, QuantizeHeight(grid, h)) - h) <= step * 0.5f + 1.0e-4f);
    }
    CHECK(QuantizeHeight(grid, 99999.0f) == 65535u && QuantizeHeight(grid, -99999.0f) == 0u);
    // 縁の標本 64 はタイル 0 と 1 の両方が持つ。
    std::vector<TileCoord> tiles;
    ForEachTileHolding(grid, SampleRect{64, 10, 1u, 1u}, [&](TileCoord t) { tiles.push_back(t); });
    CHECK(tiles.size() == 2u && tiles[0] == (TileCoord{0, 0}) && tiles[1] == (TileCoord{1, 0}));
    tiles.clear();
    ForEachTileHolding(grid, SampleRect{0, 0, 1u, 1u}, [&](TileCoord t) { tiles.push_back(t); });
    CHECK(tiles.size() == 4u);
    CHECK(TerrainCellFileName(TileCoord{3, -12}) == "x+003_z-012.terrain.bin");
}

void TestRasterEdges() {
    const TerrainGrid grid = SmallGrid();
    TiledRaster<float> raster(grid, 0.0f);
    const SampleRect rect{-2, -2, 5u, 5u};
    std::vector<float> values(rect.Count());
    std::iota(values.begin(), values.end(), 1.0f);
    raster.WriteRect(grid, rect, values);
    CHECK(raster.TileCount() == 4u);
    std::vector<float> read;
    raster.ReadRect(grid, rect, read);
    CHECK(read == values);
    // 原点の標本は 4 タイルの角。どのタイルにも同じ値が入る。
    const float origin = values[2u * 5u + 2u];
    CHECK(raster.Find({0, 0})->values[0] == origin);
    CHECK(raster.Find({-1, -1})->values[64u * 65u + 64u] == origin);
    CHECK(raster.Find({-1, 0})->values[64u] == origin);
    CHECK(raster.Get(0, 0) == origin && raster.Get(500, 500) == 0.0f);
}

void TestStackJson() {
    TerrainData data(SmallGrid());
    const uint32_t noise = data.AddLayer(NoiseLayer(20.0f));
    HeightLayer image{};
    image.kind = HeightLayerKind::Image;
    image.image.source = "Terrain/island.exr";
    image.blend = HeightBlendMode::Max;
    const uint32_t imageId = data.AddLayer(image);
    const uint32_t sculpt = data.AddLayer(SculptLayer(), 0u);
    CHECK(noise == 1u && imageId == 2u && sculpt == 3u);
    CHECK(data.LayerIndex(sculpt) == 0u);
    const nlohmann::json json = WriteHeightLayerStackJson(data.Stack());
    HeightLayerStack read{};
    std::string error;
    CHECK(ReadHeightLayerStackJson(json, read, error));
    CHECK(read == data.Stack());
    nlohmann::json badKind = json;
    badKind["layers"][0]["kind"] = "voxel";
    CHECK(!ReadHeightLayerStackJson(badKind, read, error));
    nlohmann::json duplicate = json;
    duplicate["layers"][1]["id"] = duplicate["layers"][0]["id"];
    CHECK(!ReadHeightLayerStackJson(duplicate, read, error));
    nlohmann::json badGrid = json;
    badGrid["grid"]["tileCells"] = 100;
    CHECK(!ReadHeightLayerStackJson(badGrid, read, error));
}

void TestEvaluation() {
    TerrainData data(SmallGrid());
    const uint32_t noise = data.AddLayer(NoiseLayer(30.0f));
    HeightStackEvaluator evaluator(data);
    CHECK(evaluator.Valid());
    std::vector<float> left, right, wide;
    CHECK(evaluator.EvaluateTile({0, 0}, left));
    CHECK(evaluator.EvaluateTile({1, 0}, right));
    // 隣り合うタイルの縁は同じ標本なので同じ値（継ぎ目なし）。
    bool seamless = true;
    for (uint32_t z = 0u; z <= 64u; ++z) {
        seamless = seamless && left[z * 65u + 64u] == right[z * 65u];
    }
    CHECK(seamless);
    CHECK(evaluator.EvaluateRect(SampleRect{0, 0, 129u, 65u}, wide));
    CHECK(wide[10u * 129u + 100u] == right[10u * 65u + 36u]);
    float lowest = 1.0e9f, highest = -1.0e9f;
    for (float h : left) {
        lowest = (std::min)(lowest, h);
        highest = (std::max)(highest, h);
    }
    std::printf("  noise tile: %.3f..%.3f m (amplitude 30 m), first samples %.4f %.4f %.4f\n", lowest, highest, left[0],
                left[1], left[65]);
    CHECK(highest - lowest > 5.0f && highest < 35.0f && lowest > -35.0f);
    // 同じ種なら同じ値、種を変えれば違う値。
    HeightStackEvaluator again(data);
    std::vector<float> repeat;
    again.EvaluateTile({0, 0}, repeat);
    CHECK(repeat == left);
    HeightLayer reseeded = *data.FindLayer(noise);
    reseeded.noise.seed = 7;
    CHECK(data.SetLayer(reseeded));
    HeightStackEvaluator reseededEvaluator(data);
    reseededEvaluator.EvaluateTile({0, 0}, repeat);
    CHECK(repeat != left);
    // Replace 50%：彫刻の値 10 と下の噪声の中間。
    const uint32_t sculpt = data.AddLayer(SculptLayer(HeightBlendMode::Replace));
    std::vector<float> ten(65u * 65u, 10.0f);
    data.LayerValues(sculpt)->WriteRect(data.Grid(), TileSampleRect(data.Grid(), {0, 0}), ten);
    HeightLayer half = *data.FindLayer(sculpt);
    half.opacity = 0.5f;
    CHECK(data.SetLayer(half));
    HeightStackEvaluator blended(data);
    std::vector<float> below, mixed;
    blended.EvaluateRect(SampleRect{5, 5, 1u, 1u}, below, nullptr, noise);
    blended.EvaluateRect(SampleRect{5, 5, 1u, 1u}, mixed);
    CHECK(std::fabs(mixed[0] - (below[0] + 10.0f) * 0.5f) < 1.0e-4f);
    // マスク 0 の所は層が効かない。
    HeightLayer masked = half;
    masked.masked = true;
    CHECK(data.SetLayer(masked));
    std::vector<float> zero(1u, 0.0f);
    data.LayerMask(sculpt)->WriteRect(data.Grid(), SampleRect{5, 5, 1u, 1u}, zero);
    HeightStackEvaluator maskedEvaluator(data);
    maskedEvaluator.EvaluateRect(SampleRect{5, 5, 1u, 1u}, mixed);
    CHECK(std::fabs(mixed[0] - below[0]) < 1.0e-4f);
    // 壊れたノード木は評価器が無効になる。
    HeightLayer broken = *data.FindLayer(noise);
    broken.noise.encodedNodeTree = "not-a-node-tree";
    CHECK(data.SetLayer(broken));
    HeightStackEvaluator brokenEvaluator(data);
    CHECK(!brokenEvaluator.Valid());
}

void TestBrushes() {
    TerrainData data(SmallGrid());
    const uint32_t sculpt = data.AddLayer(SculptLayer());
    HeightBrushSettings raise{};
    raise.op = HeightBrushOp::Raise;
    raise.radiusMeters = 8.0f;
    raise.strength = 2.0f;
    raise.hardness = 0.25f;
    const Vec2 center{32.0f, 32.0f};
    BrushEditResult result{};
    std::string error;
    CHECK(ApplyHeightBrush(data, sculpt, raise, std::span(&center, 1u), nullptr, result, error));
    CHECK(result.dabs == 1u);
    CHECK(std::fabs(data.LayerValues(sculpt)->Get(32, 32) - 2.0f) < 1.0e-5f);
    CHECK(data.LayerValues(sculpt)->Get(32, 41) == 0.0f);
    CHECK(data.LayerValues(sculpt)->Get(36, 32) > 0.0f && data.LayerValues(sculpt)->Get(36, 32) < 2.0f);
    // タイルの縁をまたぐ筆跡は両方のタイルを作り、縁の値が一致する。
    const Vec2 path[] = {{58.0f, 20.0f}, {70.0f, 20.0f}};
    CHECK(ApplyHeightBrush(data, sculpt, raise, path, nullptr, result, error));
    CHECK(result.dabs >= 5u);
    const TileRaster<float>* a = data.LayerValues(sculpt)->Find({0, 0});
    const TileRaster<float>* b = data.LayerValues(sculpt)->Find({1, 0});
    CHECK(a != nullptr && b != nullptr);
    bool shared = a != nullptr && b != nullptr;
    for (uint32_t z = 0u; shared && z <= 64u; ++z) {
        shared = a->values[z * 65u + 64u] == b->values[z * 65u];
    }
    CHECK(shared && b->values[20u * 65u] > 0.0f);
    // 整平は合成後の高さを目標へ寄せる（下に噪声がある時も）。
    TerrainData flat(SmallGrid());
    flat.AddLayer(NoiseLayer(10.0f));
    const uint32_t flatSculpt = flat.AddLayer(SculptLayer());
    HeightBrushSettings flatten{};
    flatten.op = HeightBrushOp::Flatten;
    flatten.radiusMeters = 6.0f;
    flatten.strength = 1.0f;
    flatten.hardness = 0.8f;
    flatten.targetHeightMeters = 25.0f;
    const Vec2 flatCenter{20.0f, 20.0f};
    CHECK(ApplyHeightBrush(flat, flatSculpt, flatten, std::span(&flatCenter, 1u), nullptr, result, error));
    CHECK(std::fabs(Composite(flat, 20, 20) - 25.0f) < 1.0e-3f);
    // 平滑は凸凹を減らす。
    HeightBrushSettings noise{};
    noise.op = HeightBrushOp::Noise;
    noise.radiusMeters = 10.0f;
    noise.strength = 3.0f;
    noise.hardness = 1.0f;
    noise.noiseScaleMeters = 1.5f;
    const Vec2 noiseCenter{40.0f, 40.0f};
    CHECK(ApplyHeightBrush(flat, flatSculpt, noise, std::span(&noiseCenter, 1u), nullptr, result, error));
    const auto roughness = [&](const TerrainData& terrain) {
        HeightStackEvaluator evaluator(terrain);
        std::vector<float> h;
        evaluator.EvaluateRect(SampleRect{35, 35, 11u, 11u}, h);
        float sum = 0.0f;
        for (uint32_t z = 1u; z < 10u; ++z) {
            for (uint32_t x = 1u; x < 10u; ++x) {
                const float laplacian = 4.0f * h[z * 11u + x] - h[z * 11u + x - 1u] - h[z * 11u + x + 1u] -
                                        h[(z - 1u) * 11u + x] - h[(z + 1u) * 11u + x];
                sum += std::fabs(laplacian);
            }
        }
        return sum;
    };
    const float rough = roughness(flat);
    HeightBrushSettings smooth{};
    smooth.op = HeightBrushOp::Smooth;
    smooth.radiusMeters = 10.0f;
    smooth.strength = 1.0f;
    smooth.hardness = 1.0f;
    for (int pass = 0; pass < 4; ++pass) {
        CHECK(ApplyHeightBrush(flat, flatSculpt, smooth, std::span(&noiseCenter, 1u), nullptr, result, error));
    }
    CHECK(roughness(flat) < rough * 0.5f);
    // 減算の層でも「上げる」は合成後の高さを上げる。
    TerrainData subtract(SmallGrid());
    const uint32_t minus = subtract.AddLayer(SculptLayer(HeightBlendMode::Subtract));
    CHECK(ApplyHeightBrush(subtract, minus, raise, std::span(&center, 1u), nullptr, result, error));
    CHECK(Composite(subtract, 32, 32) > 1.9f);
    // 手彫りでない層や空の折れ線は拒否する。
    TerrainData noSculpt(SmallGrid());
    const uint32_t noiseOnly = noSculpt.AddLayer(NoiseLayer(5.0f));
    CHECK(!ApplyHeightBrush(noSculpt, noiseOnly, raise, std::span(&center, 1u), nullptr, result, error));
    CHECK(!ApplyHeightBrush(data, sculpt, raise, std::span<const Vec2>{}, nullptr, result, error));
    // 材質と穴。
    ControlBrushSettings paint{};
    paint.material = 4u;
    paint.radiusMeters = 4.0f;
    paint.strength = 1.0f;
    paint.hardness = 1.0f;
    CHECK(ApplyControlBrush(data, paint, std::span(&center, 1u), nullptr, result, error));
    CHECK(UnpackControlTexel(data.Control().Get(32, 32)).baseMaterial == 4u);
    CHECK(UnpackControlTexel(data.Control().Get(32, 32)).handPainted);
    paint.op = ControlBrushOp::Hole;
    CHECK(ApplyControlBrush(data, paint, std::span(&center, 1u), nullptr, result, error));
    CHECK(UnpackControlTexel(data.Control().Get(32, 32)).hole);
    CHECK(!UnpackControlTexel(data.Control().Get(32, 40)).hole);
}

// 坂・段丘・範囲の制限、印章、層のマスク。
void TestBrushVariantsStampsAndMasks() {
    std::string error;
    BrushEditResult result{};
    // 坂：始点の高さから終点の高さへ一定に上る（傾きの指定は始点からの高さを決める）。
    {
        TerrainData data(SmallGrid());
        const uint32_t sculpt = data.AddLayer(SculptLayer());
        HeightBrushSettings ramp{};
        ramp.op = HeightBrushOp::Ramp;
        ramp.radiusMeters = 6.0f;
        ramp.strength = 1.0f;
        ramp.hardness = 0.9f;
        ramp.spacing = 0.1f;
        ramp.rampSlopeDegrees = 45.0f;
        const Vec2 path[] = {{10.0f, 32.0f}, {50.0f, 32.0f}};
        CHECK(ApplyHeightBrush(data, sculpt, ramp, path, nullptr, result, error));
        CHECK(std::fabs(Composite(data, 30, 32) - 20.0f) < 0.05f);
        CHECK(std::fabs(Composite(data, 50, 32) - 40.0f) < 0.05f);
    }
    // 段丘：合成後の高さを段へ寄せる。
    {
        TerrainData data(SmallGrid());
        const uint32_t sculpt = data.AddLayer(SculptLayer());
        HeightBrushSettings raise{};
        raise.op = HeightBrushOp::Raise;
        raise.radiusMeters = 20.0f;
        raise.strength = 13.0f;
        raise.hardness = 1.0f;
        const Vec2 center{32.0f, 32.0f};
        CHECK(ApplyHeightBrush(data, sculpt, raise, std::span(&center, 1u), nullptr, result, error));
        HeightBrushSettings terrace{};
        terrace.op = HeightBrushOp::Terrace;
        terrace.radiusMeters = 8.0f;
        terrace.strength = 1.0f;
        terrace.hardness = 1.0f;
        terrace.terraceStepMeters = 10.0f;
        CHECK(ApplyHeightBrush(data, sculpt, terrace, std::span(&center, 1u), nullptr, result, error));
        CHECK(std::fabs(Composite(data, 32, 32) - 10.0f) < 0.01f);
    }
    // 高さの範囲の外では筆が効かない。
    {
        TerrainData data(SmallGrid());
        const uint32_t sculpt = data.AddLayer(SculptLayer());
        HeightBrushSettings raise{};
        raise.op = HeightBrushOp::Raise;
        raise.radiusMeters = 8.0f;
        raise.strength = 5.0f;
        raise.limitMinHeightMeters = 100.0f;
        const Vec2 center{32.0f, 32.0f};
        CHECK(ApplyHeightBrush(data, sculpt, raise, std::span(&center, 1u), nullptr, result, error));
        CHECK(Composite(data, 32, 32) == 0.0f);
    }
    // 印章：縁は 0、撤销で戻る、回しても中心は同じ。
    {
        TerrainData data(SmallGrid());
        const uint32_t sculpt = data.AddLayer(SculptLayer());
        HeightImage image{};
        CHECK(MakeBuiltinStamp("hill", 65u, image));
        CHECK(!MakeBuiltinStamp("nothing", 65u, image));
        CHECK(MakeBuiltinStamp("hill", 65u, image));
        StampSettings stamp{};
        stamp.center = Vec2{32.0f, 32.0f};
        stamp.radiusMeters = 20.0f;
        stamp.heightMeters = 10.0f;
        stamp.rotationDegrees = 45.0f;
        TerrainEditRecorder recorder(data);
        CHECK(ApplyStamp(data, sculpt, image, stamp, &recorder, result, error));
        CHECK(Composite(data, 32, 32) > 8.0f);
        CHECK(Composite(data, 32, 52) == 0.0f);
        const TerrainEdit edit = recorder.Finish("stamp");
        CHECK(UndoTerrainEdit(data, edit, error));
        CHECK(Composite(data, 32, 32) == 0.0f);
    }
    // 層のマスク：消すと下だけになり、書き出しと読み込みで同じ値へ戻る。
    {
        TerrainData data(SmallGrid());
        const uint32_t sculpt = data.AddLayer(SculptLayer());
        HeightBrushSettings raise{};
        raise.op = HeightBrushOp::Raise;
        raise.radiusMeters = 30.0f;
        raise.strength = 4.0f;
        raise.hardness = 1.0f;
        const Vec2 center{32.0f, 32.0f};
        CHECK(ApplyHeightBrush(data, sculpt, raise, std::span(&center, 1u), nullptr, result, error));
        MaskBrushSettings erase{};
        erase.op = MaskBrushOp::Erase;
        erase.radiusMeters = 5.0f;
        erase.strength = 1.0f;
        erase.hardness = 1.0f;
        CHECK(ApplyMaskBrush(data, sculpt, erase, std::span(&center, 1u), nullptr, result, error));
        CHECK(data.FindLayer(sculpt)->masked);
        CHECK(Composite(data, 32, 32) == 0.0f);
        CHECK(std::fabs(Composite(data, 32, 45) - 4.0f) < 1.0e-4f);
        HeightImage exported{};
        const SampleRect rect{0, 0, 65u, 65u};
        CHECK(ExportLayerMask(data, sculpt, rect, exported, error));
        CHECK(exported.values[32u * 65u + 32u] == 0.0f);
        HeightImage solid{};
        solid.width = 2u;
        solid.height = 2u;
        solid.values.assign(4u, 1.0f);
        CHECK(ImportLayerMask(data, sculpt, solid, Vec2{0.0f, 0.0f}, Vec2{64.0f, 64.0f}, nullptr, result, error));
        CHECK(std::fabs(Composite(data, 32, 32) - 4.0f) < 1.0e-4f);
    }
}

// 自動の材質規則：傾きと高さで選ばれ、手塗りは残り、穴の印も残る。
void TestSurfaceRules() {
    const uint32_t samples = 17u;
    const uint32_t margin = kSurfaceRuleHeightMargin;
    const uint32_t stride = samples + 2u * margin;
    // x が大きいほど急な坂、高さは z で増やさない。
    std::vector<float> heights(static_cast<size_t>(stride) * stride, 0.0f);
    for (uint32_t z = 0u; z < stride; ++z) {
        for (uint32_t x = 0u; x < stride; ++x) {
            const float column = static_cast<float>(x) - static_cast<float>(margin);
            heights[static_cast<size_t>(z) * stride + x] = column <= 8.0f ? 0.0f : (column - 8.0f) * 2.0f;
        }
    }
    SurfaceRuleSet set{};
    SurfaceRule rock{};
    rock.surface = 2u;
    rock.minSlopeDegrees = 45.0f;
    rock.slopeFadeDegrees = 2.0f;
    rock.noiseAmount = 0.0f;
    set.rules.push_back(rock);
    SurfaceRuleTile tile{};
    tile.samples = samples;
    tile.spacingMeters = 1.0f;
    tile.heights = heights;
    std::vector<uint32_t> control(static_cast<size_t>(samples) * samples, 0u);
    ControlTexel painted{};
    painted.baseMaterial = 7u;
    painted.handPainted = true;
    CHECK(PackControlTexel(painted, control[16u]));
    ControlTexel holed{};
    holed.hole = true;
    CHECK(PackControlTexel(holed, control[static_cast<size_t>(8u) * samples + 14u]));
    EvaluateSurfaceRules(set, tile, control);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(8u) * samples + 2u]).baseMaterial == 0u);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(8u) * samples + 13u]).baseMaterial == 2u);
    CHECK(UnpackControlTexel(control[16u]).baseMaterial == 7u);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(8u) * samples + 14u]).hole);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(8u) * samples + 14u]).baseMaterial == 2u);
    // 島の割合：覆う割合に応じて面積が増え、0 なら何も塗らない。
    SurfaceRuleSet patches{};
    SurfaceRule island{};
    island.surface = 5u;
    island.noiseAmount = 0.0f;
    island.coverage = 0.0f;
    patches.rules.push_back(island);
    std::vector<float> flat(heights.size(), 0.0f);
    tile.heights = flat;
    std::vector<uint32_t> patched(control.size(), 0u);
    EvaluateSurfaceRules(patches, tile, patched);
    CHECK(std::all_of(patched.begin(), patched.end(),
                      [](uint32_t value) { return UnpackControlTexel(value).baseMaterial == 0u; }));
    // 色の図は世界座標で決まるので、隣の瓦片と境で揃う。
    SurfaceRuleTile left = tile;
    SurfaceRuleTile right = tile;
    right.firstSampleX = samples - 1u;
    const SurfaceMapStyle style{{{{0.2f, 0.2f, 0.2f}, {0.3f, 0.2f, 0.1f}, {0.4f, 0.3f, 0.2f}}}, 12.0f, 0.5f};
    const std::vector<SurfaceMapStyle> styles(1u, style);
    const std::vector<uint32_t> plain(control.size(), 0u);
    std::vector<float> colorLeft{};
    std::vector<float> colorRight{};
    BakeSurfaceColorMap(patches, left, plain, styles, 9u, colorLeft);
    BakeSurfaceColorMap(patches, right, plain, styles, 9u, colorRight);
    CHECK(colorLeft.size() == 324u);
    for (uint32_t row = 0u; row < 9u; ++row) {
        CHECK(std::abs(colorLeft[(row * 9u + 8u) * 4u] - colorRight[row * 9u * 4u]) < 1.0e-6f);
    }
    CHECK(std::all_of(colorLeft.begin(), colorLeft.end(), [](float value) { return std::isfinite(value); }));
    // 規則の記述は JSON で往復する。
    nlohmann::json json{};
    const SurfaceRuleSet defaults = DefaultSurfaceRules();
    HIKARI::JSONFIELDS::Write(json, defaults, kSurfaceRuleSetFields);
    SurfaceRuleSet read{};
    HIKARI::JSONFIELDS::Read(json, read, kSurfaceRuleSetFields);
    CHECK(read == defaults);
}

// 地形の読み取りは谷筋を湿った谷底と見て、谷の彫り込みは水の流れる道を作る。
// 実例集の個体の詰め方とファイルの往復。壊れたファイルは読まない。
// 1 m 間隔の瓦片の入力を、世界座標の高さの式から作る。
template <class Height>
std::vector<float> ScatterHeights(uint32_t samples, int64_t firstX, int64_t firstZ, Height height) {
    const int64_t margin = kSurfaceRuleHeightMargin;
    const int64_t stride = static_cast<int64_t>(samples) + 2 * margin;
    std::vector<float> heights(static_cast<size_t>(stride * stride));
    for (int64_t row = 0; row < stride; ++row) {
        for (int64_t column = 0; column < stride; ++column) {
            heights[static_cast<size_t>(row * stride + column)] =
                height(static_cast<float>(firstX + column - margin), static_cast<float>(firstZ + row - margin));
        }
    }
    return heights;
}

void TestScatter() {
    const auto flat = [](float, float) { return 10.0f; };
    const auto makeTile = [](const std::vector<float>& heights, uint32_t samples, int64_t firstX, int64_t firstZ) {
        ScatterTile tile{};
        tile.surface.samples = samples;
        tile.surface.spacingMeters = 1.0f;
        tile.surface.firstSampleX = firstX;
        tile.surface.firstSampleZ = firstZ;
        tile.surface.heights = heights;
        return tile;
    };
    ScatterRuleSet set{};
    ScatterRule rule{};
    rule.name = "Trees";
    rule.instanceSet = "Trees";
    rule.densityPer100 = 10.0f;
    rule.footprintMeters = 0.1f;
    rule.clusterAmount = 0.0f;
    set.rules.push_back(rule);
    const std::vector<float> flatHeights = ScatterHeights(65u, 0, 0, flat);
    const ScatterTile flatTile = makeTile(flatHeights, 65u, 0, 0);

    // 同じ入力は同じ結果で、数は密度どおり。
    std::vector<ScatterPoint> first{};
    std::vector<ScatterPoint> second{};
    EvaluateScatter(set, flatTile, first);
    EvaluateScatter(set, flatTile, second);
    CHECK(first.size() == second.size());
    CHECK(std::equal(first.begin(), first.end(), second.begin(),
                     [](const ScatterPoint& a, const ScatterPoint& b) { return a.seed == b.seed && a.x == b.x; }));
    CHECK(first.size() > 350u && first.size() < 470u); // 64 x 64 m x 0.1 = 410
    for (const ScatterPoint& point : first) {
        CHECK(point.x >= 0.0f && point.x < 64.0f && point.z >= 0.0f && point.z < 64.0f && point.y == 10.0f);
        CHECK(point.scale >= rule.scaleMin && point.scale <= rule.scaleMax);
    }

    // 瓦片の境：二枚に分けても一枚で解いても同じ個体が出る。
    const std::vector<float> wideHeights = ScatterHeights(129u, 0, 0, flat);
    std::vector<ScatterPoint> wide{};
    EvaluateScatter(set, makeTile(wideHeights, 129u, 0, 0), wide);
    const std::vector<float> rightHeights = ScatterHeights(65u, 64, 0, flat);
    std::vector<ScatterPoint> right{};
    EvaluateScatter(set, makeTile(rightHeights, 65u, 64, 0), right);
    std::vector<uint32_t> joined{};
    for (const ScatterPoint& point : first) {
        joined.push_back(point.seed);
    }
    for (const ScatterPoint& point : right) {
        joined.push_back(point.seed);
    }
    std::vector<uint32_t> whole{};
    for (const ScatterPoint& point : wide) {
        if (point.z < 64.0f) {
            whole.push_back(point.seed);
        }
    }
    std::sort(joined.begin(), joined.end());
    std::sort(whole.begin(), whole.end());
    CHECK(joined == whole);

    // 坡度と高さの条件。
    const std::vector<float> steep = ScatterHeights(65u, 0, 0, [](float x, float) { return x * 2.0f; }); // 63 度
    std::vector<ScatterPoint> points{};
    EvaluateScatter(set, makeTile(steep, 65u, 0, 0), points);
    CHECK(points.empty());
    ScatterRuleSet low = set;
    low.rules[0].maxHeightMeters = 0.0f;
    low.rules[0].heightFadeMeters = 1.0f;
    EvaluateScatter(low, flatTile, points);
    CHECK(points.empty());

    // 足跡：後の規則は前の規則の足跡の中に置かない。
    ScatterRuleSet layered{};
    ScatterRule big = rule;
    big.seed = 5u;
    big.instanceSet = "Big";
    big.densityPer100 = 2.0f;
    big.footprintMeters = 4.0f;
    ScatterRule small = rule;
    small.seed = 6u;
    small.densityPer100 = 50.0f;
    small.footprintMeters = 0.5f;
    layered.rules = {big, small};
    EvaluateScatter(layered, flatTile, points);
    size_t bigCount = 0u;
    size_t smallCount = 0u;
    for (const ScatterPoint& a : points) {
        if (a.rule != 0u) {
            ++smallCount;
            continue;
        }
        ++bigCount;
        for (const ScatterPoint& b : points) {
            if (&a != &b) {
                const float dx = a.x - b.x;
                const float dz = a.z - b.z;
                CHECK(dx * dx + dz * dz >= 3.0f * 3.0f);
            }
        }
    }
    CHECK(bigCount > 10u && smallCount > 100u);

    // 手で置いた個体の周りは空ける。
    const ScatterReserved reserved{32.0f, 32.0f, 10.0f};
    ScatterTile aroundHand = flatTile;
    aroundHand.reserved = std::span(&reserved, 1u);
    EvaluateScatter(set, aroundHand, points);
    CHECK(!points.empty());
    for (const ScatterPoint& point : points) {
        const float dx = point.x - 32.0f;
        const float dz = point.z - 32.0f;
        CHECK(dx * dx + dz * dz >= 9.0f * 9.0f);
    }

    // マスク：0 を塗った半分には出ない。
    std::vector<std::vector<float>> masks(1u, std::vector<float>(65u * 65u, 1.0f));
    for (uint32_t z = 0u; z < 65u; ++z) {
        for (uint32_t x = 0u; x < 32u; ++x) {
            masks[0][static_cast<size_t>(z) * 65u + x] = 0.0f;
        }
    }
    ScatterTile masked = flatTile;
    masked.ruleMasks = masks;
    EvaluateScatter(set, masked, points);
    CHECK(!points.empty());
    for (const ScatterPoint& point : points) {
        CHECK(point.x >= 31.0f);
    }

    // 地表材質：許さない枠の上には出ない。
    const std::vector<uint32_t> control(65u * 65u, 0u);
    ScatterRuleSet surfaceOnly = set;
    surfaceOnly.rules[0].surfaceMask = 1u << 3u;
    ScatterTile painted = flatTile;
    painted.control = control;
    EvaluateScatter(surfaceOnly, painted, points);
    CHECK(points.empty());

    // 曲線：面の規則は避け、沿線の規則は間隔どおり両側に並ぶ。
    SplineLayerSettings road{};
    road.points = {{{32.0f, 10.0f, -60.0f}, 8.0f}, {{32.0f, 10.0f, 32.0f}, 8.0f}, {{32.0f, 10.0f, 124.0f}, 8.0f}};
    road.edgeMeters = 2.0f;
    const std::vector<PreparedSpline> splines{PrepareSpline(road)};
    const std::vector<uint32_t> layers{42u};
    ScatterTile withRoad = flatTile;
    withRoad.splines = splines;
    withRoad.splineLayers = layers;
    EvaluateScatter(set, withRoad, points);
    CHECK(!points.empty());
    for (const ScatterPoint& point : points) {
        CHECK(std::abs(point.x - 32.0f) >= 6.0f); // 半幅 4 + 路肩 2 + 余白
    }
    ScatterRuleSet fence{};
    ScatterLineRule line{};
    line.instanceSet = "Fence";
    line.splineLayer = 42u;
    line.spacingMeters = 4.0f;
    line.offsetMeters = 1.0f;
    fence.lines.push_back(line);
    EvaluateScatter(fence, withRoad, points);
    size_t rightSide = 0u;
    size_t leftSide = 0u;
    for (const ScatterPoint& point : points) {
        CHECK(point.line);
        rightSide += point.x > 32.0f ? 1u : 0u;
        leftSide += point.x < 32.0f ? 1u : 0u;
        CHECK(std::abs(std::abs(point.x - 32.0f) - 5.0f) < 0.1f);
        // 進む向きは +Z なので回転は無い。
        CHECK(std::abs(point.rotation[1]) < 0.01f && std::abs(point.rotation[3]) > 0.99f);
    }
    CHECK(rightSide == leftSide && rightSide >= 15u && rightSide <= 17u); // 64 m / 4 m

    // 地面に沿わせる回転：上向きが法線へ向く。
    float rotation[4]{};
    GroundAlignedRotation(1.3f, 0.5f, -0.25f, 1.0f, rotation);
    const float upX = 2.0f * (rotation[0] * rotation[1] - rotation[3] * rotation[2]);
    const float upY = 1.0f - 2.0f * (rotation[0] * rotation[0] + rotation[2] * rotation[2]);
    const float upZ = 2.0f * (rotation[1] * rotation[2] + rotation[3] * rotation[0]);
    const float inverse = 1.0f / std::sqrt(0.5f * 0.5f + 0.25f * 0.25f + 1.0f);
    CHECK(std::abs(upX + 0.5f * inverse) < 0.001f);
    CHECK(std::abs(upY - inverse) < 0.001f);
    CHECK(std::abs(upZ - 0.25f * inverse) < 0.001f);

    // 既定の規則は JSON を往復する。
    nlohmann::json json{};
    const ScatterRuleSet defaults = DefaultScatterRules();
    CHECK(defaults.rules.size() == 3u);
    HIKARI::JSONFIELDS::Write(json, defaults, kScatterRuleSetFields);
    ScatterRuleSet read{};
    HIKARI::JSONFIELDS::Read(json, read, kScatterRuleSetFields);
    CHECK(read == defaults);
}

void TestWorldInstanceFile() {
    using namespace HIKARI;
    const GameProjectWorldInstance instance =
        WORLDINSTANCE::Encode({12.5f, 3.0f, -7.25f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f}, {1.5f, 0.75f, 2.0f}, 77u,
                              GameProjectWorldInstanceFlags::HandPlaced);
    CHECK(std::abs(WORLDINSTANCE::DecodeSnorm16(instance.rotation[1]) - 0.70710678f) < 1.0e-4f);
    CHECK(WORLDINSTANCE::DecodeHalf(instance.scale[0]) == 1.5f);
    CHECK(WORLDINSTANCE::DecodeHalf(instance.scale[1]) == 0.75f);
    CHECK(WORLDINSTANCE::DecodeHalf(WORLDINSTANCE::EncodeHalf(0.0f)) == 0.0f);
    CHECK(WORLDINSTANCE::DecodeHalf(WORLDINSTANCE::EncodeHalf(1.0e9f)) > 60000.0f);
    CHECK(instance.seed == 77u);

    const std::filesystem::path directory =
        std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "terrain_contracts_instances";
    std::error_code ec{};
    std::filesystem::create_directories(directory, ec);
    const WorldCellCoord cell{-3, 5};
    const std::filesystem::path path = MakeWorldInstanceCellPath(directory, cell);
    WorldCellCoord parsed{};
    CHECK(ParseWorldInstanceCellFileName(path.filename().string(), parsed));
    CHECK(parsed == cell);
    CHECK(!ParseWorldInstanceCellFileName("x+000_z+000.objects.json", parsed));
    const std::vector<GameProjectWorldInstance> first(2000u, instance);
    const std::vector<GameProjectWorldInstance> second(3u, instance);
    const std::array<WorldInstanceCellPart, 2> parts{WorldInstanceCellPart{11u, first},
                                                     WorldInstanceCellPart{22u, second}};
    std::string error{};
    CHECK(WriteWorldInstanceCellFile(path, cell, parts, error));
    std::vector<WorldInstanceCellRead> read{};
    CHECK(ReadWorldInstanceCellFile(path, cell, read, error));
    CHECK(read.size() == 2u && read[0].setId == 11u && read[0].instances.size() == 2000u && read[1].setId == 22u &&
          read[1].instances.size() == 3u);
    CHECK(std::memcmp(read[1].instances.data(), second.data(), sizeof(GameProjectWorldInstance) * 3u) == 0);
    CHECK(!ReadWorldInstanceCellFile(path, WorldCellCoord{0, 0}, read, error));
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        const char broken = 0x7F;
        file.write(&broken, 1);
    }
    CHECK(!ReadWorldInstanceCellFile(path, cell, read, error));

    GameProjectWorldInstanceSetDesc set{};
    std::strcpy(set.name, "Pines");
    std::strcpy(set.modelAssetGuid, "abc");
    set.tintVariation = 0.25f;
    const std::filesystem::path setsPath = directory / kWorldInstanceSetsFileName;
    CHECK(WriteWorldInstanceSetsFile(setsPath, std::span(&set, 1u), error));
    std::vector<GameProjectWorldInstanceSetDesc> sets{};
    CHECK(ReadWorldInstanceSetsFile(setsPath, sets, error));
    CHECK(sets.size() == 1u && std::strcmp(sets[0].name, "Pines") == 0 && sets[0].tintVariation == 0.25f);
    std::filesystem::remove_all(directory, ec);
}

void TestTerrainAnalysis() {
    TerrainAnalysis analysis{};
    analysis.width = 65u;
    analysis.height = 65u;
    analysis.cellMeters = 8.0f;
    analysis.heights.resize(static_cast<size_t>(analysis.width) * analysis.height);
    for (uint32_t z = 0u; z < analysis.height; ++z) {
        for (uint32_t x = 0u; x < analysis.width; ++x) {
            const float across = std::abs(static_cast<float>(x) - 32.0f) * 8.0f;
            analysis.heights[static_cast<size_t>(z) * analysis.width + x] =
                across * 0.3f + static_cast<float>(z) * 1.0f;
        }
    }
    BuildTerrainAnalysis(analysis);
    CHECK(analysis.Valid());
    CHECK(std::all_of(analysis.debris.begin(), analysis.debris.end(),
                      [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f + 1.0e-3f; }));
    const double middle = 32.0 * 8.0;
    CHECK(analysis.WetnessAt(middle, middle) > analysis.WetnessAt(middle + 120.0, middle) + 0.2f);
    CHECK(analysis.PositionAt(middle, middle) > 0.2f);
    CHECK(analysis.PositionAt(middle + 200.0, middle) < analysis.PositionAt(middle, middle));

    ErosionField field{};
    field.width = 257u;
    field.height = 257u;
    field.spacingMeters = 1.0f;
    field.heights.resize(static_cast<size_t>(field.width) * field.height);
    for (uint32_t z = 0u; z < field.height; ++z) {
        for (uint32_t x = 0u; x < field.width; ++x) {
            const float fx = static_cast<float>(x) * 0.02f;
            const float fz = static_cast<float>(z) * 0.02f;
            field.heights[static_cast<size_t>(z) * field.width + x] = 60.0f + 20.0f * std::sin(fx) * std::cos(fz);
        }
    }
    const auto [lowBefore, highBefore] = std::minmax_element(field.heights.begin(), field.heights.end());
    const float lowest = *lowBefore;
    const float highest = *highBefore;
    std::string error{};
    ErosionField carved = field;
    CHECK(CarveValleys(carved, ValleySettings{}, error));
    CHECK(carved.heights != field.heights);
    const auto [low, high] = std::minmax_element(carved.heights.begin(), carved.heights.end());
    CHECK(std::isfinite(*low) && std::isfinite(*high));
    CHECK(*low > lowest - 60.0f && *high < highest + 60.0f);
    // 山塊：中心が一番高く、外周へ下がる。
    {
        TerrainData massifData(TerrainGrid{});
        HeightLayer mountain{};
        mountain.kind = HeightLayerKind::Noise;
        mountain.noise.amplitudeMeters = 600.0f;
        mountain.noise.offsetMeters = 0.0f;
        mountain.noise.octaves = 2u;
        mountain.noise.massifRadiusMeters = 512.0f;
        CHECK(massifData.AddLayer(std::move(mountain)) != 0u);
        const HeightStackEvaluator massif(massifData);
        const SampleRect center = SampleRect{-16, -16, 33u, 33u};
        const SampleRect outside = SampleRect{2000, 2000, 33u, 33u};
        std::vector<float> centerHeights{};
        std::vector<float> outsideHeights{};
        CHECK(massif.EvaluateRect(center, centerHeights));
        CHECK(massif.EvaluateRect(outside, outsideHeights));
        CHECK(*std::min_element(centerHeights.begin(), centerHeights.end()) >
              *std::max_element(outsideHeights.begin(), outsideHeights.end()) + 100.0f);
    }
    ErosionField tooSmall{};
    tooSmall.width = 16u;
    tooSmall.height = 16u;
    tooSmall.heights.assign(256u, 0.0f);
    CHECK(!CarveValleys(tooSmall, ValleySettings{}, error));
}

// 道路は路盤を平らにして路面を塗り、河は掘って岸を湿らせ、水面は下流へ上がらない。
void TestSplineLayers() {
    TerrainGrid grid{};
    grid.tileCells = 64u;
    grid.tileSizeMeters = 64.0f;
    TerrainData data(grid);
    HeightLayer base{};
    base.kind = HeightLayerKind::Noise;
    base.blend = HeightBlendMode::Replace;
    base.noise.fractal = NoiseFractal::None;
    base.noise.amplitudeMeters = 0.0f;
    base.noise.offsetMeters = 50.0f;
    CHECK(data.AddLayer(base) != 0u);
    HeightLayer road{};
    road.kind = HeightLayerKind::Spline;
    road.blend = HeightBlendMode::Replace;
    road.spline.kind = SplineKind::Road;
    road.spline.falloffMeters = 6.0f;
    road.spline.edgeMeters = 2.0f;
    road.spline.surface = 3u;
    road.spline.edgeSurface = 1u;
    road.spline.points = {{HIKARI::MATH::Vec3{10.0f, 60.0f, 8.0f}, 6.0f},
                          {HIKARI::MATH::Vec3{10.0f, 60.0f, 56.0f}, 6.0f}};
    const uint32_t roadId = data.AddLayer(road);
    CHECK(roadId != 0u);
    const HeightStackEvaluator evaluator(data);
    const SampleRect rect = TileSampleRect(grid, TileCoord{0, 0});
    std::vector<float> heights{};
    CHECK(evaluator.EvaluateRect(rect, heights));
    const auto at = [&](uint32_t x, uint32_t z) { return heights[static_cast<size_t>(z) * rect.width + x]; };
    CHECK(std::abs(at(10u, 32u) - 60.0f) < 0.01f);       // 路面
    CHECK(std::abs(at(14u, 32u) - 60.0f) < 0.01f);       // 路肩まで平ら
    CHECK(std::abs(at(40u, 32u) - 50.0f) < 0.01f);       // 届かない所は元のまま
    CHECK(at(18u, 32u) > 50.0f && at(18u, 32u) < 60.0f); // 斜面
    const PreparedSpline prepared = PrepareSpline(data.FindLayer(roadId)->spline);
    std::vector<uint32_t> control(rect.Count(), 0u);
    ControlTexel painted{};
    painted.baseMaterial = 7u;
    painted.handPainted = true;
    CHECK(PackControlTexel(painted, control[static_cast<size_t>(32u) * rect.width + 9u]));
    PaintSplineSurfaces(std::span(&prepared, 1u), grid, rect, control);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(32u) * rect.width + 10u]).baseMaterial == 3u);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(32u) * rect.width + 9u]).baseMaterial == 7u);
    CHECK(UnpackControlTexel(control[static_cast<size_t>(32u) * rect.width + 40u]).baseMaterial == 0u);
    // 一つの点を動かした時は、その近くの範囲だけ変わる。
    SplineLayerSettings moved = prepared.settings;
    moved.points.push_back(SplinePoint{HIKARI::MATH::Vec3{10.0f, 60.0f, 120.0f}, 6.0f});
    moved.points.push_back(SplinePoint{HIKARI::MATH::Vec3{10.0f, 60.0f, 200.0f}, 6.0f});
    SplineLayerSettings movedEnd = moved;
    movedEnd.points.back().position.z = 220.0f;
    const SampleRect changed = SplineChangedRect(PrepareSpline(moved), PrepareSpline(movedEnd), grid);
    CHECK(!changed.Empty() && changed.z0 > 8);
    // 河：水面は上流より上がらない。岸の中は掘られ、岸は湿る。
    SplineLayerSettings river{};
    river.kind = SplineKind::River;
    river.depthMeters = 3.0f;
    river.points = {{HIKARI::MATH::Vec3{30.0f, 49.0f, 0.0f}, 10.0f}, {HIKARI::MATH::Vec3{30.0f, 52.0f, 60.0f}, 10.0f}};
    const PreparedSpline flow = PrepareSpline(river);
    CHECK(flow.sections.back().y <= flow.sections.front().y);
    std::vector<float> values{};
    std::vector<float> alpha(rect.Count(), 1.0f);
    EvaluateSplineHeights(flow, grid, rect, values, alpha);
    const size_t center = static_cast<size_t>(32u) * rect.width + 30u;
    CHECK(values[center] < 47.0f && alpha[center] == 1.0f);
    std::vector<uint32_t> riverControl(rect.Count(), 0u);
    PaintSplineSurfaces(std::span(&flow, 1u), grid, rect, riverControl);
    CHECK(UnpackControlTexel(riverControl[center]).wetness == 15u);
    // 記述は JSON で往復する。
    const nlohmann::json written = WriteHeightLayerStackJson(data.Stack());
    HeightLayerStack readBack{};
    std::string error{};
    CHECK(ReadHeightLayerStackJson(written, readBack, error));
    CHECK(readBack == data.Stack());
}

void TestUndo() {
    TerrainData data(SmallGrid());
    const uint32_t sculpt = data.AddLayer(SculptLayer());
    HeightBrushSettings raise{};
    raise.radiusMeters = 6.0f;
    raise.strength = 1.5f;
    const Vec2 seed{10.0f, 10.0f};
    BrushEditResult result{};
    std::string error;
    CHECK(ApplyHeightBrush(data, sculpt, raise, std::span(&seed, 1u), nullptr, result, error));
    const TiledRaster<float> before = *data.LayerValues(sculpt);
    // 縁をまたいで新しいタイルも作る筆跡を記録する。
    TerrainEditRecorder recorder(data);
    const Vec2 path[] = {{10.0f, 10.0f}, {70.0f, 10.0f}, {70.0f, 70.0f}};
    CHECK(ApplyHeightBrush(data, sculpt, raise, path, &recorder, result, error));
    const TiledRaster<float> after = *data.LayerValues(sculpt);
    const TerrainEdit edit = recorder.Finish("Raise");
    CHECK(!edit.Empty() && edit.tiles.size() >= 3u && !edit.stackBefore.has_value());
    CHECK(edit.ByteSize() < 3u * 65u * 65u * 4u * 2u);
    CHECK(UndoTerrainEdit(data, edit, error));
    CHECK(*data.LayerValues(sculpt) == before);
    CHECK(RedoTerrainEdit(data, edit, error));
    CHECK(*data.LayerValues(sculpt) == after);
    // 層を消す操作も戻せる（層の記述とタイルの両方）。
    TerrainEditRecorder removal(data);
    removal.TouchAll(TerrainChannel::LayerValues, sculpt);
    removal.TouchAll(TerrainChannel::LayerMask, sculpt);
    CHECK(data.RemoveLayer(sculpt));
    const TerrainEdit removeEdit = removal.Finish("Remove Layer");
    CHECK(removeEdit.stackBefore.has_value() && data.FindLayer(sculpt) == nullptr);
    CHECK(UndoTerrainEdit(data, removeEdit, error));
    CHECK(data.FindLayer(sculpt) != nullptr && *data.LayerValues(sculpt) == after);
    CHECK(RedoTerrainEdit(data, removeEdit, error));
    CHECK(data.FindLayer(sculpt) == nullptr);
    // 壊れた記録は data を変えずに失敗する。
    CHECK(UndoTerrainEdit(data, removeEdit, error));
    TerrainEdit corrupt = edit;
    corrupt.tiles.back().before.assign(8u, 0xABu);
    corrupt.tiles.back().existedBefore = true;
    const TiledRaster<float> untouched = *data.LayerValues(sculpt);
    CHECK(!UndoTerrainEdit(data, corrupt, error));
    CHECK(*data.LayerValues(sculpt) == untouched);
}

ErosionField ConeField(uint32_t size, float spacing) {
    ErosionField field{};
    field.width = size;
    field.height = size;
    field.spacingMeters = spacing;
    field.heights.resize(static_cast<std::size_t>(size) * size);
    const float c = static_cast<float>(size - 1u) * 0.5f;
    for (uint32_t z = 0u; z < size; ++z) {
        for (uint32_t x = 0u; x < size; ++x) {
            const float dx = (static_cast<float>(x) - c) * spacing;
            const float dz = (static_cast<float>(z) - c) * spacing;
            const float r = std::sqrt(dx * dx + dz * dz);
            const float ripple = 0.8f * std::sin(static_cast<float>(x) * 0.7f) * std::cos(static_cast<float>(z) * 0.5f);
            field.heights[static_cast<std::size_t>(z) * size + x] = (std::max)(0.0f, 40.0f - r * 1.2f) + ripple;
        }
    }
    return field;
}

double Sum(const std::vector<float>& values) { return std::accumulate(values.begin(), values.end(), 0.0); }

float MaxSlope(const ErosionField& field) {
    float slope = 0.0f;
    for (uint32_t z = 0u; z < field.height; ++z) {
        for (uint32_t x = 0u; x + 1u < field.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * field.width + x;
            slope = (std::max)(slope, std::fabs(field.heights[i + 1u] - field.heights[i]) / field.spacingMeters);
        }
    }
    return slope;
}

void TestErosion() {
    std::string error;
    ErosionField thermal = ConeField(64u, 0.5f);
    const double before = Sum(thermal.heights);
    const float slopeBefore = MaxSlope(thermal);
    ThermalErosionSettings thermalSettings{};
    thermalSettings.iterations = 80u;
    thermalSettings.talusAngleDegrees = 30.0f;
    ErosionMaps maps{};
    CHECK(RunThermalErosion(thermal, thermalSettings, &maps, error));
    CHECK(std::fabs(Sum(thermal.heights) - before) < 1.0e-3 * thermal.heights.size());
    CHECK(MaxSlope(thermal) < slopeBefore);
    CHECK(Sum(maps.wear) > 0.0 && std::fabs(Sum(maps.wear) - Sum(maps.deposition)) < 1.0e-4 * Sum(maps.wear));

    ErosionField hydraulic = ConeField(64u, 1.0f);
    ErosionField repeat = hydraulic;
    HydraulicErosionSettings hydraulicSettings{};
    hydraulicSettings.iterations = 120u;
    ErosionMaps hydraulicMaps{};
    const double hydraulicBefore = Sum(hydraulic.heights);
    CHECK(RunHydraulicErosion(hydraulic, hydraulicSettings, &hydraulicMaps, error));
    // 削った分と積もった分は釣り合う（堆積物の運搬で総量が増えない）。
    CHECK(std::fabs(Sum(hydraulic.heights) - hydraulicBefore) < 1.0e-3 * hydraulic.heights.size());
    CHECK(RunHydraulicErosion(repeat, hydraulicSettings, nullptr, error));
    CHECK(hydraulic.heights == repeat.heights);
    CHECK(std::all_of(hydraulic.heights.begin(), hydraulic.heights.end(), [](float h) { return std::isfinite(h); }));
    CHECK(Sum(hydraulicMaps.flow) > 0.0 && Sum(hydraulicMaps.wear) > 0.0 && Sum(hydraulicMaps.deposition) > 0.0);
    CHECK(*std::max_element(hydraulicMaps.wear.begin(), hydraulicMaps.wear.end()) <=
          hydraulicSettings.maximumErosionDepth + 1.0e-4f);
    ErosionField tiny{};
    tiny.width = 2u;
    tiny.height = 2u;
    tiny.heights.assign(4u, 0.0f);
    CHECK(!RunHydraulicErosion(tiny, hydraulicSettings, nullptr, error));

    // 層へ書く：外周の羽根の幅では変化が 0 から始まる。
    TerrainData data(SmallGrid());
    data.AddLayer(NoiseLayer(25.0f));
    const uint32_t sculpt = data.AddLayer(SculptLayer());
    ErosionRequest request{};
    request.hydraulicSettings.iterations = 60u;
    request.thermalSettings.iterations = 20u;
    request.featherSamples = 8u;
    const SampleRect rect{0, 0, 96u, 96u};
    TerrainEditRecorder recorder(data);
    CHECK(ApplyErosionToLayer(data, sculpt, rect, request, &recorder, nullptr, error));
    CHECK(data.LayerValues(sculpt)->Get(0, 40) == 0.0f);
    float changed = 0.0f;
    for (int64_t x = 20; x < 76; ++x) {
        changed = (std::max)(changed, std::fabs(data.LayerValues(sculpt)->Get(x, 48)));
    }
    CHECK(changed > 1.0e-3f);
    const TerrainEdit edit = recorder.Finish("Erode");
    CHECK(edit.ChangedTiles().size() == 4u);
    CHECK(UndoTerrainEdit(data, edit, error) && data.LayerValues(sculpt)->TileCount() == 0u);
}

void TestCellFile() {
    TerrainData data(SmallGrid());
    data.AddLayer(NoiseLayer(15.0f));
    const uint32_t sculpt = data.AddLayer(SculptLayer());
    HeightLayer maskedSculpt = *data.FindLayer(sculpt);
    maskedSculpt.masked = true;
    data.SetLayer(maskedSculpt);
    HeightBrushSettings raise{};
    raise.radiusMeters = 10.0f;
    const Vec2 center{30.0f, 30.0f};
    BrushEditResult result{};
    std::string error;
    ApplyHeightBrush(data, sculpt, raise, std::span(&center, 1u), nullptr, result, error);
    ControlBrushSettings paint{};
    paint.material = 9u;
    ApplyControlBrush(data, paint, std::span(&center, 1u), nullptr, result, error);
    std::vector<float> half(9u, 0.5f);
    data.LayerMask(sculpt)->WriteRect(data.Grid(), SampleRect{29, 29, 3u, 3u}, half);
    HeightStackEvaluator evaluator(data);
    std::vector<uint8_t> bytes;
    CHECK(WriteTerrainCell(data, {0, 0}, &evaluator, {}, bytes, error));
    std::vector<uint8_t> again;
    WriteTerrainCell(data, {0, 0}, &evaluator, {}, again, error);
    CHECK(bytes == again);
    const std::filesystem::path path = OutputDirectory() / TerrainCellFileName({0, 0});
    CHECK(SaveTerrainCellFile(path, bytes, error));
    std::vector<uint8_t> loaded;
    CHECK(LoadTerrainCellFile(path, loaded, error) && loaded == bytes);

    TerrainData reopened(SmallGrid());
    CHECK(reopened.ReplaceStack(data.Stack()));
    CHECK(ReadTerrainCell(reopened, {0, 0}, loaded, error));
    CHECK(*reopened.LayerValues(sculpt)->Find({0, 0}) == *data.LayerValues(sculpt)->Find({0, 0}));
    CHECK(*reopened.LayerMask(sculpt)->Find({0, 0}) == *data.LayerMask(sculpt)->Find({0, 0}));
    CHECK(*reopened.Control().Find({0, 0}) == *data.Control().Find({0, 0}));
    TerrainCellRuntime runtime{};
    TileRaster<uint16_t> baked{};
    CHECK(evaluator.BakeTile({0, 0}, baked));
    CHECK(ReadTerrainCellRuntime(data.Grid(), {0, 0}, loaded, runtime, error));
    CHECK(runtime.heights == baked && runtime.control == *data.Control().Find({0, 0}));
    std::printf("  cell file: %zu bytes (baked 16-bit heights + control + layer values + mask)\n", bytes.size());

    // 壊れたファイルは理由を付けて拒否し、data を変えない。
    const TileRaster<float> kept = *reopened.LayerValues(sculpt)->Find({0, 0});
    std::vector<uint8_t> corrupt = bytes;
    corrupt.back() ^= 0x5Au;
    CHECK(!ReadTerrainCell(reopened, {0, 0}, corrupt, error) && error.find("checksum") != std::string::npos);
    CHECK(*reopened.LayerValues(sculpt)->Find({0, 0}) == kept);
    CHECK(!ReadTerrainCell(reopened, {1, 0}, bytes, error) && error.find("instead of") != std::string::npos);
    std::vector<uint8_t> future = bytes;
    future[4] = 2u;
    CHECK(!ReadTerrainCell(reopened, {0, 0}, future, error) && error.find("unsupported") != std::string::npos);
    CHECK(!ReadTerrainCell(reopened, {0, 0}, std::span(bytes).first(20u), error));
    TerrainGrid otherGrid = SmallGrid();
    otherGrid.heightRange = 2048.0f;
    CHECK(!ReadTerrainCellRuntime(otherGrid, {0, 0}, bytes, runtime, error));
    TerrainData missingLayer(SmallGrid());
    CHECK(!ReadTerrainCell(missingLayer, {0, 0}, bytes, error));
}

void TestImages() {
    std::string error;
    HeightImage image{};
    image.width = 33u;
    image.height = 17u;
    image.values.resize(33u * 17u);
    for (uint32_t y = 0u; y < 17u; ++y) {
        for (uint32_t x = 0u; x < 33u; ++x) {
            image.values[y * 33u + x] = static_cast<float>(x) / 32.0f * 0.75f + static_cast<float>(y) / 16.0f * 0.25f;
        }
    }
    const std::filesystem::path exr = OutputDirectory() / "gradient.exr";
    CHECK(SaveHeightImageExr(exr, image, error));
    HeightImage read{};
    CHECK(LoadHeightImage(exr, 0u, read, error));
    CHECK(read == image);
    const std::filesystem::path raw = OutputDirectory() / "gradient.r16";
    CHECK(SaveHeightImageRaw16(raw, image, error));
    CHECK(LoadHeightImage(raw, 33u, read, error) && read.width == 33u && read.height == 17u);
    float worst = 0.0f;
    for (std::size_t i = 0u; i < image.values.size(); ++i) {
        worst = (std::max)(worst, std::fabs(read.values[i] - image.values[i]));
    }
    CHECK(worst <= 0.5f / 65535.0f + 1.0e-7f);
    CHECK(!LoadHeightImage(raw, 10u, read, error));
    CHECK(!LoadHeightImage(OutputDirectory() / "missing.png", 0u, read, error));
    CHECK(std::fabs(SampleHeightImage(image, 16.0, 8.0) - (0.375f + 0.125f)) < 1.0e-6f);

    // 画像層：画素 (0,0) を世界 (0,0)、33 画素を 64 m に置き、値 1 を 100 m にする。
    TerrainData data(SmallGrid());
    HeightLayer layer{};
    layer.kind = HeightLayerKind::Image;
    layer.image.source = "gradient.exr";
    layer.image.sizeMeters = {64.0f, 32.0f};
    layer.image.heightScaleMeters = 100.0f;
    const uint32_t id = data.AddLayer(layer);
    CHECK(data.SetLayerImage(id, std::make_shared<HeightImage>(image)));
    HeightStackEvaluator evaluator(data);
    std::vector<float> out;
    CHECK(evaluator.EvaluateRect(SampleRect{32, 16, 1u, 1u}, out));
    CHECK(std::fabs(out[0] - 50.0f) < 1.0e-3f);
    CHECK(evaluator.EvaluateRect(SampleRect{0, 40, 1u, 1u}, out) && out[0] == 0.0f);
}

void ImportSample() {
    const char* sample = std::getenv("HIKARI_TERRAIN_IMPORT_SAMPLE");
    if (sample == nullptr || *sample == '\0') {
        std::printf("  import sample: skipped (HIKARI_TERRAIN_IMPORT_SAMPLE not set)\n");
        return;
    }
    std::string error;
    HeightImage image{};
    const auto start = std::chrono::steady_clock::now();
    if (!LoadHeightImage(sample, 0u, image, error)) {
        std::printf("  import sample: %s\n", error.c_str());
        CHECK(false);
        return;
    }
    const double loadMs = Milliseconds(start);
    TerrainGrid grid{};
    TerrainData data(grid);
    HeightLayer layer{};
    layer.kind = HeightLayerKind::Image;
    layer.image.source = sample;
    layer.image.sizeMeters = {static_cast<float>(image.width - 1u), static_cast<float>(image.height - 1u)};
    layer.image.heightScaleMeters = 600.0f;
    const uint32_t id = data.AddLayer(layer);
    data.SetLayerImage(id, std::make_shared<HeightImage>(image));
    data.AddLayer(NoiseLayer(4.0f));
    HeightStackEvaluator evaluator(data);
    std::vector<float> heights;
    const SampleRect rect{0, 0, (std::min)(image.width, 2049u), (std::min)(image.height, 2049u)};
    const auto evalStart = std::chrono::steady_clock::now();
    CHECK(evaluator.EvaluateRect(rect, heights));
    const auto [lo, hi] = std::minmax_element(heights.begin(), heights.end());
    std::printf("  import sample: %ux%u loaded in %.1f ms, %ux%u evaluated in %.1f ms, height %.2f..%.2f m\n",
                image.width, image.height, loadMs, rect.width, rect.height, Milliseconds(evalStart), *lo, *hi);
}

void Bench() {
    std::string error;
    TerrainGrid grid{};
    TerrainData data(grid);
    HeightLayer base = NoiseLayer(150.0f);
    base.noise.featureSizeMeters = 900.0f;
    base.noise.octaves = 7u;
    data.AddLayer(base);
    HeightLayer ridges = NoiseLayer(60.0f);
    ridges.noise.fractal = NoiseFractal::Ridged;
    ridges.noise.featureSizeMeters = 400.0f;
    data.AddLayer(ridges);
    const uint32_t sculpt = data.AddLayer(SculptLayer());
    HeightStackEvaluator evaluator(data);
    std::vector<float> out;
    auto start = std::chrono::steady_clock::now();
    for (int32_t i = 0; i < 16; ++i) {
        evaluator.EvaluateTile({i, 0}, out);
    }
    std::printf("  bench: 257x257 tile, 2 noise layers (7+5 octaves) + sculpt: %.2f ms per tile\n",
                Milliseconds(start) / 16.0);
    start = std::chrono::steady_clock::now();
    evaluator.EvaluateRect(SampleRect{0, 0, 4097u, 4097u}, out);
    std::printf("  bench: 4 km x 4 km (4097^2) evaluated in %.0f ms\n", Milliseconds(start));
    ErosionRequest request{};
    const SampleRect km{0, 0, 1024u, 1024u};
    start = std::chrono::steady_clock::now();
    CHECK(ApplyErosionToLayer(data, sculpt, km, request, nullptr, nullptr, error));
    std::printf("  bench: 1 km^2 (1024^2 at 1 m) thermal %u + hydraulic %u iterations: %.0f ms\n",
                request.thermalSettings.iterations, request.hydraulicSettings.iterations, Milliseconds(start));
    HeightStackEvaluator baked(data);
    start = std::chrono::steady_clock::now();
    std::vector<uint8_t> bytes;
    std::size_t total = 0u;
    for (int32_t z = 0; z < 4; ++z) {
        for (int32_t x = 0; x < 4; ++x) {
            WriteTerrainCell(data, {x, z}, &baked, {}, bytes, error);
            total += bytes.size();
        }
    }
    std::printf("  bench: 16 cell files written in %.1f ms, %.1f KiB per cell\n", Milliseconds(start),
                static_cast<double>(total) / 16.0 / 1024.0);
}

} // namespace

int main(int argc, char** argv) {
    const bool bench = argc > 1 && std::string(argv[1]) == "--bench";
    TestControl();
    TestGrid();
    TestRasterEdges();
    TestStackJson();
    TestEvaluation();
    TestBrushes();
    TestBrushVariantsStampsAndMasks();
    TestSurfaceRules();
    TestTerrainAnalysis();
    TestWorldInstanceFile();
    TestScatter();
    TestSplineLayers();
    TestUndo();
    TestErosion();
    TestCellFile();
    TestImages();
    ImportSample();
    if (bench) {
        Bench();
    }
    std::printf("terrain data contracts: %s (%d failures)\n", gFailures == 0 ? "passed" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
