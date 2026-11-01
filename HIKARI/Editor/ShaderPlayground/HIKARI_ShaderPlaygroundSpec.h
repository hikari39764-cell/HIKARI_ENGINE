#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"

// ShaderPlayground の試験の設定（.json）。何を試すか（自分の .hlsl か描画様式の着色モデル）、どの場面で、
// どの値のタイルを並べるかを書く。読み込みでタイルごとの値を詰めた形（HLSL の gPlayTiles と同じ並び）まで作る。
//
//   {
//     "formatVersion": 1,
//     "source": "MyToon.hlsl",            省略すると描画様式の着色モデル（style の値）を試す
//     "compare": "MyToonOld.hlsl",        任意：同じタイルで描いて差を出す
//     "scene": "sphere",                  sphere / furnace / uv / image / curve / ramp
//     "image": "photo.png",               scene が image のときの入力（省略で試験図）
//     "parameters": [{"name": "bands", "value": 3}],    HLSL の PLAY_PARAM(s, bands)
//     "defaults": {"roughness": 0.4, "style": {"model": "Cel"}},
//     "tiles": [{"label": "2 段", "set": {"bands": 2}}],  行。省略すると 1 行
//     "sweep": {"key": "roughness", "from": 0, "to": 1, "count": 6}  列。省略すると 1 列
//   }
//
// set と defaults のキー：baseColor, roughness, metallic, lightAzimuth, lightElevation（度）, lightIntensity,
// lightColor, ambient, time、parameters の名前、style（描画様式の着色の値）。掃引の key は数値のキーか
// parameters の名前（x を変える）か style.<名前>。
namespace HIKARI::EDITOR::PLAYGROUND {

inline constexpr uint32_t kSpecFormatVersion = 1u;
// HLSL の kPlaygroundMaxTiles / kPlaygroundTileSlots / kPlaygroundParamSlots と対。
inline constexpr uint32_t kMaxTiles = 64u;
inline constexpr uint32_t kTileSlots = 16u;
inline constexpr uint32_t kParamSlots = 12u;
// 描画様式の着色モデルを試すとき params[0..1] が詰めた様式の値になる。
inline constexpr uint32_t kStyleParamSlots = 2u;

// HLSL の kPlaygroundScene* と対。
enum class Scene : uint32_t { Sphere, Furnace, Uv, Image, Curve, Ramp };
inline constexpr TEXT::EnumName<Scene> kSceneNames[] = {
    {Scene::Sphere, "sphere"}, {Scene::Furnace, "furnace"}, {Scene::Uv, "uv"},
    {Scene::Image, "image"},   {Scene::Curve, "curve"},     {Scene::Ramp, "ramp"},
};

enum class Device : uint8_t { Gpu, Warp };
inline constexpr TEXT::EnumName<Device> kDeviceNames[] = {{Device::Gpu, "gpu"}, {Device::Warp, "warp"}};

struct Tile {
    std::string label;
    std::array<MATH::Vec4, kTileSlots> slots{};
};

struct Spec {
    std::string name;
    std::filesystem::path directory; // 設定のあるフォルダ。source / compare / image はここからの相対で、外へ出られない
    std::filesystem::path source;    // 空なら描画様式の着色モデル
    std::filesystem::path compare;
    std::filesystem::path image;
    Scene scene = Scene::Sphere;
    Device device = Device::Gpu;
    uint32_t tileSize = 160u;
    uint32_t columns = 0u; // タイルの横の数。0 は掃引の数（掃引が無ければ最大 6 で折り返す）
    uint32_t gap = 4u;
    float exposure = 1.0f;
    MATH::Vec2 curveRange{-0.25f, 1.25f};
    MATH::Vec3 ambientColor{1.0f, 1.0f, 1.0f};
    // 0 より大きいと、これを超える値を問題として数える（furnace の既定は 1.001）。
    float maxValue = 0.0f;
    std::vector<std::string> parameterNames; // 並びが PLAY_PARAM の番号
    uint32_t parameterBase = 0u;             // 描画様式の着色モデルなら kStyleParamSlots
    std::vector<Tile> tiles;
};

bool ReadSpec(const std::filesystem::path& path, Spec& outSpec, std::string& outError);

} // namespace HIKARI::EDITOR::PLAYGROUND
