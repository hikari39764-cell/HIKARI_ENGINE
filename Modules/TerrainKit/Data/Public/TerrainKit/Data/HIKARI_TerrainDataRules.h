#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "TerrainKit/Data/HIKARI_TerrainDataAnalysis.h"
#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

// 自動の材質規則：上から順に重ねて、標本ごとに一番効く 2 材質を決めて制御図へ書く。手塗りの標本は書き換えない。
// 材質の名前では分岐しない。どの枠の材質も同じ条件で選ばれる。
namespace HIKARI::TERRAIN::DATA {

struct SurfaceRule {
    std::string name{};
    bool enabled = true;
    uint32_t surface = 0u; // 材質の枠
    float strength = 1.0f;
    float minSlopeDegrees = 0.0f;
    float maxSlopeDegrees = 90.0f;
    float slopeFadeDegrees = 6.0f;
    float minHeightMeters = -100000.0f;
    float maxHeightMeters = 100000.0f;
    float heightFadeMeters = 12.0f;
    // 凹凸：周りの平均との差（m）。正が谷、負が尾根。
    float minConcavityMeters = -1000.0f;
    float maxConcavityMeters = 1000.0f;
    float concavityFadeMeters = 0.5f;
    // 向き：面が向く方位（度、0 が +Z、90 が +X）。facingAmount が 0 なら見ない。
    float facingDegrees = 0.0f;
    float facingAmount = 0.0f;
    // 湿り（0 が乾いた尾根、1 が水の集まる谷底）。
    float minWetness = 0.0f;
    float maxWetness = 1.0f;
    float wetnessFade = 0.2f;
    // 地形の位置（-1 が尾根、1 が谷底）。
    float minPosition = -1.0f;
    float maxPosition = 1.0f;
    float positionFade = 0.4f;
    // 面積の割合：噪声の島にして地面のこれだけを覆う。1 なら島にしない。
    float coverage = 1.0f;
    float coverageScaleMeters = 64.0f;
    float coverageSoftness = 0.12f; // 島の境界の幅（噪声の値で）
    // 境界を噪声で崩す（標本の傾きと高さをフェード幅の割合だけずらす）。
    float noiseScaleMeters = 24.0f;
    float noiseAmount = 0.6f;
    bool operator==(const SurfaceRule&) const = default;
};

inline constexpr auto kSurfaceRuleFields = [](auto&& field) {
    using S = SurfaceRule;
    using namespace JSONFIELDS;
    field("name", &S::name, Label{"Name"});
    field("enabled", &S::enabled, Label{"Enabled"});
    field("surface", &S::surface, Range{0.0, 31.0}, Label{"Surface Slot"});
    field("strength", &S::strength, Slider(0.0, 1.0), Label{"Strength"});
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
    field("minConcavityMeters", &S::minConcavityMeters, Range{-1000.0, 1000.0}, Label{"Min Concavity"}, Unit{"m"},
          Group{"Concavity"});
    field("maxConcavityMeters", &S::maxConcavityMeters, Range{-1000.0, 1000.0}, Label{"Max Concavity"}, Unit{"m"},
          Group{"Concavity"});
    field("concavityFadeMeters", &S::concavityFadeMeters, Range{0.01, 100.0}, Label{"Concavity Fade"}, Unit{"m"},
          Group{"Concavity"});
    field("facingDegrees", &S::facingDegrees, Range{-360.0, 360.0}, Label{"Facing"}, Unit{"deg"}, Group{"Facing"});
    field("facingAmount", &S::facingAmount, Slider(0.0, 1.0), Label{"Facing Amount"}, Group{"Facing"});
    field("minWetness", &S::minWetness, Slider(0.0, 1.0), Label{"Min Wetness"},
          Help{"Where water collects: 0 is a dry ridge, 1 is a valley floor."}, Group{"Wetness"});
    field("maxWetness", &S::maxWetness, Slider(0.0, 1.0), Label{"Max Wetness"}, Group{"Wetness"});
    field("wetnessFade", &S::wetnessFade, Range{0.01, 1.0}, Label{"Wetness Fade"}, Group{"Wetness"});
    field("minPosition", &S::minPosition, Range{-1.0, 1.0}, Label{"Min Position"},
          Help{"Place in the landform: -1 is a ridge, 1 is a valley."}, Group{"Ridge Or Valley"});
    field("maxPosition", &S::maxPosition, Range{-1.0, 1.0}, Label{"Max Position"}, Group{"Ridge Or Valley"});
    field("positionFade", &S::positionFade, Range{0.01, 2.0}, Label{"Position Fade"}, Group{"Ridge Or Valley"});
    field("coverage", &S::coverage, Slider(0.0, 1.0), Label{"Coverage"},
          Help{"Share of the ground this rule covers, as patches of noise. 1 means everywhere."}, Group{"Patches"});
    field("coverageScaleMeters", &S::coverageScaleMeters, Range{1.0, 8192.0}, Label{"Coverage Scale"}, Unit{"m"},
          Group{"Patches"});
    field("coverageSoftness", &S::coverageSoftness, Range{0.01, 2.0}, Label{"Coverage Softness"},
          Help{"Width of the patch edges. Larger values fade the patches in gradually."}, Group{"Patches"});
    field("noiseScaleMeters", &S::noiseScaleMeters, Range{0.25, 4096.0}, Label{"Noise Scale"}, Unit{"m"},
          Group{"Edge Noise"});
    field("noiseAmount", &S::noiseAmount, Range{0.0, 4.0}, Label{"Noise Amount"}, Group{"Edge Noise"});
};

struct SurfaceRuleSet {
    std::vector<SurfaceRule> rules{};
    uint32_t debrisSurface = 7u; // 水の筋に運ばれる岩屑の色を取る材質の枠
    bool operator==(const SurfaceRuleSet&) const = default;
};

inline constexpr auto kSurfaceRuleSetFields = [](auto&& field) {
    field("rules", &SurfaceRuleSet::rules, JSONFIELDS::Elements{kSurfaceRuleFields}, JSONFIELDS::Label{"Rules"});
    field("debrisSurface", &SurfaceRuleSet::debrisSurface, JSONFIELDS::Range{0.0, 31.0},
          JSONFIELDS::Label{"Debris Surface Slot"},
          JSONFIELDS::Help{"Surface whose color water carries down gullies from steep rock."});
};

// 既定の枠に合わせた、山の組み合わせ：麓は草で、乾いた所は枯れ草、湿った窪みは牧草。尾根は擦れた地面、斜面は礫、
// 森林限界より上と崖は岩、川原は砂利、頂上は雪。
SurfaceRuleSet DefaultSurfaceRules();

// 周りの高さを見るのに要る余白（標本）。
inline constexpr uint32_t kSurfaceRuleHeightMargin = 32u;

struct SurfaceRuleTile {
    uint32_t samples = 0u;
    float spacingMeters = 1.0f;
    int64_t firstSampleX = 0; // 噪声の世界座標用
    int64_t firstSampleZ = 0;
    // (samples + 2 * margin)² の高さ（m）。行優先、x が内側。
    std::span<const float> heights{};
    const TerrainAnalysis* analysis = nullptr; // 無ければ湿り 0.5、位置 0
};

// control は samples² の制御図。手塗りの標本と、穴・ナビの印はそのまま残す。
void EvaluateSurfaceRules(const SurfaceRuleSet& set, const SurfaceRuleTile& tile, std::vector<uint32_t>& control);

// 材質の面の色の作り方（線形 RGB）。3 色を広い噪声で揺らし、帯（地層）と細かな濃淡を載せる。
struct SurfaceMapStyle {
    std::array<std::array<float, 3>, 3> colors{};
    float bandMeters = 0.0f;
    float bandAmount = 0.0f;
};

// 地形の色の図を瓦片ごとに焼く：制御図の材質の配色、上流から運ばれた岩屑の筋、凹みの暗さと稜の明るさ。
// 近くの材質の図はこの色に対する濃淡として掛かるので、どの距離でも色はここで決まる。
// control は samples² の制御図、styles は枠の順。out は size² x RGBA：RGB = 線形の色、A = 環境光の遮蔽。
// 世界座標だけで決まるので瓦片の境で揃う。
void BakeSurfaceColorMap(const SurfaceRuleSet& set, const SurfaceRuleTile& tile, std::span<const uint32_t> control,
                         std::span<const SurfaceMapStyle> styles, uint32_t size, std::vector<float>& out);

} // namespace HIKARI::TERRAIN::DATA
