#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"
#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

// 高さの層スタック（下から上へ合成する）。記述は JSON 文本で保存し、智能体と人が直接読んで差分を取れる。
// 層の画素（手彫りの差分、マスク）はタイル単位の二進ファイルに分けて持つ（HIKARI_TerrainDataFile.h）。
namespace HIKARI::TERRAIN::DATA {

enum class HeightLayerKind : uint8_t {
    Noise = 0,  // FastNoise2 の手続き生成
    Image = 1,  // 外部ツールの高さ画像（EXR / R16）
    Sculpt = 2, // 筆刷・侵蝕・印章が書き込むタイル単位の値
    Spline = 3, // 道路・河・坂の曲線（HIKARI_TerrainDataSpline.h）
};

inline constexpr TEXT::EnumName<HeightLayerKind> kHeightLayerKindNames[] = {
    {HeightLayerKind::Noise, "noise"},
    {HeightLayerKind::Image, "image"},
    {HeightLayerKind::Sculpt, "sculpt"},
    {HeightLayerKind::Spline, "spline"},
};

// c = 下の合成結果、v = この層の値、a = 不透明度 × マスク。
enum class HeightBlendMode : uint8_t {
    Add = 0,      // c + a v
    Subtract = 1, // c - a v
    Max = 2,      // lerp(c, max(c, v), a)
    Min = 3,      // lerp(c, min(c, v), a)
    Replace = 4,  // lerp(c, v, a)
    Multiply = 5, // lerp(c, c v, a)
};

inline constexpr TEXT::EnumName<HeightBlendMode> kHeightBlendModeNames[] = {
    {HeightBlendMode::Add, "add"}, {HeightBlendMode::Subtract, "subtract"}, {HeightBlendMode::Max, "max"},
    {HeightBlendMode::Min, "min"}, {HeightBlendMode::Replace, "replace"},   {HeightBlendMode::Multiply, "multiply"},
};

enum class NoiseBasis : uint8_t { Simplex = 0, SuperSimplex = 1, Perlin = 2, Value = 3 };

inline constexpr TEXT::EnumName<NoiseBasis> kNoiseBasisNames[] = {
    {NoiseBasis::Simplex, "simplex"},
    {NoiseBasis::SuperSimplex, "superSimplex"},
    {NoiseBasis::Perlin, "perlin"},
    {NoiseBasis::Value, "value"},
};

enum class NoiseFractal : uint8_t { None = 0, FBm = 1, Ridged = 2 };

inline constexpr TEXT::EnumName<NoiseFractal> kNoiseFractalNames[] = {
    {NoiseFractal::None, "none"},
    {NoiseFractal::FBm, "fbm"},
    {NoiseFractal::Ridged, "ridged"},
};

struct NoiseLayerSettings {
    NoiseBasis basis = NoiseBasis::Simplex;
    NoiseFractal fractal = NoiseFractal::FBm;
    float featureSizeMeters = 512.0f;
    uint32_t octaves = 5u;
    float gain = 0.5f;
    float lacunarity = 2.0f;
    float weightedStrength = 0.0f;
    float amplitudeMeters = 120.0f;
    float offsetMeters = 0.0f;
    int32_t seed = 1337;
    // 山塊の半径。0 より大きければ噪声は山の形を崩す役になり、高さは中心へ向かって上がる。
    float massifRadiusMeters = 0.0f;
    MATH::Vec2 massifCenterMeters{0.0f, 0.0f};
    // FastNoise2 ノードエディタが書き出す符号化済みノード木。空でなければ上の基底とフラクタルより優先する。
    std::string encodedNodeTree{};
    bool operator==(const NoiseLayerSettings&) const = default;
};

inline constexpr auto kNoiseLayerFields = [](auto&& field) {
    using S = NoiseLayerSettings;
    using namespace JSONFIELDS;
    field("basis", &S::basis, kNoiseBasisNames, Label{"Basis"});
    field("fractal", &S::fractal, kNoiseFractalNames, Label{"Fractal"});
    field("featureSizeMeters", &S::featureSizeMeters, Range{1.0, 65536.0}, Label{"Feature Size"}, Unit{"m"},
          Help{"World size of the largest noise feature (1 / frequency)."});
    field("octaves", &S::octaves, Range{1.0, 16.0}, Label{"Octaves"});
    field("gain", &S::gain, Slider(0.0, 1.0), Label{"Gain"}, Help{"Amplitude multiplier per octave."});
    field("lacunarity", &S::lacunarity, Range{1.0, 4.0}, Label{"Lacunarity"}, Help{"Frequency multiplier per octave."});
    field("weightedStrength", &S::weightedStrength, Slider(0.0, 1.0), Label{"Weighted Strength"},
          Help{"Higher values keep low areas smoother while high areas keep octave detail."});
    field("amplitudeMeters", &S::amplitudeMeters, Range{-20000.0, 20000.0}, Label{"Amplitude"}, Unit{"m"},
          Help{"Height of noise value 1; fractal octaves are normalized to stay within -1..1."});
    field("offsetMeters", &S::offsetMeters, Range{-20000.0, 20000.0}, Label{"Offset"}, Unit{"m"});
    field("seed", &S::seed, Label{"Seed"});
    field("massifRadiusMeters", &S::massifRadiusMeters, Range{0.0, 65536.0}, Label{"Mountain Radius"}, Unit{"m"},
          Help{"Above 0, the layer becomes one mountain mass with this radius; the noise breaks up its shape."});
    field("massifCenterMeters", &S::massifCenterMeters, Label{"Mountain Center"}, Unit{"m"});
    field("encodedNodeTree", &S::encodedNodeTree, Label{"Node Tree"}, Advanced{},
          Help{"Encoded FastNoise2 node tree; overrides basis and fractal when set."});
};

struct ImageLayerSettings {
    // プロジェクト相対の画像パス（.exr / .r16 / .raw）。
    std::string source{};
    uint32_t rawWidth = 0u;
    MATH::Vec2 originMeters{0.0f, 0.0f}; // 画素 (0, 0) の世界 XZ
    MATH::Vec2 sizeMeters{1024.0f, 1024.0f};
    float heightScaleMeters = 1.0f; // 画像の値 1 の高さ
    float heightOffsetMeters = 0.0f;
    bool operator==(const ImageLayerSettings&) const = default;
};

inline constexpr auto kImageLayerFields = [](auto&& field) {
    using S = ImageLayerSettings;
    using namespace JSONFIELDS;
    field("source", &S::source, Label{"Source"}, Help{"Project-relative EXR or R16 height image."});
    field("rawWidth", &S::rawWidth, Range{0.0, 65536.0}, Label{"Raw Width"},
          Help{"Pixel width of an R16 image; 0 means square."});
    field("originMeters", &S::originMeters, Label{"Origin"}, Unit{"m"}, Help{"World XZ of the first pixel."});
    field("sizeMeters", &S::sizeMeters, Label{"Size"}, Unit{"m"}, Help{"World XZ span of the image."});
    field("heightScaleMeters", &S::heightScaleMeters, Range{-100000.0, 100000.0}, Label{"Height Scale"}, Unit{"m"});
    field("heightOffsetMeters", &S::heightOffsetMeters, Range{-100000.0, 100000.0}, Label{"Height Offset"}, Unit{"m"});
};

struct HeightLayer {
    uint32_t id = 0u;
    std::string name{};
    HeightLayerKind kind = HeightLayerKind::Sculpt;
    HeightBlendMode blend = HeightBlendMode::Add;
    float opacity = 1.0f;
    bool enabled = true;
    // true の時、層のマスク画（0..1、既定 1）を不透明度に掛ける。
    bool masked = false;
    NoiseLayerSettings noise{};
    ImageLayerSettings image{};
    SplineLayerSettings spline{};
    bool operator==(const HeightLayer&) const = default;
};

inline constexpr auto kHeightLayerFields = [](auto&& field) {
    using S = HeightLayer;
    using namespace JSONFIELDS;
    field("id", &S::id, ReadOnly{}, Label{"Id"});
    field("name", &S::name, Label{"Name"});
    field("kind", &S::kind, kHeightLayerKindNames, Label{"Kind"}, ReadOnly{});
    field("blend", &S::blend, kHeightBlendModeNames, Label{"Blend"});
    field("opacity", &S::opacity, Slider(0.0, 1.0), Label{"Opacity"});
    field("enabled", &S::enabled, Label{"Enabled"});
    field("masked", &S::masked, Label{"Use Mask"});
    field("noise", &S::noise, Nested{kNoiseLayerFields}, Group{"Noise"},
          Show{[](const S& layer) { return layer.kind == HeightLayerKind::Noise; }});
    field("image", &S::image, Nested{kImageLayerFields}, Group{"Image"},
          Show{[](const S& layer) { return layer.kind == HeightLayerKind::Image; }});
    field("spline", &S::spline, Nested{kSplineLayerFields}, Group{"Spline"},
          Show{[](const S& layer) { return layer.kind == HeightLayerKind::Spline; }});
};

struct HeightLayerStack {
    TerrainGrid grid{};
    std::vector<HeightLayer> layers{}; // 下から上へ
    uint32_t nextLayerId = 1u;
    bool operator==(const HeightLayerStack&) const = default;
};

inline constexpr auto kHeightLayerStackFields = [](auto&& field) {
    using S = HeightLayerStack;
    using namespace JSONFIELDS;
    field("grid", &S::grid, Nested{kTerrainGridFields}, Group{"Grid"});
    field("layers", &S::layers, Elements{kHeightLayerFields}, Label{"Layers"});
    field("nextLayerId", &S::nextLayerId, Hidden{});
};

// 層スタック全体の整合（格子、層 ID が一意で nextLayerId 未満、値が有限）。
bool ValidateHeightLayerStack(const HeightLayerStack& stack, std::string* error = nullptr);
nlohmann::json WriteHeightLayerStackJson(const HeightLayerStack& stack);
// 厳密に読む（型違い・未知の列挙名・整合違反は error を返し、out は変えない）。
bool ReadHeightLayerStackJson(const nlohmann::json& in, HeightLayerStack& out, std::string& error);

// a = opacity × mask で 1 標本を合成する。
float BlendHeight(HeightBlendMode mode, float below, float value, float alpha) noexcept;

} // namespace HIKARI::TERRAIN::DATA
