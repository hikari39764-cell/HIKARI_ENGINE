#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include <sstream>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"

namespace HIKARI {
    namespace POST {

        void PostChain::Clear()
        {
            effects_.clear();
        }

        void PostChain::Add(PostEffect* effect)
        {
            if (!effect) {
                return;
            }
            effects_.push_back(effect);
        }

        void PostChain::UpdateContext(const GFX::Context& ctx)
        {
            context_ = ctx;
            ping_.UpdateContext(ctx);
            pong_.UpdateContext(ctx);
        }

        void PostChain::SetDebugName(std::string name)
        {
            debugName_ = std::move(name);
            ping_.SetDebugName(debugName_ + ".Ping");
            pong_.SetDebugName(debugName_ + ".Pong");
        }

        std::string PostChain::DumpState() const
        {
            std::ostringstream oss;
            oss << "[PostChain] name=" << debugName_
                << " effectCount=" << effects_.size()
                << " tempsInitialized=" << tempsInitialized_
                << "\n  " << ping_.DumpState()
                << "\n  " << pong_.DumpState();
            return oss.str();
        }

        void PostChain::Finalize()
        {
            ping_.Finalize();
            pong_.Finalize();
            tempsInitialized_ = false;
            effects_.clear();
        }

        void PostChain::EnsureTempSize(int w, int h)
        {
            if (w <= 0 || h <= 0) {
                return;
            }

            constexpr DXGI_FORMAT kPostChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!tempsInitialized_ ||
                ping_.GetResource() == nullptr ||
                pong_.GetResource() == nullptr ||
                ping_.GetFormat() != kPostChainFormat ||
                pong_.GetFormat() != kPostChainFormat) {
                ping_.Finalize();
                pong_.Finalize();
                ping_.UpdateContext(context_);
                pong_.UpdateContext(context_);
                const bool okPing = ping_.Init(
                    w, h,
                    kPostChainFormat,
                    false,
                    { 0.0f, 0.0f, 0.0f, 0.0f }
                );

                const bool okPong = pong_.Init(
                    w, h,
                    kPostChainFormat,
                    false,
                    { 0.0f, 0.0f, 0.0f, 0.0f }
                );
                tempsInitialized_ = okPing && okPong;
                if (!tempsInitialized_) {
                    DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] EnsureTempSize failed.\n") + DumpState());
                }
                return;
            }



            if (ping_.GetWidth() != w || ping_.GetHeight() != h) {
                ping_.Finalize();
                ping_.UpdateContext(context_);
                ping_.Init(
                    w, h,
                    kPostChainFormat,
                    false,
                    { 0.0f, 0.0f, 0.0f, 0.0f }
                );
            }
            if (pong_.GetWidth() != w || pong_.GetHeight() != h) {
                pong_.Finalize();
                pong_.UpdateContext(context_);
                pong_.Init(
                    w, h,
                    kPostChainFormat,
                    false,
                    { 0.0f, 0.0f, 0.0f, 0.0f }
                );
            }
        }
        void PostChain::PrepareBuffers(int w, int h)
        {
            EnsureTempSize(w, h);
        }

        RenderTarget2D* PostChain::Execute(
            RenderTarget2D& src,
            QuadDrawer& quad,
            const CommonParams& params,
            int tempWidth,
            int tempHeight)
        {
            if (effects_.empty()) {
                return &src;
            }


            int w = tempWidth > 0 ? tempWidth : src.GetWidth();
            int h = tempHeight > 0 ? tempHeight : src.GetHeight();
            EnsureTempSize(w, h);
            if (ping_.GetResource() == nullptr || pong_.GetResource() == nullptr) {
                DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute skipped: temp buffers invalid.\n") + DumpState());
                return &src;
            }

            RenderTarget2D* cur = &src;

            for (size_t i = 0; i < effects_.size(); i++) {
                if (effects_[i] == nullptr) {
                    DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute skipped null effect.\n") + DumpState());
                    continue;
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

 
                quad.SetInputTexture(cur->GetSrvHeap(), cur->GetSrvGpu());
                if (!quad.SetOutputFormat(dst->GetFormat())) {
                    DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Execute SetOutputFormat failed.\n") + DumpState());
                    dst->EndCapture();
                    return cur;
                }


                effects_[i]->ApplyCommonParams(params);
                if (!effects_[i]->BindAndDraw(quad)) {
                    DEBUGLOG::PushRenderError(std::string("[PostChain][ERROR] Effect BindAndDraw failed.\n") + DumpState());
                }

                dst->EndCapture();

                cur = dst;
            }

            return cur;
        }

    } // POST
} // HIKARI
