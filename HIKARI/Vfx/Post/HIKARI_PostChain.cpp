#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include <sstream>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI {
namespace POST {

void PostChain::Clear() { effects_.clear(); }

void PostChain::Add(PostEffect* effect) {
    if (!effect) {
        return;
    }
    effects_.push_back(effect);
}

void PostChain::UpdateContext(const GFX::Context& ctx) {
    context_ = ctx;
    ping_.UpdateContext(ctx);
    pong_.UpdateContext(ctx);
}

void PostChain::SetDebugName(std::string name) {
    debugName_ = std::move(name);
    ping_.SetDebugName(debugName_ + ".Ping");
    pong_.SetDebugName(debugName_ + ".Pong");
}

std::string PostChain::DumpState() const {
    std::ostringstream oss;
    oss << "[PostChain] name=" << debugName_ << " effectCount=" << effects_.size()
        << " tempsInitialized=" << tempsInitialized_ << "\n  " << ping_.DumpState() << "\n  " << pong_.DumpState();
    return oss.str();
}

void PostChain::Finalize() {
    ping_.Finalize();
    pong_.Finalize();
    tempsInitialized_ = false;
    effects_.clear();
}

bool PostChain::EnsureTempSize(int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }

    constexpr DXGI_FORMAT kPostChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool recreate = !tempsInitialized_ || ping_.GetResource() == nullptr || pong_.GetResource() == nullptr ||
                          ping_.GetFormat() != kPostChainFormat || pong_.GetFormat() != kPostChainFormat ||
                          ping_.GetWidth() != w || ping_.GetHeight() != h || pong_.GetWidth() != w ||
                          pong_.GetHeight() != h;
    if (!recreate) {
        return true;
    }

    // ping/pong は常に同じ世代として再生成し、片方だけ古い状態を
    // 後段へ公開しない。
    ping_.Finalize();
    pong_.Finalize();
    tempsInitialized_ = false;
    ping_.UpdateContext(context_);
    pong_.UpdateContext(context_);
    const bool okPing = ping_.Init(w, h, kPostChainFormat, false, {0.0f, 0.0f, 0.0f, 0.0f});
    const bool okPong = pong_.Init(w, h, kPostChainFormat, false, {0.0f, 0.0f, 0.0f, 0.0f});
    tempsInitialized_ = okPing && okPong;
    if (!tempsInitialized_) {
        ping_.Finalize();
        pong_.Finalize();
        DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] EnsureTempSize failed.\n") + DumpState());
    }
    return tempsInitialized_;
}

RenderTarget2D* PostChain::Execute(RenderTarget2D& src, QuadDrawer& quad, const CommonParams& params, int tempWidth,
                                   int tempHeight, const RENDER3D::RenderTextureView* surfaceFeatureMask,
                                   ID3D12Resource* sceneDepth) {
    if (effects_.empty()) {
        return &src;
    }
    if (surfaceFeatureMask &&
        (!surfaceFeatureMask->IsSampleable() || !surfaceFeatureMask->resource ||
         surfaceFeatureMask->format != DXGI_FORMAT_R32G32_UINT ||
         !(surfaceFeatureMask->state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) || !context_.device ||
         !context_.srvHeap || !context_.deferredReleaseQueue || context_.currentFrameRetireFenceValue == 0u))
        return &src;

    int w = tempWidth > 0 ? tempWidth : src.GetWidth();
    int h = tempHeight > 0 ? tempHeight : src.GetHeight();
    if (!EnsureTempSize(w, h) || ping_.GetResource() == nullptr || pong_.GetResource() == nullptr) {
        DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute skipped: temp buffers invalid.\n") +
                                  DumpState());
        return &src;
    }

    RenderTarget2D* cur = &src;

    for (size_t i = 0; i < effects_.size(); i++) {
        if (effects_[i] == nullptr) {
            DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute skipped null effect.\n") + DumpState());
            return &src;
        }
        RenderTarget2D* dst = nullptr;

        if (cur == &ping_) {
            dst = &pong_;
        } else if (cur == &pong_) {
            dst = &ping_;
        } else {
            dst = &ping_;
        }

        dst->BeginCapture(0, 0, 0, 0);

        std::vector<RENDER3D::RenderResourceView> inputDescriptors;
        if (surfaceFeatureMask || sceneDepth) {
            inputDescriptors = RENDER3D::AllocateRenderResourceDescriptorRange(3u);
            if (inputDescriptors.size() != 3u) {
                dst->EndCapture();
                return &src;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1u;
            view.Format = cur->GetFormat();
            context_.device->CreateShaderResourceView(cur->GetResource(), &view, inputDescriptors[0].cpu);
            view.Format = DXGI_FORMAT_R32_FLOAT;
            context_.device->CreateShaderResourceView(sceneDepth, &view, inputDescriptors[1].cpu);
            view.Format = surfaceFeatureMask ? surfaceFeatureMask->format : DXGI_FORMAT_R32G32_UINT;
            context_.device->CreateShaderResourceView(surfaceFeatureMask ? surfaceFeatureMask->resource : nullptr,
                                                      &view, inputDescriptors[2].cpu);
            quad.SetInputTexture(context_.srvHeap, inputDescriptors[0].gpu, inputDescriptors[1].gpu);
        } else {
            quad.SetInputTexture(cur->GetSrvHeap(), cur->GetSrvGpu());
        }
        // Descriptor retirement is deferred to this frame's fence on every exit.
        const auto retireInputs = [&]() {
            for (auto& view : inputDescriptors)
                RENDER3D::RetireRenderResourceDescriptor(view, context_, "PostChain.SurfaceInputs");
        };
        if (!quad.SetOutputFormat(dst->GetFormat())) {
            DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute SetOutputFormat failed.\n") +
                                      DumpState());
            dst->EndCapture();
            retireInputs();
            return &src;
        }

        effects_[i]->ApplyCommonParams(params);
        if (!effects_[i]->BindAndDraw(quad)) {
            DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Effect BindAndDraw failed.\n") + DumpState());
            dst->EndCapture();
            retireInputs();
            return &src;
        }

        dst->EndCapture();
        retireInputs();

        cur = dst;
    }

    return cur;
}

} // namespace POST
} // namespace HIKARI
