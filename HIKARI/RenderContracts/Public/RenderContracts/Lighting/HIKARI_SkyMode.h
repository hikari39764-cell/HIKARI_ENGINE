#pragma once

namespace HIKARI {

// 空の描画経路。環境プロファイルの SkySource から決まり、読み込みに失敗すると None になる。
// shader が数値で参照する（2 立方体、3 全景、4 大気）。
enum class SkyMode {
    None = 0,
    Cubemap = 2,
    Texture2D = 3,
    Atmosphere = 4,
};

} // namespace HIKARI
