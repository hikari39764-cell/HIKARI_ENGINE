#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "RenderCore/Resources/HIKARI_RenderResourcePool.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::RAYTRACING {
struct RuntimeRayLightingFrame;
}

namespace HIKARI::RENDER3D::SCREENSPACE {

struct RayTracedShadowInputs {
    RenderTextureView sceneDepth{};
    // GeometryAux の面の記述（着色の法線）。無いフレームは影を作らず、Forward はカスケード影を読む。
    RenderTextureView packedSurface{};
    const MESHRENDERER::CameraCB* camera = nullptr;
    const RAYTRACING::RuntimeRayLightingFrame* rayLighting = nullptr;
    const MESHRENDERER::MeshMaterialGpuFrameView* materials = nullptr;
    MATH::Vec3 directionToSun{0.0f, 1.0f, 0.0f};
    float coneHalfAngle = 0.0f; // 太陽の角半径 × 半影の倍率 [rad]
    float maxDistance = 100.0f; // 影の距離 [m]
    float normalOffset = 0.02f; // 始点を面から浮かせる距離 [m]
    uint32_t sampleCount = 2u;
    uint32_t frameIndex = 0u;
    const TEMPORAL::TemporalFrameState* temporal = nullptr;
};

// 光線追跡の太陽の影。場面の深度から画素ごとに太陽の円錐へ影光線を飛ばし、可視率（R8, 1 = 照らされる）を書き、
// 同じ面の近傍で空間フィルターを掛け、独立した履歴（RGBA16F: 可視率・法線・距離）で安定化して Forward へ渡す。
// 加速構造・材質・局所光の束ね方は光線クエリの共通根パラメーター（RuntimeRayQueryBindings）に従う。
class RayTracedShadowRenderer final {
public:
    RayTracedShadowRenderer() = default;
    RayTracedShadowRenderer(const RayTracedShadowRenderer&) = delete;
    RayTracedShadowRenderer& operator=(const RayTracedShadowRenderer&) = delete;

    bool Render(const GFX::Context& graphics, const RayTracedShadowInputs& inputs);
    // 最後に描いた（フィルター済みの）可視率。描いていなければ無効。
    RenderTextureView GetVisibilityView() const noexcept;
    void Release();

private:
    // 可視率の面：生の追跡結果（raw）とフィルター済み（filtered）。
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
        RenderResourceView srv{};
        RenderResourceView uav{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    bool EnsurePipeline(const GFX::Context& graphics);
    bool EnsureTargets(const GFX::Context& graphics, uint32_t width, uint32_t height);
    bool CreateTarget(const GFX::Context& graphics, Target& target, uint32_t width, uint32_t height, DXGI_FORMAT format,
                      const wchar_t* name);
    void ReleaseTargets(const GFX::Context& graphics);
    void ReleaseTarget(const GFX::Context& graphics, Target& target);
    static void Transition(ID3D12GraphicsCommandList* commands, Target& target, D3D12_RESOURCE_STATES next);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tracePipeline_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filterPipeline_{};
    Target raw_{};
    Target filtered_[2]{};
    uint32_t writeIndex_ = 0;
    uint64_t historyFrame_ = UINT64_MAX;
    TEMPORAL::TemporalViewLease historyView_{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    uint64_t deviceGeneration_ = 0u;
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
    bool pipelineFailed_ = false;
    bool valid_ = false;
};

} // namespace HIKARI::RENDER3D::SCREENSPACE
