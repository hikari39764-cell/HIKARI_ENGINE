#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

// 曲線の層：道路・河・坂・湖。制御点だけを保存し、高さと材質はそこから毎回求める（元の地形を壊さない）。
// 道路は路盤を平らにして路面と路肩を塗る。河は河床を掘り、岸を塗って湿らせ、水面の帯を作る。坂は高さだけ。
// 湖は閉じた曲線で範囲を決め、最初の点の高さが水面。深さが 0 なら地形を掘らず、窪地へ水を張るだけ。
namespace HIKARI::TERRAIN::DATA {

enum class SplineKind : uint8_t { Road = 0, River = 1, Ramp = 2, Lake = 3 };

inline constexpr TEXT::EnumName<SplineKind> kSplineKindNames[] = {
    {SplineKind::Road, "road"},
    {SplineKind::River, "river"},
    {SplineKind::Ramp, "ramp"},
    {SplineKind::Lake, "lake"},
};

inline constexpr uint32_t kSplineMaximumPoints = 512u;

struct SplinePoint {
    MATH::Vec3 position{}; // 河では y が水面の高さ
    float widthMeters = 8.0f;
    bool operator==(const SplinePoint&) const = default;
};

inline constexpr auto kSplinePointFields = [](auto&& field) {
    using S = SplinePoint;
    using namespace JSONFIELDS;
    field("position", &S::position, Label{"Position"}, Unit{"m"});
    field("widthMeters", &S::widthMeters, Range{0.5, 2048.0}, Label{"Width"}, Unit{"m"});
};

struct SplineLayerSettings {
    SplineKind kind = SplineKind::Road;
    std::vector<SplinePoint> points{};
    // 元の地形へ戻っていく斜面の幅。
    float falloffMeters = 12.0f;
    // 路肩、または河の岸（塗る幅）。
    float edgeMeters = 3.0f;
    // 材質。paint を切ると高さだけ変える。
    bool paint = true;
    uint32_t surface = 1u;
    uint32_t edgeSurface = 1u;
    // 河と湖。
    float depthMeters = 2.5f;
    float wetMeters = 6.0f;
    float flowSpeed = 1.2f; // m/s
    // 湖だけ。
    float shelfMeters = 16.0f;     // 岸から一番深い所まで下がる幅
    float shoreNoiseMeters = 2.0f; // 岸の線を崩す量
    MATH::Vec3 waterColor{0.035f, 0.13f, 0.12f};
    float clarityMeters = 1.2f; // この水の厚さで 63% 見えなくなる
    bool operator==(const SplineLayerSettings&) const = default;
};

inline constexpr auto kSplineLayerFields = [](auto&& field) {
    using S = SplineLayerSettings;
    using namespace JSONFIELDS;
    constexpr auto isRiver = [](const S& settings) { return settings.kind == SplineKind::River; };
    constexpr auto isLake = [](const S& settings) { return settings.kind == SplineKind::Lake; };
    constexpr auto isWater = [](const S& settings) {
        return settings.kind == SplineKind::River || settings.kind == SplineKind::Lake;
    };
    field("kind", &S::kind, kSplineKindNames, Label{"Kind"}, ReadOnly{});
    field("points", &S::points, Elements{kSplinePointFields}, Label{"Points"});
    field("falloffMeters", &S::falloffMeters, Range{0.0, 2048.0}, Label{"Falloff"}, Unit{"m"},
          Help{"Width of the slope that blends back to the terrain below."});
    field("edgeMeters", &S::edgeMeters, Range{0.0, 512.0}, Label{"Edge Width"}, Unit{"m"},
          Help{"Road shoulder or river bank painted with the edge surface."});
    field("paint", &S::paint, Label{"Paint Surface"});
    field("surface", &S::surface, Range{0.0, 31.0}, Label{"Surface Slot"});
    field("edgeSurface", &S::edgeSurface, Range{0.0, 31.0}, Label{"Edge Surface Slot"});
    field("depthMeters", &S::depthMeters, Range{0.0, 512.0}, Label{"Depth"}, Unit{"m"}, Show{isWater});
    field("wetMeters", &S::wetMeters, Range{0.0, 512.0}, Label{"Wet Bank"}, Unit{"m"}, Show{isWater});
    field("flowSpeed", &S::flowSpeed, Range{0.0, 64.0}, Label{"Flow Speed"}, Unit{"m/s"}, Show{isWater});
    field("waterColor", &S::waterColor, Range{0.0, 8.0}, Label{"Water Color"}, Color{}, Show{isWater});
    field("clarityMeters", &S::clarityMeters, Range{0.01, 1000.0}, Label{"Clarity"}, Unit{"m"}, Show{isWater});
    field("shelfMeters", &S::shelfMeters, Range{0.1, 2048.0}, Label{"Shore Shelf"}, Unit{"m"}, Show{isLake},
          Help{"Width over which the lake bed falls from the shore to full depth."});
    field("shoreNoiseMeters", &S::shoreNoiseMeters, Range{0.0, 64.0}, Label{"Shore Noise"}, Unit{"m"}, Show{isLake});
};

// 曲線を細かく切った一区切り。河の水面の帯もこの列から作る。
struct SplineSection {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float halfWidth = 1.0f;
    float tangentX = 0.0f;
    float tangentZ = 1.0f;
    float along = 0.0f; // 始点からの水平距離（m）
};

// 湖の符号付き距離の図（内側が正）。範囲の外は負の大きな値を返す。
struct SplineLakeField {
    double originX = 0.0;
    double originZ = 0.0;
    float cellMeters = 2.0f;
    uint32_t width = 0u;
    uint32_t height = 0u;
    float level = 0.0f; // 水面の高さ
    std::vector<float> signedDistance{};

    bool Valid() const noexcept { return width >= 2u && height >= 2u; }
    float DistanceAt(double worldX, double worldZ) const noexcept;
};

struct PreparedSpline {
    SplineLayerSettings settings{};
    std::vector<SplineSection> sections{};
    SplineLakeField lake{}; // 湖だけ
    // 制御点の区間ごとの最初の区切りの番号（末尾に総数）。動かした区間だけ焼き直すのに使う。
    std::vector<uint32_t> segmentFirst{};
    float reach = 0.0f; // 中心線からこの距離の外には何もしない
};

// 河の水面の帯が岸の外へはみ出す幅。岸はここまで水面より上に保つ。岸を崩す噪声の幅より常に広い。
inline float RiverBankMargin(float halfWidth) noexcept {
    const float scaled = halfWidth * 0.3f;
    return scaled > 1.5f ? scaled : 1.5f;
}

// 中心線は向心 Catmull-Rom（湖は閉じる）。河の水面は上流から下流へ上がらないように丸める。
PreparedSpline PrepareSpline(const SplineLayerSettings& settings);

// 層の値（alpha には opacity などが入っていて、そこへ形を掛ける）。
void EvaluateSplineHeights(const PreparedSpline& spline, const TerrainGrid& grid, const SampleRect& rect,
                           std::vector<float>& values, std::vector<float>& alpha);

// 曲線が届く全範囲の標本の矩形。
SampleRect SplineAffectedRect(const PreparedSpline& spline, const TerrainGrid& grid);
// before から after へ変えた時に結果が変わる範囲（変わった区間とその前後）。
SampleRect SplineChangedRect(const PreparedSpline& before, const PreparedSpline& after, const TerrainGrid& grid);

// 材質を塗る。rect は control の範囲（rect.Count() 個）。手塗りの標本と穴・ナビの印はそのまま残す。
void PaintSplineSurfaces(std::span<const PreparedSpline> splines, const TerrainGrid& grid, const SampleRect& rect,
                         std::vector<uint32_t>& control);

// 窪地へ水を張る時の水面と岸の線。seed を含む窪地から水があふれ出る高さまで広げ、岸を閉じた線にする。
struct LakeFill {
    float level = 0.0f;
    std::vector<MATH::Vec2> shore{}; // 世界の XZ
    float areaSquareMeters = 0.0f;
};

// heights は width x height の高さ（行優先、x が内側）。窪地が無い（すぐ外周へ流れ出る）か、minimumArea より小さければ false。
bool FindLakeFill(std::span<const float> heights, uint32_t width, uint32_t height, double originX, double originZ,
                  float cellMeters, double seedX, double seedZ, float minimumArea, LakeFill& out);

} // namespace HIKARI::TERRAIN::DATA
