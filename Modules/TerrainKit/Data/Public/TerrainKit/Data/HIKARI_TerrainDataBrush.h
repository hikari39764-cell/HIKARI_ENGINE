#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"

// 筆刷の核：世界 XZ の折れ線に沿って一定間隔で dab を置き、手彫り層（高さ）か制御図（材質・穴）を書き換える。
// 筆刷の入力は世界座標の折れ線と設定だけなので、人の操作と智能体の操作が同じ結果になる（同じ入力 → 同じ値）。
namespace HIKARI::TERRAIN::DATA {

enum class HeightBrushOp : uint8_t {
    Raise = 0,   // 強さ（m）× 重みだけ上げる
    Lower = 1,   // 下げる
    Smooth = 2,  // 合成後の高さを 3×3 の平均へ寄せる（強さは 0..1 の割合）
    Flatten = 3, // 合成後の高さを targetHeight へ寄せる（強さは 0..1 の割合）
    Noise = 4,   // 決定的な値噪声（noiseScaleMeters）を強さ（m）で足す
    Ramp = 5,    // 折れ線の始点から終点へ一定の坂へ寄せる（強さは 0..1 の割合）
    Terrace = 6, // 合成後の高さを terraceStepMeters の段へ寄せる（強さは 0..1 の割合）
};

inline constexpr TEXT::EnumName<HeightBrushOp> kHeightBrushOpNames[] = {
    {HeightBrushOp::Raise, "raise"},     {HeightBrushOp::Lower, "lower"}, {HeightBrushOp::Smooth, "smooth"},
    {HeightBrushOp::Flatten, "flatten"}, {HeightBrushOp::Noise, "noise"}, {HeightBrushOp::Ramp, "ramp"},
    {HeightBrushOp::Terrace, "terrace"},
};

// 筆の形：正方形の濃淡の図（0..1）。dab の円の中へ、dab ごとに回して重ねる。
struct BrushShape {
    uint32_t size = 0u;
    std::vector<float> values{}; // size * size
};

struct HeightBrushSettings {
    HeightBrushOp op = HeightBrushOp::Raise;
    float radiusMeters = 16.0f;
    float strength = 0.5f;
    float hardness = 0.5f;
    float spacing = 0.25f; // dab の間隔（半径に対する割合）
    float targetHeightMeters = 0.0f;
    float noiseScaleMeters = 8.0f;
    uint32_t noiseSeed = 1u;
    float terraceStepMeters = 8.0f;
    // 0 より大きければ坂の傾きを固定する（始点の高さから終点の向きへ上る）。0 なら終点の地面の高さへ繋ぐ。
    float rampSlopeDegrees = 0.0f;
    // この範囲の傾き・高さの所だけ筆が効く（縁は緩やかに切れる）。
    float limitMinSlopeDegrees = 0.0f;
    float limitMaxSlopeDegrees = 90.0f;
    float limitMinHeightMeters = -100000.0f;
    float limitMaxHeightMeters = 100000.0f;
    bool operator==(const HeightBrushSettings&) const = default;
};

inline constexpr auto kHeightBrushFields = [](auto&& field) {
    using S = HeightBrushSettings;
    using namespace JSONFIELDS;
    field("op", &S::op, kHeightBrushOpNames, Label{"Operation"});
    field("radiusMeters", &S::radiusMeters, Range{0.25, 4096.0}, Label{"Radius"}, Unit{"m"});
    field("strength", &S::strength, Range{0.0, 1000.0}, Label{"Strength"},
          Help{"Meters per dab for raise, lower and noise; fraction per dab (0..1) for smooth and flatten."});
    field("hardness", &S::hardness, Slider(0.0, 1.0), Label{"Hardness"},
          Help{"Fraction of the radius at full weight before the smooth falloff."});
    field("spacing", &S::spacing, Range{0.02, 4.0}, Label{"Spacing"}, Help{"Dab spacing as a fraction of the radius."});
    field("targetHeightMeters", &S::targetHeightMeters, Range{-100000.0, 100000.0}, Label{"Target Height"}, Unit{"m"},
          Show{[](const S& s) { return s.op == HeightBrushOp::Flatten; }});
    field("noiseScaleMeters", &S::noiseScaleMeters, Range{0.25, 4096.0}, Label{"Noise Scale"}, Unit{"m"},
          Show{[](const S& s) { return s.op == HeightBrushOp::Noise; }});
    field("noiseSeed", &S::noiseSeed, Label{"Noise Seed"},
          Show{[](const S& s) { return s.op == HeightBrushOp::Noise; }});
    field("terraceStepMeters", &S::terraceStepMeters, Range{0.1, 4096.0}, Label{"Terrace Step"}, Unit{"m"},
          Show{[](const S& s) { return s.op == HeightBrushOp::Terrace; }});
    field("rampSlopeDegrees", &S::rampSlopeDegrees, Range{-89.0, 89.0}, Label{"Ramp Slope"}, Unit{"deg"},
          Help{"Fixed slope from the start of the path; 0 joins the ground height at the end of the path."},
          Show{[](const S& s) { return s.op == HeightBrushOp::Ramp; }});
    field("limitMinSlopeDegrees", &S::limitMinSlopeDegrees, Range{0.0, 90.0}, Label{"Min Slope"}, Unit{"deg"});
    field("limitMaxSlopeDegrees", &S::limitMaxSlopeDegrees, Range{0.0, 90.0}, Label{"Max Slope"}, Unit{"deg"});
    field("limitMinHeightMeters", &S::limitMinHeightMeters, Range{-100000.0, 100000.0}, Label{"Min Height"}, Unit{"m"});
    field("limitMaxHeightMeters", &S::limitMaxHeightMeters, Range{-100000.0, 100000.0}, Label{"Max Height"}, Unit{"m"});
};

enum class ControlBrushOp : uint8_t {
    Paint = 0,  // 材質を塗る（PaintControlMaterial）
    Hole = 1,   // 穴を開ける
    Fill = 2,   // 穴を塞ぐ
    Unlock = 3, // 手塗りの印を外し、自動材質規則に返す
};

inline constexpr TEXT::EnumName<ControlBrushOp> kControlBrushOpNames[] = {
    {ControlBrushOp::Paint, "paint"},
    {ControlBrushOp::Hole, "hole"},
    {ControlBrushOp::Fill, "fill"},
    {ControlBrushOp::Unlock, "unlock"},
};

struct ControlBrushSettings {
    ControlBrushOp op = ControlBrushOp::Paint;
    uint32_t material = 0u;
    float radiusMeters = 8.0f;
    float strength = 0.5f;
    float hardness = 0.5f;
    float spacing = 0.25f;
    bool operator==(const ControlBrushSettings&) const = default;
};

inline constexpr auto kControlBrushFields = [](auto&& field) {
    using S = ControlBrushSettings;
    using namespace JSONFIELDS;
    field("op", &S::op, kControlBrushOpNames, Label{"Operation"});
    field("material", &S::material, Range{0.0, 31.0}, Label{"Material"},
          Show{[](const S& s) { return s.op == ControlBrushOp::Paint; }});
    field("radiusMeters", &S::radiusMeters, Range{0.25, 4096.0}, Label{"Radius"}, Unit{"m"});
    field("strength", &S::strength, Slider(0.0, 1.0), Label{"Strength"});
    field("hardness", &S::hardness, Slider(0.0, 1.0), Label{"Hardness"});
    field("spacing", &S::spacing, Range{0.02, 4.0}, Label{"Spacing"});
};

struct BrushEditResult {
    uint32_t dabs = 0u;
    SampleRect touched{}; // 書き換えた標本を全て含む矩形
};

// 中心からの距離 / 半径 → 重み（hardness までは 1、そこから端へ滑らかに 0）。
float BrushFalloff(float normalizedDistance, float hardness) noexcept;
// 折れ線上の dab の中心（最初の点を必ず含む）。
std::vector<MATH::Vec2> PlaceBrushDabs(std::span<const MATH::Vec2> path, float radiusMeters, float spacing);

// 手彫り層へ高さの筆刷をかける。recorder があれば変更前のタイルを記録する。
bool ApplyHeightBrush(TerrainData& data, uint32_t layerId, const HeightBrushSettings& settings,
                      std::span<const MATH::Vec2> path, TerrainEditRecorder* recorder, BrushEditResult& result,
                      std::string& error, const BrushShape* shape = nullptr);
// 制御図へ材質・穴の筆刷をかける。
bool ApplyControlBrush(TerrainData& data, const ControlBrushSettings& settings, std::span<const MATH::Vec2> path,
                       TerrainEditRecorder* recorder, BrushEditResult& result, std::string& error,
                       const BrushShape* shape = nullptr);

} // namespace HIKARI::TERRAIN::DATA
