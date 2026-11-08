#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
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
    field("minSlopeDegrees", &S::minSlopeDegrees, Range{0.0, 90.0}, Label{"Min Slope"}, Unit{"deg"});
    field("maxSlopeDegrees", &S::maxSlopeDegrees, Range{0.0, 90.0}, Label{"Max Slope"}, Unit{"deg"});
    field("slopeFadeDegrees", &S::slopeFadeDegrees, Range{0.01, 45.0}, Label{"Slope Fade"}, Unit{"deg"});
    field("minHeightMeters", &S::minHeightMeters, Range{-100000.0, 100000.0}, Label{"Min Height"}, Unit{"m"});
    field("maxHeightMeters", &S::maxHeightMeters, Range{-100000.0, 100000.0}, Label{"Max Height"}, Unit{"m"});
    field("heightFadeMeters", &S::heightFadeMeters, Range{0.01, 4096.0}, Label{"Height Fade"}, Unit{"m"});
    field("minConcavityMeters", &S::minConcavityMeters, Range{-1000.0, 1000.0}, Label{"Min Concavity"}, Unit{"m"});
    field("maxConcavityMeters", &S::maxConcavityMeters, Range{-1000.0, 1000.0}, Label{"Max Concavity"}, Unit{"m"});
    field("concavityFadeMeters", &S::concavityFadeMeters, Range{0.01, 100.0}, Label{"Concavity Fade"}, Unit{"m"});
    field("facingDegrees", &S::facingDegrees, Range{-360.0, 360.0}, Label{"Facing"}, Unit{"deg"});
    field("facingAmount", &S::facingAmount, Slider(0.0, 1.0), Label{"Facing Amount"});
    field("noiseScaleMeters", &S::noiseScaleMeters, Range{0.25, 4096.0}, Label{"Noise Scale"}, Unit{"m"});
    field("noiseAmount", &S::noiseAmount, Range{0.0, 4.0}, Label{"Noise Amount"});
};

struct SurfaceRuleSet {
    std::vector<SurfaceRule> rules{};
    bool operator==(const SurfaceRuleSet&) const = default;
};

inline constexpr auto kSurfaceRuleSetFields = [](auto&& field) {
    field("rules", &SurfaceRuleSet::rules, JSONFIELDS::Elements{kSurfaceRuleFields}, JSONFIELDS::Label{"Rules"});
};

// 枠 0 を地面として、傾きと高さで土・岩・砂・雪を重ねる既定の組み合わせ。
SurfaceRuleSet DefaultSurfaceRules();

// 周りの高さを見るのに要る余白（標本）。
inline constexpr uint32_t kSurfaceRuleHeightMargin = 8u;

struct SurfaceRuleTile {
    uint32_t samples = 0u;
    float spacingMeters = 1.0f;
    int64_t firstSampleX = 0; // 噪声の世界座標用
    int64_t firstSampleZ = 0;
    // (samples + 2 * margin)² の高さ（m）。行優先、x が内側。
    std::span<const float> heights{};
};

// control は samples² の制御図。手塗りの標本と、穴・ナビの印はそのまま残す。
void EvaluateSurfaceRules(const SurfaceRuleSet& set, const SurfaceRuleTile& tile, std::vector<uint32_t>& control);

} // namespace HIKARI::TERRAIN::DATA
