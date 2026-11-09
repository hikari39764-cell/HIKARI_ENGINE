#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"
#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

// 散布規則：地形の条件から個体の置き場所を決める。一規則が一層で、沿線の規則、面の規則の順に場所を取る（大きい物を先に）。
// 候補は世界座標の抖動格子で、位置としきい値は規則の種と格子の番号の哈希で決まる（Horizon Zero Dawn の密度図と抖動の
// 考え方）。同じ入力なら同じ結果で、瓦片の境でも続く。条件は材質規則と同じ坡度・高さ・湿り・地形の位置に、地表材質、
// 筆で塗れるマスク（層のマスクを流用）、塊の噪声を足したもの。手で動かした分は結果に重ねる側（呼び出し側）が持つ。
namespace HIKARI::TERRAIN::DATA {

struct ScatterRule {
    std::string name{};
    bool enabled = true;
    uint32_t seed = 1u;           // 順番を入れ替えても結果が変わらないよう、乱数は規則が持つ
    std::string instanceSet{};    // 結果を書く実例集の名前
    float densityPer100 = 2.0f;   // 100 m2 あたりの個体数（条件を全部満たす所で）
    float footprintMeters = 1.0f; // 占める半径。後の規則はこの中に置けない
    float scaleMin = 0.8f;
    float scaleMax = 1.25f;
    float edgeScale = 0.6f;      // 密度の低い所（林縁）の個体の大きさの倍率
    float alignToGround = 0.25f; // 0 は真上、1 は地面の傾きのまま
    float minSlopeDegrees = 0.0f;
    float maxSlopeDegrees = 40.0f;
    float slopeFadeDegrees = 6.0f;
    float minHeightMeters = -100000.0f;
    float maxHeightMeters = 100000.0f;
    float heightFadeMeters = 20.0f;
    float minWetness = 0.0f;
    float maxWetness = 1.0f;
    float wetnessFade = 0.2f;
    float minPosition = -1.0f;
    float maxPosition = 1.0f;
    float positionFade = 0.4f;
    uint32_t surfaceMask = 0xFFFFFFFFu; // 置いてよい地表材質の枠（bit が枠の番号）
    uint32_t maskLayer = 0u;            // この層のマスクを密度に掛ける（0 は無し）
    // 塊：噪声で密度を揺らす。0 なら一様。大きいほど林と空き地がはっきり分かれる。
    float clusterAmount = 0.6f;
    float clusterScaleMeters = 80.0f;
    bool avoidSplines = true; // 道路と河を避ける
    bool operator==(const ScatterRule&) const = default;
};

inline constexpr auto kScatterRuleFields = [](auto&& field) {
    using S = ScatterRule;
    using namespace JSONFIELDS;
    field("name", &S::name, Label{"Name"});
    field("enabled", &S::enabled, Label{"Enabled"});
    field("seed", &S::seed, Range{0.0, 4294967295.0}, Label{"Seed"});
    field("instanceSet", &S::instanceSet, Label{"Instance Set"}, Group{"Instances"});
    field("densityPer100", &S::densityPer100, Range{0.0, 10000.0}, Label{"Density"}, Unit{"per 100 m2"},
          Group{"Instances"});
    field("footprintMeters", &S::footprintMeters, Range{0.05, 25.0}, Label{"Footprint"}, Unit{"m"},
          Help{"Space one instance takes. Later rules cannot place inside it."}, Group{"Instances"});
    field("scaleMin", &S::scaleMin, Range{0.01, 100.0}, Label{"Scale Min"}, Group{"Instances"});
    field("scaleMax", &S::scaleMax, Range{0.01, 100.0}, Label{"Scale Max"}, Group{"Instances"});
    field("edgeScale", &S::edgeScale, Slider(0.0, 1.0), Label{"Edge Scale"},
          Help{"Size of instances where density is low, such as at the edge of a forest."}, Group{"Instances"});
    field("alignToGround", &S::alignToGround, Slider(0.0, 1.0), Label{"Align To Ground"}, Group{"Instances"});
    field("minSlopeDegrees", &S::minSlopeDegrees, Range{0.0, 90.0}, Label{"Min Slope"}, Unit{"deg"}, Group{"Slope"});
    field("maxSlopeDegrees", &S::maxSlopeDegrees, Range{0.0, 90.0}, Label{"Max Slope"}, Unit{"deg"}, Group{"Slope"});
    field("slopeFadeDegrees", &S::slopeFadeDegrees, Range{0.01, 45.0}, Label{"Slope Fade"}, Unit{"deg"},
          Group{"Slope"});
    field("minHeightMeters", &S::minHeightMeters, Range{-100000.0, 100000.0}, Label{"Min Height"}, Unit{"m"},
          Group{"Height"});
    field("maxHeightMeters", &S::maxHeightMeters, Range{-100000.0, 100000.0}, Label{"Max Height"}, Unit{"m"},
          Group{"Height"});
    field("heightFadeMeters", &S::heightFadeMeters, Range{0.01, 4096.0}, Label{"Height Fade"}, Unit{"m"},
          Group{"Height"});
    field("minWetness", &S::minWetness, Slider(0.0, 1.0), Label{"Min Wetness"}, Group{"Wetness"});
    field("maxWetness", &S::maxWetness, Slider(0.0, 1.0), Label{"Max Wetness"}, Group{"Wetness"});
    field("wetnessFade", &S::wetnessFade, Range{0.01, 1.0}, Label{"Wetness Fade"}, Group{"Wetness"});
    field("minPosition", &S::minPosition, Range{-1.0, 1.0}, Label{"Min Position"}, Group{"Ridge Or Valley"});
    field("maxPosition", &S::maxPosition, Range{-1.0, 1.0}, Label{"Max Position"}, Group{"Ridge Or Valley"});
    field("positionFade", &S::positionFade, Range{0.01, 2.0}, Label{"Position Fade"}, Group{"Ridge Or Valley"});
    field("surfaceMask", &S::surfaceMask, Range{0.0, 4294967295.0}, Label{"Surface Slots"},
          Help{"Bit mask of the surface slots allowed."}, Group{"Allowed Surfaces"});
    field("maskLayer", &S::maskLayer, Range{0.0, 4294967295.0}, Label{"Mask Layer"},
          Help{"Terrain layer whose painted mask multiplies the density. 0 means none."}, Group{"Density Mask"});
    field("clusterAmount", &S::clusterAmount, Slider(0.0, 1.0), Label{"Clustering"},
          Help{"How strongly noise groups instances into stands and clearings."}, Group{"Clustering"});
    field("clusterScaleMeters", &S::clusterScaleMeters, Range{1.0, 8192.0}, Label{"Cluster Size"}, Unit{"m"},
          Group{"Clustering"});
    field("avoidSplines", &S::avoidSplines, Label{"Avoid Roads And Rivers"}, Group{"Clustering"});
};

// 沿線に並べる規則：栅栏、電柱、石垣など。
enum class ScatterLineSide : uint8_t { Center = 0, Left = 1, Right = 2, Both = 3 };

inline constexpr TEXT::EnumName<ScatterLineSide> kScatterLineSideNames[] = {
    {ScatterLineSide::Center, "center"},
    {ScatterLineSide::Left, "left"},
    {ScatterLineSide::Right, "right"},
    {ScatterLineSide::Both, "both"},
};

struct ScatterLineRule {
    std::string name{};
    bool enabled = true;
    uint32_t seed = 1u;
    std::string instanceSet{};
    uint32_t splineLayer = 0u; // 沿う曲線の層
    float spacingMeters = 4.0f;
    ScatterLineSide side = ScatterLineSide::Both;
    float offsetMeters = 1.0f; // 曲線の縁からの距離（Center では使わない）
    float jitterMeters = 0.0f;
    float yawJitterDegrees = 0.0f;
    float scaleMin = 1.0f;
    float scaleMax = 1.0f;
    float footprintMeters = 0.5f;
    bool operator==(const ScatterLineRule&) const = default;
};

inline constexpr auto kScatterLineRuleFields = [](auto&& field) {
    using S = ScatterLineRule;
    using namespace JSONFIELDS;
    field("name", &S::name, Label{"Name"});
    field("enabled", &S::enabled, Label{"Enabled"});
    field("seed", &S::seed, Range{0.0, 4294967295.0}, Label{"Seed"});
    field("instanceSet", &S::instanceSet, Label{"Instance Set"});
    field("splineLayer", &S::splineLayer, Range{0.0, 4294967295.0}, Label{"Spline Layer"});
    field("spacingMeters", &S::spacingMeters, Range{0.25, 4096.0}, Label{"Spacing"}, Unit{"m"});
    field("side", &S::side, kScatterLineSideNames, Label{"Side"});
    field("offsetMeters", &S::offsetMeters, Range{-256.0, 256.0}, Label{"Offset From Edge"}, Unit{"m"});
    field("jitterMeters", &S::jitterMeters, Range{0.0, 256.0}, Label{"Position Jitter"}, Unit{"m"});
    field("yawJitterDegrees", &S::yawJitterDegrees, Range{0.0, 180.0}, Label{"Rotation Jitter"}, Unit{"deg"});
    field("scaleMin", &S::scaleMin, Range{0.01, 100.0}, Label{"Scale Min"});
    field("scaleMax", &S::scaleMax, Range{0.01, 100.0}, Label{"Scale Max"});
    field("footprintMeters", &S::footprintMeters, Range{0.05, 25.0}, Label{"Footprint"}, Unit{"m"});
};

struct ScatterRuleSet {
    std::vector<ScatterRule> rules{};
    std::vector<ScatterLineRule> lines{};
    float splineMarginMeters = 1.5f; // 道路や河の縁からさらに空ける距離
    bool operator==(const ScatterRuleSet&) const = default;
};

inline constexpr auto kScatterRuleSetFields = [](auto&& field) {
    using namespace JSONFIELDS;
    field("rules", &ScatterRuleSet::rules, Elements{kScatterRuleFields}, Label{"Scatter Rules"});
    field("lines", &ScatterRuleSet::lines, Elements{kScatterLineRuleFields}, Label{"Line Rules"});
    field("splineMarginMeters", &ScatterRuleSet::splineMarginMeters, Range{0.0, 256.0}, Label{"Spline Margin"},
          Unit{"m"});
};

// 既定の枠（DefaultSurfaceRules）に合わせた山の組み合わせ：森、低木、岩。
ScatterRuleSet DefaultScatterRules();

// 先に場所を取っているもの（手で置いた個体など）。
struct ScatterReserved {
    float x = 0.0f;
    float z = 0.0f;
    float radiusMeters = 1.0f;
};

struct ScatterTile {
    SurfaceRuleTile surface{}; // 高さの余白つきの入力
    std::span<const uint32_t> control{};
    // 規則と同じ数の samples² のマスク（空の要素は無し）。余白の分は見ない。
    std::span<const std::vector<float>> ruleMasks{};
    // 曲線と、その層の番号（同じ数）。
    std::span<const PreparedSpline> splines{};
    std::span<const uint32_t> splineLayers{};
    std::span<const ScatterReserved> reserved{};
    // これより低い地面（海の中）には置かない。
    float waterLevel = -1.0e9f;
};

struct ScatterPoint {
    uint32_t rule = 0u; // rules または lines の番号
    bool line = false;
    uint32_t seed = 0u; // 規則と候補の番号から決まる。手で消した個体を覚える鍵にもなる
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
    float scale = 1.0f;
};

// 瓦片の中の個体を求める。余白の分も同じ計算をして場所を取るので、隣の瓦片の個体と足跡が重ならない。
void EvaluateScatter(const ScatterRuleSet& set, const ScatterTile& tile, std::vector<ScatterPoint>& out);

// 方位 yaw の回転に、地面の傾き（高さの勾配）の align の割合だけ倒す回転を重ねる。out は x, y, z, w。
void GroundAlignedRotation(float yawRadians, float slopeX, float slopeZ, float align, float out[4]) noexcept;

} // namespace HIKARI::TERRAIN::DATA
