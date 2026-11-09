#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"

// 地表材質。.hterrainsurface（JSON）が元の図を指し、読む時に通道を詰めて BC7 へ焼き、Library に置く。
// 代码は材質の名前で分岐しない。どの材質も同じ項目で振る舞いを決める。
namespace HIKARI::TERRAINKIT {

inline constexpr uint32_t kSurfaceTextureSize = 1024u;
inline constexpr uint32_t kSurfaceTextureMips = 11u;

// この地表に生える草（Ghost of Tsushima の草の考え方。HIKARI_TerrainKitGrass と Render3D_TerrainKitGrass が読む）。
// 焼いた図の鍵に入れないよう表を分ける（値を変えても図を焼き直さない）。ファイルでは同じ段に並ぶ。
struct TerrainSurfaceGrass {
    float density = 0.0f; // 0 なら生えない
    float heightMin = 0.3f;
    float heightMax = 0.6f;
    float width = 0.016f; // 葉の半分の幅
    float tiltDegrees = 20.0f;
    float tiltVariationDegrees = 16.0f;
    float bend = 0.3f;
    float clumpMeters = 1.2f;
    float clumpHeight = 0.35f;
    float clumpPull = 0.25f;
    float clumpFacing = 0.55f;
    float clumpAway = 0.25f;
    MATH::Vec3 rootColor{0.14f, 0.2f, 0.07f}; // sRGB 0..1
    MATH::Vec3 tipColor{0.46f, 0.56f, 0.2f};
    float translucency = 0.55f;
    float roughness = 0.55f;
    float colorVariation = 0.05f;
    float groundBlend = 0.5f;
    float wind = 1.0f;
    float rootShade = 0.35f;
    float plumeShare = 0.0f; // 穂をつける葉の割合（蒲苇など）
    MATH::Vec3 plumeColor{0.9f, 0.88f, 0.8f};
    bool operator==(const TerrainSurfaceGrass&) const = default;
};

inline constexpr auto kTerrainSurfaceGrassFields = [](auto&& field) {
    using S = TerrainSurfaceGrass;
    using namespace JSONFIELDS;
    field("grassDensity", &S::density, Slider(0.0, 1.0), Label{"Grass Density"},
          Help{"0 means no grass grows on this surface."});
    field("grassHeightMin", &S::heightMin, Range{0.01, 5.0}, Label{"Grass Height Min"}, Unit{"m"});
    field("grassHeightMax", &S::heightMax, Range{0.01, 5.0}, Label{"Grass Height Max"}, Unit{"m"});
    field("grassWidth", &S::width, Range{0.001, 0.2}, Label{"Blade Half Width"}, Unit{"m"});
    field("grassTilt", &S::tiltDegrees, Range{0.0, 80.0}, Label{"Lean"}, Unit{"deg"});
    field("grassTiltVariation", &S::tiltVariationDegrees, Range{0.0, 60.0}, Label{"Lean Variation"}, Unit{"deg"});
    field("grassBend", &S::bend, Slider(0.0, 1.0), Label{"Tip Droop"});
    field("grassClumpMeters", &S::clumpMeters, Range{0.1, 16.0}, Label{"Clump Size"}, Unit{"m"});
    field("grassClumpHeight", &S::clumpHeight, Slider(0.0, 1.0), Label{"Clump Height Difference"});
    field("grassClumpPull", &S::clumpPull, Slider(0.0, 1.0), Label{"Pull To Clump Center"});
    field("grassClumpFacing", &S::clumpFacing, Slider(0.0, 1.0), Label{"Clump Same Direction"});
    field("grassClumpAway", &S::clumpAway, Slider(0.0, 2.0), Label{"Lean Away From Center"});
    field("grassRootColor", &S::rootColor, Label{"Root Color"}, Help{"sRGB 0..1."});
    field("grassTipColor", &S::tipColor, Label{"Tip Color"}, Help{"sRGB 0..1."});
    field("grassTranslucency", &S::translucency, Slider(0.0, 1.0), Label{"Translucency"});
    field("grassRoughness", &S::roughness, Slider(0.05, 1.0), Label{"Roughness"});
    field("grassColorVariation", &S::colorVariation, Slider(0.0, 0.5), Label{"Clump Color Difference"},
          Help{"Keep this small for a painted look."});
    field("grassGroundBlend", &S::groundBlend, Slider(0.0, 1.0), Label{"Root Takes Ground Color"});
    field("grassWind", &S::wind, Range{0.0, 3.0}, Label{"Wind Response"});
    field("grassRootShade", &S::rootShade, Slider(0.0, 1.0), Label{"Root Brightness"});
    field("grassPlumeShare", &S::plumeShare, Slider(0.0, 1.0), Label{"Plume Share"},
          Help{"Share of blades that end in a plume, such as pampas grass."});
    field("grassPlumeColor", &S::plumeColor, Label{"Plume Color"}, Help{"sRGB 0..1."});
};

struct TerrainSurfaceAsset {
    std::string name{};
    // 元の図（この資産からの相対パス）。空なら既定値で埋める。
    std::string color{};
    std::string normal{}; // DirectX 形式
    std::string height{}; // 無ければ明るさから作る
    std::string roughness{};
    std::string ao{};
    float tilingMeters = 4.0f;
    float heightContrast = 0.25f; // 高さ混合の幅（小さいほど境界が締まる）
    float normalStrength = 1.0f;
    float variationContrast = 1.6f; // 大きな尺度で読んだ図の濃淡を強める倍率
    float roughnessValue = 0.85f;   // 粗さの図が無い時の値、ある時は倍率
    // 地形の色の図での配色（sRGB 0..1）。A が基調で、B と C へ広い範囲で揺れる。A が黒なら図の平均色を使う。
    MATH::Vec3 mapColorA{0.0f, 0.0f, 0.0f};
    MATH::Vec3 mapColorB{0.0f, 0.0f, 0.0f};
    MATH::Vec3 mapColorC{0.0f, 0.0f, 0.0f};
    float mapBandMeters = 0.0f; // 地層の帯の間隔。0 なら帯なし
    float mapBandAmount = 0.0f;
    std::string license{};
    std::string author{};
    std::string source{};
    TerrainSurfaceGrass grass{};
    bool operator==(const TerrainSurfaceAsset&) const = default;
};

inline constexpr auto kTerrainSurfaceAssetFields = [](auto&& field) {
    using S = TerrainSurfaceAsset;
    using namespace JSONFIELDS;
    field("name", &S::name, Label{"Name"});
    field("color", &S::color, Label{"Color"});
    field("normal", &S::normal, Label{"Normal"});
    field("height", &S::height, Label{"Height"});
    field("roughness", &S::roughness, Label{"Roughness"});
    field("ao", &S::ao, Label{"Ambient Occlusion"});
    field("tilingMeters", &S::tilingMeters, Range{0.1, 1024.0}, Label{"Tiling"}, Unit{"m"});
    field("heightContrast", &S::heightContrast, Range{0.01, 1.0}, Label{"Height Blend Width"});
    field("normalStrength", &S::normalStrength, Range{0.0, 4.0}, Label{"Normal Strength"});
    field("variationContrast", &S::variationContrast, Range{0.0, 8.0}, Label{"Variation Contrast"},
          Help{"How strongly the larger repeats of the map vary the color at a distance."});
    field("roughnessValue", &S::roughnessValue, Range{0.0, 2.0}, Label{"Roughness"});
    field("mapColorA", &S::mapColorA, Label{"Map Color"},
          Help{"Main color of this surface in the terrain color map, sRGB 0..1. Black uses the map's average."});
    field("mapColorB", &S::mapColorB, Label{"Map Color 2"}, Help{"The color drifts toward this over wide areas."});
    field("mapColorC", &S::mapColorC, Label{"Map Color 3"}, Help{"The color drifts toward this in some patches."});
    field("mapBandMeters", &S::mapBandMeters, Range{0.0, 512.0}, Label{"Band Spacing"}, Unit{"m"},
          Help{"Spacing of rock strata bands. 0 means no bands."});
    field("mapBandAmount", &S::mapBandAmount, Slider(0.0, 1.0), Label{"Band Strength"});
    field("license", &S::license, Label{"License"});
    field("author", &S::author, Label{"Author"});
    field("source", &S::source, Label{"Source"});
};

struct CookedSurface {
    TerrainSurfaceAsset asset{};
    TerrainSurfaceGrass grass{};                         // 実際に描く草（資産の値に場面の上書きを重ねたもの）
    std::vector<uint8_t> colorHeight{};                  // BC7 全段を続けて（RGB sRGB + 高さ）
    std::vector<uint8_t> detail{};                       // BC7 全段を続けて（法線 xy、粗さ、AO）
    std::array<float, 3> averageColor{0.5f, 0.5f, 0.5f}; // 線形
    bool cooked = false;                                 // 今回焼いた（false は Library から読んだ）
};

// 選べる材質（モジュールの内蔵 + プロジェクトの Assets 以下）。名前 → 資産ファイル。
struct SurfaceCatalogEntry {
    std::string name{};
    std::filesystem::path file{};
    bool builtin = false;
};
std::vector<SurfaceCatalogEntry> ListTerrainSurfaces(const std::filesystem::path& projectRoot);
bool ReadTerrainSurfaceAsset(const std::filesystem::path& file, TerrainSurfaceAsset& out, std::string& error);
// 地形の色の図で使う配色。
TERRAIN::DATA::SurfaceMapStyle MakeSurfaceMapStyle(const CookedSurface& surface);

// 地形の枠（0..31 番）へ割り当てた材質。読み込みと焼きは背景の thread で進む。
class TerrainSurfaceLibrary {
public:
    // 背景の thread を止めて待つ。DLL を外す前に呼ぶ（DLL の解放中に thread は待てない）。
    void Shutdown();

    // 枠の名前を替える。中身が同じなら何もしない。
    void SetSlots(const std::filesystem::path& projectRoot, std::vector<std::string> names);
    const std::vector<std::string>& Slots() const noexcept { return names_; }
    // 描画の thread で毎フレーム：背景の結果が揃っていれば受け取る。受け取った時 true。
    bool Poll();
    bool Busy() const noexcept { return pending_.valid(); }
    float Progress() const noexcept;
    const std::vector<CookedSurface>& Surfaces() const noexcept { return surfaces_; }
    uint64_t Revision() const noexcept { return revision_; }
    const std::string& LastError() const noexcept { return lastError_; }
    // 草の見た目の場面の上書き（材質の名前ごと）。図は焼き直さない。
    const std::map<std::string, TerrainSurfaceGrass>& GrassOverrides() const noexcept { return grassOverrides_; }
    void SetGrassOverrides(std::map<std::string, TerrainSurfaceGrass> overrides);
    void SetGrassOverride(const std::string& name, const std::optional<TerrainSurfaceGrass>& grass);
    uint64_t GrassRevision() const noexcept { return grassRevision_; }

private:
    struct Shared {
        std::atomic<uint32_t> finished{0u};
        std::atomic<uint32_t> total{1u};
        std::atomic<bool> cancel{false};
    };
    struct Loaded {
        std::vector<CookedSurface> surfaces{};
        std::string error{};
    };

    std::vector<std::string> names_{};
    std::vector<CookedSurface> surfaces_{};
    std::future<Loaded> pending_{};
    std::shared_ptr<Shared> shared_{};
    uint64_t revision_ = 0u;
    std::string lastError_{};
    std::map<std::string, TerrainSurfaceGrass> grassOverrides_{};
    uint64_t grassRevision_ = 0u;

    void ApplyGrassOverrides();
};

} // namespace HIKARI::TERRAINKIT
