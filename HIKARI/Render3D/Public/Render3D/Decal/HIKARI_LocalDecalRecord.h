#pragma once

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Decal/HIKARI_DecalContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightGpuData.h"

// デカールの GPU 記録と、クラスタへ割り当てる境界。記録は局所光と同じバッファに光源の後ろへ並ぶ。
namespace HIKARI::RENDER3D::DECAL {

bool IsDecalUploadable(const SceneDecalProvider& decal) noexcept;
LIGHTING::LocalDecalGpuData BuildLocalDecalGpuData(const SceneDecalProvider& decal) noexcept;
// 箱を包む球。
void ResolveDecalBoundingSphere(const SceneDecalProvider& decal, MATH::Vec3& center, float& radius) noexcept;

} // namespace HIKARI::RENDER3D::DECAL
