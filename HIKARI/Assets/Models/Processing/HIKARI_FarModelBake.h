#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace HIKARI {

class ModelAsset;
struct TextureAsset3D;

// 模型の一つの網を、遠景用の模型へ焼く。遠景模型は縦の面 3 枚と高さ別の水平の切片 3 枚で、色と被覆率と外向きの法線を持つ。
// 水平の切片は高さごとの枝の半径に合わせてあるので、水平に見ても輪郭の外へ出ず、見下ろすと重なって木の形になる。
// 原点と大きさは元の模型と同じなので、切り替えても位置がずれない。出力は普通の glTF と PNG で、普通に導入できる。
struct FarModelBakeRequest {
    uint32_t meshIndex = 0u;
    // 貼り絵の一辺の画素数。
    uint32_t atlasPixels = 1024u;
    std::filesystem::path outputDirectory{};
    std::string name{};
    // 模型の貼り絵の元画像（png や jpg）の絶対パスを返す。無ければ空。
    std::function<std::filesystem::path(const TextureAsset3D&)> resolveTexture{};
};

bool BakeFarModel(const ModelAsset& model, const FarModelBakeRequest& request, std::string& outMessage);

} // namespace HIKARI
