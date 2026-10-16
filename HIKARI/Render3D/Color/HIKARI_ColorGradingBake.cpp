#include "Render3D/Color/HIKARI_ColorGradingBake.h"
#include "Core/Math/HIKARI_ScalarMath.h"

#include <algorithm>
#include <cmath>

#include <DirectXPackedVector.h>

#include "Assets/Color/HIKARI_ColorLutAsset.h"

namespace HIKARI::RENDER3D::COLOR {
namespace {} // namespace

bool IsColorGradingActive(const ColorGradingSettings& settings, const ASSETS::COLOR::ColorLutAsset* lut) noexcept {
    const MATH::Vec3 one{1.0f, 1.0f, 1.0f};
    return settings.shadowsTint != one || settings.midtonesTint != one || settings.highlightsTint != one ||
           (lut != nullptr && settings.lutIntensity > 0.0f);
}

void BakeColorGradingLut(const ColorGradingSettings& settings, const ASSETS::COLOR::ColorLutAsset* lut, float gamma,
                         std::vector<uint16_t>& outRgbaHalf) {
    constexpr uint32_t size = kColorGradingLutSize;
    const float encode = 1.0f / (std::max)(gamma, 0.01f);
    // 倍率は線形の光に掛ける。符号化した値では x * gain^(1/gamma) と同じ。
    const auto encodedGain = [encode](const MATH::Vec3& tint) {
        return MATH::Vec3{std::pow(tint.x, encode), std::pow(tint.y, encode), std::pow(tint.z, encode)};
    };
    const MATH::Vec3 shadows = encodedGain(settings.shadowsTint);
    const MATH::Vec3 midtones = encodedGain(settings.midtonesTint);
    const MATH::Vec3 highlights = encodedGain(settings.highlightsTint);
    const float lutIntensity = lut != nullptr ? std::clamp(settings.lutIntensity, 0.0f, 1.0f) : 0.0f;
    outRgbaHalf.resize(static_cast<size_t>(size) * size * size * 4u);
    uint16_t* texel = outRgbaHalf.data();
    const float step = 1.0f / static_cast<float>(size - 1u);
    for (uint32_t b = 0; b < size; ++b) {
        for (uint32_t g = 0; g < size; ++g) {
            for (uint32_t r = 0; r < size; ++r) {
                MATH::Vec3 color{static_cast<float>(r) * step, static_cast<float>(g) * step,
                                 static_cast<float>(b) * step};
                // 符号化した輝度で暗部 [0, 0.4]・明部 [0.6, 1] の重みを分け、残りを中間に配る（重みの和は 1）。
                const float luma = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
                const float shadowWeight = 1.0f - MATH::SmoothStep(0.0f, 0.4f, luma);
                const float highlightWeight = MATH::SmoothStep(0.6f, 1.0f, luma);
                const float midtoneWeight = 1.0f - shadowWeight - highlightWeight;
                const MATH::Vec3 gain =
                    shadows * shadowWeight + midtones * midtoneWeight + highlights * highlightWeight;
                color = {std::clamp(color.x * gain.x, 0.0f, 1.0f), std::clamp(color.y * gain.y, 0.0f, 1.0f),
                         std::clamp(color.z * gain.z, 0.0f, 1.0f)};
                if (lutIntensity > 0.0f) {
                    color = color + (ASSETS::COLOR::SampleColorLut(*lut, color) - color) * lutIntensity;
                }
                texel[0] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(color.x, 0.0f, 1.0f));
                texel[1] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(color.y, 0.0f, 1.0f));
                texel[2] = DirectX::PackedVector::XMConvertFloatToHalf(std::clamp(color.z, 0.0f, 1.0f));
                texel[3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
                texel += 4;
            }
        }
    }
}

} // namespace HIKARI::RENDER3D::COLOR
