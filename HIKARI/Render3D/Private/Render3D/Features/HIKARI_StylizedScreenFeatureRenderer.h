#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Features/HIKARI_RenderFeatureTypes.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::FEATURES {

struct StylizedScreenFeatureSettings {
    float thicknessPixels = 1.25f;
    float depthThreshold = 0.0025f;
    float normalThreshold = 0.16f;
    float intensity = 0.92f;
    float outlineColor[3]{0.025f, 0.030f, 0.045f};
    MATH::Mat4 viewProj{};
    float directionalLightWS[3]{0.4f, -1.0f, -0.6f};
    float faceHairShadowScale = 1.0f;
    float faceHairDepthTolerance = 0.004f;
};

struct StylizedScreenFeatureDebugState {
    bool requested = false;
    bool inputsReady = false;
    bool materialStyleReady = false;
    bool pipelineReady = false;
    bool rendered = false;
    uint32_t scheduledFeatureMask = 0u;
    uint32_t passCount = 0u;
    uint64_t transientBytes = 0u;
};

class StylizedScreenFeatureRenderer final {
public:
    bool Render(const GFX::Context& graphics, const PIPELINE::RenderTargetAccess& targetAccess,
                const SURFACE::SceneSurfaceSet& surfaces, const RenderFeaturePlan& plan,
                const StylizedScreenFeatureSettings& settings = {});
    void Release();

    const StylizedScreenFeatureDebugState& GetDebugState() const noexcept { return debugState_; }

private:
    bool EnsurePipeline(ID3D12Device* device);
    bool EnsureConstantBuffer(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_{};
    uint8_t* constantMapped_ = nullptr;
    uint32_t constantStride_ = 0u;
    StylizedScreenFeatureDebugState debugState_{};
};

} // namespace HIKARI::RENDER3D::FEATURES
