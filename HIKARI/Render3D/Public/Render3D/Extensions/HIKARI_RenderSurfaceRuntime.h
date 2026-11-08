#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI/Project/RenderSurface.h"

namespace HIKARI::RENDER3D::SURFACE_EXTENSIONS {

// 視点の描画の先頭で一度。モジュールに資源の準備をさせる。
void BeginFrame(const GFX::Context& graphics, std::span<IRenderSurfaceExtension* const> extensions);
bool HasWork() noexcept;

struct RecordInput {
    RenderSurfacePass pass = RenderSurfacePass::Forward;
    uint32_t shadowCascade = 0u;
    bool geometryFeatureMask = false;
    MATH::Mat4 viewProjection{};
    MATH::Vec3 cameraPosition{};
};

// 主相机の位置。影の視点も遠近分級をこれに揃える。
void SetFrameCameraPosition(const MATH::Vec3& position) noexcept;
const MATH::Vec3& GetFrameCameraPosition() noexcept;

// 呼び手が root signature を束縛し、描画先と viewport を設定した状態で呼ぶ。
bool Record(const GFX::Context& graphics, ID3D12RootSignature* rootSignature, const RecordInput& input);

void Shutdown(const GFX::Context* graphics) noexcept;

} // namespace HIKARI::RENDER3D::SURFACE_EXTENSIONS
