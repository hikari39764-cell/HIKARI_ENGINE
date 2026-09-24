#include "Vfx/Post/HIKARI_GameUiBackdropComposite.h"

#include <algorithm>
#include <cstring>

#include <d3dx12.h>

#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {
namespace {
struct alignas(16) BackdropConstants {
    float inverseSize[2]{};
    float blurRadius = 0.0f;
    float progress = 0.0f;
    float dimAmount = 0.0f;
    float desaturation = 0.0f;
    float vignette = 0.0f;
    float padding = 0.0f;
    float tint[4]{};
};

static_assert(sizeof(BackdropConstants) == 48u);
} // namespace

GameUiBackdropComposite::~GameUiBackdropComposite() { Shutdown(); }

void GameUiBackdropComposite::UpdateContext(const GFX::Context& context) {
    if (context_.device != nullptr && context_.device != context.device) {
        Shutdown();
    }
    context_ = context;
}

void GameUiBackdropComposite::Shutdown() {
    if (constants_ != nullptr && mappedConstants_ != nullptr) {
        constants_->Unmap(0u, nullptr);
        mappedConstants_ = nullptr;
    }
    GFX::RetireD3D12ObjectForFrame(constants_, context_, "GameUiBackdropComposite.Constants");
    pixelShader_.Reset();
    constantStride_ = 0u;
}

bool GameUiBackdropComposite::EnsureResources() {
    if (context_.device == nullptr) {
        return false;
    }
    if (pixelShader_ == nullptr && !GFX::CompileShaderFileSm6(L"HIKARI/Shaders/Post_GameUiBackdrop.hlsl", "main",
                                                              GFX::ShaderStage::Pixel, pixelShader_.GetAddressOf())) {
        return false;
    }
    if (constants_ != nullptr && mappedConstants_ != nullptr) {
        return true;
    }
    constantStride_ = (sizeof(BackdropConstants) + 255u) & ~255u;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto description =
        CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(constantStride_) * GFX::kFrameResourceCount);
    if (!HIKARI_DX_CHECK(context_.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                  IID_PPV_ARGS(constants_.GetAddressOf())),
                         "GameUiBackdropComposite::CreateConstants") ||
        !HIKARI_DX_CHECK(constants_->Map(0u, nullptr, reinterpret_cast<void**>(&mappedConstants_)),
                         "GameUiBackdropComposite::MapConstants")) {
        Shutdown();
        return false;
    }
    return true;
}

bool GameUiBackdropComposite::Execute(RenderTarget2D& source, RenderTarget2D& destination, QuadDrawer& quad,
                                      const GameProjectUiBackdropRequest& request) {
    if (source.GetResource() == nullptr || destination.GetResource() == nullptr ||
        source.GetWidth() != destination.GetWidth() || source.GetHeight() != destination.GetHeight() ||
        !EnsureResources() || !quad.SetOutputFormat(destination.GetFormat())) {
        return false;
    }
    const uint32_t slot = context_.frameIndex % GFX::kFrameResourceCount;
    BackdropConstants local{};
    local.inverseSize[0] = 1.0f / static_cast<float>((std::max)(source.GetWidth(), 1));
    local.inverseSize[1] = 1.0f / static_cast<float>((std::max)(source.GetHeight(), 1));
    local.blurRadius = std::clamp(request.blurRadiusPixels, 0.0f, 24.0f);
    local.progress = std::clamp(request.progress, 0.0f, 1.0f);
    local.dimAmount = std::clamp(request.dimAmount, 0.0f, 0.95f);
    local.desaturation = std::clamp(request.desaturation, 0.0f, 1.0f);
    local.vignette = std::clamp(request.vignette, 0.0f, 1.0f);
    local.tint[0] = request.tint.r;
    local.tint[1] = request.tint.g;
    local.tint[2] = request.tint.b;
    local.tint[3] = request.tint.a;
    const UINT64 offset = static_cast<UINT64>(slot) * constantStride_;
    std::memcpy(mappedConstants_ + offset, &local, sizeof(local));

    destination.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
    quad.SetInputTexture(source.GetSrvHeap(), source.GetSrvGpu());
    if (!quad.SetPixelShader(pixelShader_.Get())) {
        destination.EndCapture();
        return false;
    }
    quad.SetConstantBuffer(constants_->GetGPUVirtualAddress() + offset);
    quad.DrawFullscreen();
    destination.EndCapture();
    return true;
}

} // namespace HIKARI::POST
