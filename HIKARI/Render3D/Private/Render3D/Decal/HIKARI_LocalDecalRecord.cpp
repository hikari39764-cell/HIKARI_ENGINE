#include "Render3D/Decal/HIKARI_LocalDecalRecord.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::RENDER3D::DECAL {
namespace {

// 箱の空間 [-0.5, 0.5]^3 からワールドへ。
MATH::Mat4 BoxToWorld(const SceneDecalProvider& decal) noexcept {
    return decal.world * MATH::Mat4::Scale(decal.settings.size);
}

MATH::Vec3 Axis(const MATH::Mat4& matrix, int column) noexcept {
    return {matrix.m[column][0], matrix.m[column][1], matrix.m[column][2]};
}

uint32_t PackUnorm8(float value) noexcept {
    return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

} // namespace

bool IsDecalUploadable(const SceneDecalProvider& decal) noexcept {
    const DecalSettings& settings = decal.settings;
    const bool affectsSurface = settings.colorWeight > 0.0f || settings.materialWeight > 0.0f ||
                                (settings.normalWeight > 0.0f && decal.normalTextureIndex != 0u);
    if (!settings.enabled || settings.opacity <= 0.0f || !affectsSurface || !MATH::IsFinite(decal.world)) {
        return false;
    }
    // 拡縮 0 の軸があると箱の空間へ戻せない。
    const MATH::Mat4 box = BoxToWorld(decal);
    const float determinant = MATH::Dot(MATH::Cross(Axis(box, 0), Axis(box, 1)), Axis(box, 2));
    return std::isfinite(determinant) && std::abs(determinant) > 1e-9f;
}

LIGHTING::LocalDecalGpuData BuildLocalDecalGpuData(const SceneDecalProvider& decal) noexcept {
    const DecalSettings& settings = decal.settings;
    LIGHTING::LocalDecalGpuData gpu{};
    const MATH::Mat4 worldToBox = MATH::Inverse(BoxToWorld(decal));
    for (int row = 0; row < 3; ++row) {
        gpu.worldToDecal[row] = {worldToBox.m[0][row], worldToBox.m[1][row], worldToBox.m[2][row],
                                 worldToBox.m[3][row]};
    }
    gpu.textures = static_cast<float>(decal.baseColorTextureIndex +
                                      decal.normalTextureIndex * LIGHTING::kLocalLightTextureIndexStride);
    gpu.angleFadeCos = std::cos(settings.angleFadeDegrees * std::numbers::pi_v<float> / 180.0f);
    gpu.kind = static_cast<uint32_t>(LIGHTING::LocalLightType::Decal) | PackUnorm8(settings.color.x) << 8u |
               PackUnorm8(settings.color.y) << 16u | PackUnorm8(settings.color.z) << 24u;
    gpu.colorWeight = settings.opacity * settings.colorWeight;
    gpu.normalWeight = decal.normalTextureIndex != 0u ? settings.opacity * settings.normalWeight : 0.0f;
    gpu.materialWeight = settings.opacity * settings.materialWeight;
    gpu.roughness = settings.roughness;
    gpu.metallic = settings.metallic;
    return gpu;
}

void ResolveDecalBoundingSphere(const SceneDecalProvider& decal, MATH::Vec3& center, float& radius) noexcept {
    const MATH::Mat4 box = BoxToWorld(decal);
    center = {box.m[3][0], box.m[3][1], box.m[3][2]};
    const MATH::Vec3 x = Axis(box, 0);
    const MATH::Vec3 y = Axis(box, 1);
    const MATH::Vec3 z = Axis(box, 2);
    radius = 0.5f * std::sqrt(MATH::Dot(x, x) + MATH::Dot(y, y) + MATH::Dot(z, z));
}

} // namespace HIKARI::RENDER3D::DECAL
