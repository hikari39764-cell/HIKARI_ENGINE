#include "HIKARI_FarModelCard.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace HIKARI::ASSETS::MODELS {
namespace {

// 面の一画素を kSupersample² 個に割って塗る（細い葉の被覆率を取る）。
constexpr uint32_t kSupersample = 4u;
// 法線は木の塊としての外向きを主にして、葉の向きを少し混ぜる。
constexpr float kOutwardNormalWeight = 0.65f;
// 木の中心より奥で見つかった面の明るさ（葉の間の陰）。
constexpr float kOcclusionFloor = 0.45f;

float SrgbToLinear(uint8_t value) noexcept {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> result{};
        for (size_t index = 0u; index < result.size(); ++index) {
            const float c = float(index) / 255.0f;
            result[index] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return result;
    }();
    return table[value];
}

float LinearToSrgb(float value) noexcept {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow((std::max)(value, 1.0e-6f), 1.0f / 2.4f) - 0.055f;
}

uint8_t ToUnorm8(float value) noexcept { return uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); }

// 正射影で塗り、一画素へ平均する。color は線形の RGB と被覆率、normal は面の接空間。
void RenderCard(const BakeCard& card, const std::vector<BakeTriangle>& triangles,
                const std::vector<BakeMaterial>& materials, float* outColor, float* outNormal) {
    const uint32_t inner = card.cell - kFarModelCardMargin * 2u;
    const uint32_t size = inner * kSupersample;
    const size_t count = size_t(size) * size;
    std::vector<float> depth(count, -FLT_MAX);
    std::vector<std::array<float, 3>> color(count);
    std::vector<std::array<float, 3>> normal(count);
    const float toPixelU = float(size) * 0.5f / card.halfU;
    const float toPixelV = float(size) * 0.5f / card.halfV;
    for (const BakeTriangle& triangle : triangles) {
        const BakeMaterial& material = materials[triangle.material];
        const BakeVertex& a = triangle.vertices[0];
        const BakeVertex& b = triangle.vertices[1];
        const BakeVertex& c = triangle.vertices[2];
        float sx[3], sy[3], sz[3];
        for (int i = 0; i < 3; ++i) {
            const Vec3 offset = triangle.vertices[size_t(i)].position - card.center;
            sx[i] = (MATH::Dot(offset, card.right) + card.halfU) * toPixelU;
            sy[i] = (card.halfV - MATH::Dot(offset, card.up)) * toPixelV;
            sz[i] = MATH::Dot(offset, card.toCamera);
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::abs(area) < 1.0e-6f) {
            continue;
        }
        const int minX = (std::max)(int(std::floor((std::min)({sx[0], sx[1], sx[2]}))), 0);
        const int maxX = (std::min)(int(std::ceil((std::max)({sx[0], sx[1], sx[2]}))), int(size) - 1);
        const int minY = (std::max)(int(std::floor((std::min)({sy[0], sy[1], sy[2]}))), 0);
        const int maxY = (std::min)(int(std::ceil((std::max)({sy[0], sy[1], sy[2]}))), int(size) - 1);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = float(x) + 0.5f;
                const float py = float(y) + 0.5f;
                const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                const size_t index = size_t(y) * size + size_t(x);
                const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                const float height = a.position.y * w0 + b.position.y * w1 + c.position.y * w2;
                if (z <= depth[index] || height < card.minHeight || height > card.maxHeight) {
                    continue;
                }
                float texel[4]{1.0f, 1.0f, 1.0f, 1.0f};
                if (material.baseColor != nullptr) {
                    material.baseColor->Sample(a.uv * w0 + b.uv * w1 + c.uv * w2, texel);
                }
                if (texel[3] * material.factor[3] < material.cutoff) {
                    continue;
                }
                depth[index] = z;
                const float front = std::clamp((z + card.halfU * 0.3f) / (card.halfU * 0.8f), 0.0f, 1.0f);
                const float occlusion = card.slice ? 1.0f : kOcclusionFloor + (1.0f - kOcclusionFloor) * front;
                color[index] = {texel[0] * material.factor[0] * occlusion, texel[1] * material.factor[1] * occlusion,
                                texel[2] * material.factor[2] * occlusion};
                Vec3 world = a.normal * w0 + b.normal * w1 + c.normal * w2;
                // 葉は両面なので、見ている側を向く法線へ揃える。
                if (MATH::Dot(world, card.toCamera) < 0.0f) {
                    world = world * -1.0f;
                }
                normal[index] = {MATH::Dot(world, card.right), MATH::Dot(world, card.up),
                                 MATH::Dot(world, card.toCamera)};
            }
        }
    }
    for (uint32_t y = 0u; y < inner; ++y) {
        const float planeV = 1.0f - (float(y) + 0.5f) * 2.0f / float(inner);
        for (uint32_t x = 0u; x < inner; ++x) {
            const float planeU = (float(x) + 0.5f) * 2.0f / float(inner) - 1.0f;
            float sum[3]{}, normalSum[3]{};
            uint32_t covered = 0u;
            for (uint32_t sampleY = 0u; sampleY < kSupersample; ++sampleY) {
                for (uint32_t sampleX = 0u; sampleX < kSupersample; ++sampleX) {
                    const size_t index = size_t(y * kSupersample + sampleY) * size + x * kSupersample + sampleX;
                    if (depth[index] == -FLT_MAX) {
                        continue;
                    }
                    ++covered;
                    for (size_t channel = 0u; channel < 3u; ++channel) {
                        sum[channel] += color[index][channel];
                        normalSum[channel] += normal[index][channel];
                    }
                }
            }
            float* pixel = outColor + (size_t(y) * inner + x) * 4u;
            float* tangent = outNormal + (size_t(y) * inner + x) * 3u;
            pixel[3] = float(covered) / float(kSupersample * kSupersample);
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = covered != 0u ? sum[channel] / float(covered) : 0.0f;
            }
            // 外向き：縦の面は円柱（上を少し向く）、切片は上向きで縁が少し開く。
            const Vec3 outward =
                MATH::Normalize(card.slice ? Vec3{planeU * 0.4f, planeV * 0.4f, 1.0f}
                                           : Vec3{planeU, 0.35f, std::sqrt((std::max)(1.0f - planeU * planeU, 0.05f))});
            const float length =
                std::sqrt(normalSum[0] * normalSum[0] + normalSum[1] * normalSum[1] + normalSum[2] * normalSum[2]);
            const float detail = length > 1.0e-5f ? (1.0f - kOutwardNormalWeight) / length : 0.0f;
            const Vec3 mixed = MATH::Normalize(Vec3{normalSum[0], normalSum[1], normalSum[2]} * detail +
                                               outward * kOutwardNormalWeight);
            tangent[0] = mixed.x;
            tangent[1] = mixed.y;
            tangent[2] = mixed.z;
        }
    }
}

// 透明な画素へ周りの色を広げる（mip と線形補間で縁が暗くならない）。
void DilateColor(float* rgba, uint32_t size) {
    std::vector<uint8_t> known(size_t(size) * size);
    for (size_t index = 0u; index < known.size(); ++index) {
        known[index] = rgba[index * 4u + 3u] > 0.02f ? 1u : 0u;
    }
    for (uint32_t iteration = 0u; iteration < size; ++iteration) {
        std::vector<uint8_t> next = known;
        bool changed = false;
        for (uint32_t y = 0u; y < size; ++y) {
            for (uint32_t x = 0u; x < size; ++x) {
                const size_t index = size_t(y) * size + x;
                if (known[index] != 0u) {
                    continue;
                }
                float sum[3]{};
                int found = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = int(x) + dx;
                        const int ny = int(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= int(size) || ny >= int(size) ||
                            known[size_t(ny) * size + size_t(nx)] == 0u) {
                            continue;
                        }
                        const float* other = rgba + (size_t(ny) * size + size_t(nx)) * 4u;
                        sum[0] += other[0];
                        sum[1] += other[1];
                        sum[2] += other[2];
                        ++found;
                    }
                }
                if (found != 0) {
                    for (int channel = 0; channel < 3; ++channel) {
                        rgba[index * 4u + size_t(channel)] = sum[channel] / float(found);
                    }
                    next[index] = 1u;
                    changed = true;
                }
            }
        }
        known.swap(next);
        if (!changed) {
            break;
        }
    }
}

} // namespace

void CpuImage::Sample(Vec2 uv, float outColor[4]) const noexcept {
    const float x = (uv.x - std::floor(uv.x)) * float(width) - 0.5f;
    const float y = (uv.y - std::floor(uv.y)) * float(height) - 0.5f;
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const float fx = x - float(x0);
    const float fy = y - float(y0);
    for (int channel = 0; channel < 4; ++channel) {
        outColor[channel] = 0.0f;
    }
    for (int corner = 0; corner < 4; ++corner) {
        const uint32_t px = uint32_t(x0 + (corner & 1) + int(width)) % width;
        const uint32_t py = uint32_t(y0 + (corner >> 1) + int(height)) % height;
        const float weight = ((corner & 1) != 0 ? fx : 1.0f - fx) * ((corner >> 1) != 0 ? fy : 1.0f - fy);
        const uint8_t* texel = rgba.data() + (size_t(py) * width + px) * 4u;
        for (int channel = 0; channel < 3; ++channel) {
            outColor[channel] += SrgbToLinear(texel[channel]) * weight;
        }
        outColor[3] += float(texel[3]) / 255.0f * weight;
    }
}

bool LoadCpuImage(const std::filesystem::path& path, CpuImage& out, std::string& outMessage) {
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage loaded{};
    if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, loaded))) {
        outMessage = "Cannot read texture: " + path.generic_string();
        return false;
    }
    const DirectX::Image* image = loaded.GetImage(0u, 0u, 0u);
    DirectX::ScratchImage converted{};
    if (metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                    DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
            outMessage = "Cannot convert texture: " + path.generic_string();
            return false;
        }
        image = converted.GetImage(0u, 0u, 0u);
    }
    out.width = uint32_t(image->width);
    out.height = uint32_t(image->height);
    out.rgba.resize(size_t(out.width) * out.height * 4u);
    for (uint32_t y = 0u; y < out.height; ++y) {
        std::memcpy(out.rgba.data() + size_t(y) * out.width * 4u, image->pixels + size_t(y) * image->rowPitch,
                    size_t(out.width) * 4u);
    }
    return true;
}

void RenderCardIntoAtlas(const BakeCard& card, const std::vector<BakeTriangle>& triangles,
                         const std::vector<BakeMaterial>& materials, uint32_t atlasPixels, uint8_t* colorAtlas,
                         uint8_t* normalAtlas) {
    const uint32_t inner = card.cell - kFarModelCardMargin * 2u;
    std::vector<float> color(size_t(inner) * inner * 4u);
    std::vector<float> normal(size_t(inner) * inner * 3u);
    RenderCard(card, triangles, materials, color.data(), normal.data());
    DilateColor(color.data(), inner);
    // 余白は端の画素を伸ばす。
    for (uint32_t y = 0u; y < card.cell; ++y) {
        const uint32_t sourceY = uint32_t(std::clamp(int(y) - int(kFarModelCardMargin), 0, int(inner) - 1));
        for (uint32_t x = 0u; x < card.cell; ++x) {
            const uint32_t sourceX = uint32_t(std::clamp(int(x) - int(kFarModelCardMargin), 0, int(inner) - 1));
            const float* pixel = color.data() + (size_t(sourceY) * inner + sourceX) * 4u;
            const float* tangent = normal.data() + (size_t(sourceY) * inner + sourceX) * 3u;
            const size_t target = size_t(card.originY + y) * atlasPixels + card.originX + x;
            uint8_t* outPixel = colorAtlas + target * 4u;
            uint8_t* outNormal = normalAtlas + target * 4u;
            for (int channel = 0; channel < 3; ++channel) {
                outPixel[channel] = ToUnorm8(LinearToSrgb(pixel[channel]));
                outNormal[channel] = ToUnorm8(tangent[channel] * 0.5f + 0.5f);
            }
            outPixel[3] = ToUnorm8(pixel[3]);
        }
    }
}

} // namespace HIKARI::ASSETS::MODELS
