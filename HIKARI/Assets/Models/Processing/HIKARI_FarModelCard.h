#pragma once

#include <array>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

// 遠景模型の焼き込みの内部：面を一枚ずつ塗って貼り絵へ写す。
namespace HIKARI::ASSETS::MODELS {

// 貼り絵の中で面どうしの間に空ける画素。
inline constexpr uint32_t kFarModelCardMargin = 2u;

using MATH::Vec2;
using MATH::Vec3;

// 読み取り専用の RGBA8 画像。
struct CpuImage {
    uint32_t width = 0u;
    uint32_t height = 0u;
    std::vector<uint8_t> rgba{};

    // 線形の色と透明度を bilinear で読む。
    void Sample(Vec2 uv, float outColor[4]) const noexcept;
};

bool LoadCpuImage(const std::filesystem::path& path, CpuImage& out, std::string& outMessage);

struct BakeVertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
};

struct BakeMaterial {
    float factor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float cutoff = 0.0f;
    const CpuImage* baseColor = nullptr;
};

struct BakeTriangle {
    std::array<BakeVertex, 3> vertices{};
    uint32_t material = 0u;
};

// 遠景模型の一枚の面。
struct BakeCard {
    Vec3 right{};
    Vec3 up{};
    Vec3 toCamera{};
    Vec3 center{};
    float halfU = 1.0f;
    float halfV = 1.0f;
    // 水平の切片が焼く世界の高さの範囲。
    float minHeight = -FLT_MAX;
    float maxHeight = FLT_MAX;
    bool slice = false;
    uint32_t originX = 0u, originY = 0u, cell = 0u;
};

// 面を塗り、貼り絵の区画へ写す。貼り絵は RGBA8 で、一辺が atlasPixels。他の面と区画が重ならなければ並列に呼べる。
void RenderCardIntoAtlas(const BakeCard& card, const std::vector<BakeTriangle>& triangles,
                         const std::vector<BakeMaterial>& materials, uint32_t atlasPixels, uint8_t* colorAtlas,
                         uint8_t* normalAtlas);

} // namespace HIKARI::ASSETS::MODELS
