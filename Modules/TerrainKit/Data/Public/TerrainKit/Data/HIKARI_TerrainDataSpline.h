#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataGrid.h"

// 曲線の層：道路・河・坂。制御点だけを保存し、高さと材質はそこから毎回求める（元の地形を壊さない）。
// 道路は路盤を平らにして路面と路肩を塗る。河は河床を掘り、岸を塗って湿らせ、水面の帯を作る。坂は高さだけ。
namespace HIKARI::TERRAIN::DATA {

enum class SplineKind : uint8_t { Road = 0, River = 1, Ramp = 2 };

inline constexpr TEXT::EnumName<SplineKind> kSplineKindNames[] = {
    {SplineKind::Road, "road"},
    {SplineKind::River, "river"},
    {SplineKind::Ramp, "ramp"},
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
    // 河だけ。
    float depthMeters = 2.5f;
    float wetMeters = 6.0f;
    float flowSpeed = 1.2f; // m/s
    MATH::Vec3 waterColor{0.035f, 0.13f, 0.12f};
    float clarityMeters = 1.2f; // この水の厚さで 63% 見えなくなる
    bool operator==(const SplineLayerSettings&) const = default;
};

inline constexpr auto kSplineLayerFields = [](auto&& field) {
    using S = SplineLayerSettings;
    using namespace JSONFIELDS;
    constexpr auto isRiver = [](const S& settings) { return settings.kind == SplineKind::River; };
    field("kind", &S::kind, kSplineKindNames, Label{"Kind"}, ReadOnly{});
    field("points", &S::points, Elements{kSplinePointFields}, Label{"Points"});
    field("falloffMeters", &S::falloffMeters, Range{0.0, 2048.0}, Label{"Falloff"}, Unit{"m"},
          Help{"Width of the slope that blends back to the terrain below."});
    field("edgeMeters", &S::edgeMeters, Range{0.0, 512.0}, Label{"Edge Width"}, Unit{"m"},
          Help{"Road shoulder or river bank painted with the edge surface."});
    field("paint", &S::paint, Label{"Paint Surface"});
    field("surface", &S::surface, Range{0.0, 31.0}, Label{"Surface Slot"});
    field("edgeSurface", &S::edgeSurface, Range{0.0, 31.0}, Label{"Edge Surface Slot"});
    field("depthMeters", &S::depthMeters, Range{0.0, 512.0}, Label{"Depth"}, Unit{"m"}, Show{isRiver});
    field("wetMeters", &S::wetMeters, Range{0.0, 512.0}, Label{"Wet Bank"}, Unit{"m"}, Show{isRiver});
    field("flowSpeed", &S::flowSpeed, Range{0.0, 64.0}, Label{"Flow Speed"}, Unit{"m/s"}, Show{isRiver});
    field("waterColor", &S::waterColor, Range{0.0, 8.0}, Label{"Water Color"}, Color{}, Show{isRiver});
    field("clarityMeters", &S::clarityMeters, Range{0.01, 1000.0}, Label{"Clarity"}, Unit{"m"}, Show{isRiver});
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

struct PreparedSpline {
    SplineLayerSettings settings{};
    std::vector<SplineSection> sections{};
    // 制御点の区間ごとの最初の区切りの番号（末尾に総数）。動かした区間だけ焼き直すのに使う。
    std::vector<uint32_t> segmentFirst{};
    float reach = 0.0f; // 中心線からこの距離の外には何もしない
};

// 河の水面の帯が岸の外へはみ出す幅。岸はここまで水面より上に保つ。岸を崩す噪声の幅より常に広い。
inline float RiverBankMargin(float halfWidth) noexcept {
    const float scaled = halfWidth * 0.3f;
    return scaled > 1.5f ? scaled : 1.5f;
}

// 中心線は向心 Catmull-Rom。河の水面は上流から下流へ上がらないように丸める。
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

} // namespace HIKARI::TERRAIN::DATA
