#pragma once

#include <cstdint>
#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "TerrainKit/Data/HIKARI_TerrainDataErosion.h"

// 大きな形から「侵蝕された山」を作る。粗い格子から細かい格子へ順に水系を彫り（河川侵蝕の流水力則の陰解法、
// Braun & Willett 2013）、安息角で岩屑を均し（熱侵蝕）、岩の強い所を段にして、最後に岩の凹凸を載せる。
// 細かい起伏はこの処理が作り直すので、元の地形からは大きな形だけが残る。
namespace HIKARI::TERRAIN::DATA {

struct ValleySettings {
    float erosion = 1.0f;       // 水系を彫る強さ
    float hillSlope = 1.1f;     // 斜面の限界の傾き（tan）
    float detail = 1.0f;        // 水系の枝分かれを生む細かい揺らぎ
    float rockVariation = 0.6f; // 岩質のむら（値の大きい所は深く彫られて谷壁が立ち、小さい所は滑らかな斜面になる）
    float ledges = 1.0f;        // 岩の段（地層）
    float roughness = 11.0f;    // 岩の凹凸の高さ（m）
    int32_t seed = 7;
    float strength = 1.0f; // 元の地形との混ぜ具合
    bool operator==(const ValleySettings&) const = default;
};

inline constexpr auto kValleyFields = [](auto&& field) {
    using S = ValleySettings;
    using namespace JSONFIELDS;
    field("erosion", &S::erosion, Range{0.0, 8.0}, Label{"Erosion"}, Help{"How deeply water carves the valleys."});
    field("hillSlope", &S::hillSlope, Range{0.1, 3.0}, Label{"Hill Slope"},
          Help{"Tangent of the steepest hillside; steeper ground collapses toward it."});
    field("detail", &S::detail, Range{0.0, 4.0}, Label{"Branching"},
          Help{"Small variation that makes valleys wander and branch."});
    field("rockVariation", &S::rockVariation, Slider(0.0, 1.0), Label{"Rock Variation"},
          Help{"How much the rock differs from place to place: some areas are deeply gullied, others stay smooth."});
    field("ledges", &S::ledges, Slider(0.0, 1.0), Label{"Ledges"}, Help{"Horizontal steps in the rock."});
    field("roughness", &S::roughness, Range{0.0, 100.0}, Label{"Rock Roughness"}, Unit{"m"});
    field("seed", &S::seed, Label{"Seed"});
    field("strength", &S::strength, Slider(0.0, 1.0), Label{"Strength"});
};

// 場の高さを侵蝕された山の形へ置き換える。外周は流れの出口になる。
bool CarveValleys(ErosionField& field, const ValleySettings& settings, std::string& error);

} // namespace HIKARI::TERRAIN::DATA
