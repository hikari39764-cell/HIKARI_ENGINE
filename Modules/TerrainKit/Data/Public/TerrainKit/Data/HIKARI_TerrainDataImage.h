#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// 高さ画像の読み書き。Gaea・World Machine・Houdini と EXR（32/16 bit 浮動小数）か R16（16 bit 符号なしの生データ）で
// やり取りする。EXR は値をそのまま、R16 は 0..1 に正規化して返す。
namespace HIKARI::TERRAIN::DATA {

struct HeightImage {
    uint32_t width = 0u;
    uint32_t height = 0u;
    std::vector<float> values{}; // 行優先、x が内側

    bool Valid() const noexcept {
        return width >= 2u && height >= 2u && values.size() == static_cast<std::size_t>(width) * height;
    }
    bool operator==(const HeightImage&) const = default;
};

// 拡張子で形式を選ぶ（.exr / .r16 / .raw）。R16 の幅は rawWidth、0 なら正方形とみなす。
bool LoadHeightImage(const std::filesystem::path& path, uint32_t rawWidth, HeightImage& out, std::string& error);
// 単一チャンネル（Y）の 32 bit 浮動小数 EXR で書く。
bool SaveHeightImageExr(const std::filesystem::path& path, const HeightImage& image, std::string& error);
// 0..1 の値を 16 bit 符号なしの生データで書く（範囲外は丸める）。
bool SaveHeightImageRaw16(const std::filesystem::path& path, const HeightImage& image, std::string& error);

// 画素座標（0..width-1）で双線形に読む。範囲外は端の画素。
float SampleHeightImage(const HeightImage& image, double pixelX, double pixelY) noexcept;

} // namespace HIKARI::TERRAIN::DATA
