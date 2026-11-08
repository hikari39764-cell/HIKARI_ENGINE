#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"

// 地表材質。.hterrainsurface（JSON）が元の図を指し、読む時に通道を詰めて BC7 へ焼き、Library に置く。
// 代码は材質の名前で分岐しない。どの材質も同じ項目で振る舞いを決める。
namespace HIKARI::TERRAINKIT {

inline constexpr uint32_t kSurfaceTextureSize = 1024u;
inline constexpr uint32_t kSurfaceTextureMips = 11u;

struct TerrainSurfaceAsset {
    std::string name{};
    // 元の図（この資産からの相対パス）。空なら baseColor と既定値で埋める。
    std::string color{};
    std::string normal{}; // DirectX 形式
    std::string height{}; // 無ければ明るさから作る
    std::string roughness{};
    std::string ao{};
    float tilingMeters = 4.0f;
    float heightContrast = 0.25f; // 高さ混合の幅（小さいほど境界が締まる）
    float normalStrength = 1.0f;
    float roughnessValue = 0.85f;           // 粗さの図が無い時の値、ある時は倍率
    MATH::Vec3 baseColor{1.0f, 1.0f, 1.0f}; // 色の図に掛ける（線形）
    std::string license{};
    std::string author{};
    std::string source{};
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
    field("roughnessValue", &S::roughnessValue, Range{0.0, 2.0}, Label{"Roughness"});
    field("baseColor", &S::baseColor, Label{"Base Color"});
    field("license", &S::license, Label{"License"});
    field("author", &S::author, Label{"Author"});
    field("source", &S::source, Label{"Source"});
};

struct CookedSurface {
    TerrainSurfaceAsset asset{};
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
};

} // namespace HIKARI::TERRAINKIT
