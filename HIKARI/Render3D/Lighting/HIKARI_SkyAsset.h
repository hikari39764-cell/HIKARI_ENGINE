#pragma once

#include <string>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

// 空の描画経路。環境プロファイルの SkySource から決まり、読み込みに失敗すると None になる。
// shader が数値で参照する（2 立方体、3 全景、4 大気）。
enum class SkyMode {
    None = 0,
    Cubemap = 2,
    Texture2D = 3,
    Atmosphere = 4,
};

constexpr SkyMode ResolveSkyRenderMode(SkySource source) noexcept {
    switch (source) {
    case SkySource::Hdri: return SkyMode::Cubemap;
    case SkySource::Panorama: return SkyMode::Texture2D;
    case SkySource::Atmosphere:
    default: return SkyMode::Atmosphere;
    }
}

struct SkyAsset {
    std::string name;
    std::string meshAssetName;
    std::string texturePath;
    SkyMode preferredMode = SkyMode::Cubemap;
    bool textureIsLinear = false;
};

} // namespace HIKARI
