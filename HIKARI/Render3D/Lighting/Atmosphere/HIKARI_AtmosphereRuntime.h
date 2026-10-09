#pragma once

#include <cstdint>
#include <array>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI::ATMOSPHERE {

// 大気 LUT の視点（ルート定数 b1）。カメラ位置の y は地表からの高さ [m] として扱い、
// 空の LUT の観測者の高さと空気遠近の視線に使う。
struct AtmosphereViewConstants {
    MATH::Mat4 inverseViewProj{};
    // xyz カメラ位置 [m]、w このビューの Pre-Exposure。
    MATH::Vec4 cameraPreExposure{};
    // x 空気遠近の最大距離 [m]、y 雲の陰の境の高度 [m]（これより下の空気が雲の下。雲が無ければ 0）。
    MATH::Vec4 params{};
    // 空気遠近への雲の陰。rgb 雲の下から見上げた空の平均輝度 [nits]、w 雲を抜けて空気へ届く主光源の割合。
    MATH::Vec4 cloudShading{0.0f, 0.0f, 0.0f, 1.0f};
};
static_assert(sizeof(AtmosphereViewConstants) == 112u);

// 空気遠近の体積 LUT の解像度と奥行き。奥行きは d = maxDistance * w^2（w = (slice + 1) / slices）。
inline constexpr uint32_t kAerialPerspectiveResolution = 32u;
inline constexpr uint32_t kAerialPerspectiveSlices = 32u;
inline constexpr float kAerialPerspectiveMaxDistance = 32000.0f;
// ビューごとに別の体積へ書く。合成までの間に別ビュー（画面テクスチャのカメラなど）が空を描いても上書きしない。
inline constexpr uint32_t kAerialPerspectiveRingSize = 4u;

// Owns the reusable transmittance, multi-scattering, and sky-view LUTs.
// The owner deliberately keeps the resources local to the atmosphere
// stage: their SRV/UAV descriptors and default-heap textures are retired
// through the frame fence before a configuration rebuild.
class AtmosphereLutOwner final {
public:
    AtmosphereLutOwner() = default;
    ~AtmosphereLutOwner();

    AtmosphereLutOwner(const AtmosphereLutOwner&) = delete;
    AtmosphereLutOwner& operator=(const AtmosphereLutOwner&) = delete;

    bool Configure(const AtmosphereSettings& settings, bool enabled);
    void SetContext(const GFX::Context& context) noexcept { lastContext_ = context; }
    void Invalidate();
    void Reset();

    // Allocates all three UAV/SRV pairs and the shared compute pipeline.
    // Returns false when the descriptor pool, device, or cooked shaders
    // cannot provide a complete GPU path; callers must use analytic sky
    // in that case.
    bool EnsureGpuResources(const GFX::Context& context);

    // Dispatches only the LUTs whose typed transport inputs changed. The
    // final state is ALL_SHADER_RESOURCE for the sky pixel pass and the
    // environment convolution compute pass. 空の LUT は観測者の高さ（view のカメラ y）が
    // 約 4% 変わった時にも作り直す。
    bool Dispatch(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
                  const AtmosphereLutConstants& constants, const AtmosphereViewConstants& view);

    // このビューの空気遠近の体積 LUT を書き、その SRV を返す（失敗時は 0）。Dispatch で LUT が揃った後に呼ぶ。
    D3D12_GPU_DESCRIPTOR_HANDLE DispatchAerialPerspective(const GFX::Context& context,
                                                          D3D12_GPU_VIRTUAL_ADDRESS constantsGpu,
                                                          const AtmosphereViewConstants& view);

    uint32_t GetLastDispatchCount() const noexcept { return lastDispatchCount_; }
    uint64_t GetWorkingSetBytes() const noexcept;

    const RENDER3D::RenderResourceView& GetSkyViewSrv() const noexcept { return skyViewGpu_.srv; }

private:
    struct AtmosphereLutDimensions {
        uint32_t transmittanceWidth = 256u;
        uint32_t transmittanceHeight = 64u;
        uint32_t multiScatteringSize = 32u;
        uint32_t skyViewWidth = 192u;
        uint32_t skyViewHeight = 108u;
    };

    struct GpuLut {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
        RENDER3D::RenderResourceView srv{};
        RENDER3D::RenderResourceView uav{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };

    bool EnsurePipeline(const GFX::Context& context);
    bool CreateLut(const GFX::Context& context, GpuLut& lut, uint32_t width, uint32_t height, uint32_t depth,
                   const wchar_t* debugName);
    void RetireView(const GFX::Context& context, RENDER3D::RenderResourceView& view, const char* debugName);
    void RetireResource(const GFX::Context& context, Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                        const char* debugName);
    void Transition(ID3D12GraphicsCommandList* commandList, GpuLut& lut, D3D12_RESOURCE_STATES next);

    AtmosphereLutDimensions dimensions_{};
    GpuLut transmittanceGpu_{};
    GpuLut multiScatteringGpu_{};
    GpuLut skyViewGpu_{};
    std::array<GpuLut, kAerialPerspectiveRingSize> aerialPerspectiveGpu_{};
    uint32_t aerialPerspectiveCursor_ = 0u;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRoot_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> transmittancePso_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> multiScatteringPso_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyViewPso_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> aerialPerspectivePso_{};
    GFX::Context lastContext_{};
    std::array<uint64_t, 3> lastDispatchSignatures_{};
    uint32_t lastDispatchCount_ = 0u;
    bool resourcesReady_ = false;
    bool pipelineReady_ = false;
    bool configured_ = false;
    bool gpuLutsReady_ = false;
};

} // namespace HIKARI::ATMOSPHERE
