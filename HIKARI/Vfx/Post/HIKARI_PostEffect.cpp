#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include <Windows.h>
#include <d3dcommon.h>
#include <cstring>
#include <d3dx12.h>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace POST {

        GFX::Context PostEffect::context_{};

        PostEffect::PostEffect()
        {
            ZeroMemory(&params_, sizeof(params_));
        }

        void PostEffect::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        PostEffect::~PostEffect()
        {
            if (constantBuffer_ && mappedPtr_) {
                constantBuffer_->Unmap(0, nullptr);
                mappedPtr_ = nullptr;
            }
            GFX::RetireD3D12ObjectForFrame(constantBuffer_, context_, "PostEffect.ConstantBuffer");
        }

        bool PostEffect::CreateConstantBuffer()
        {
            if (constantBuffer_ && mappedPtr_) {
                return true;
            }

            auto* device = context_.device;
            if (!device) {
                DEBUGLOG::PushRenderError("[PostEffect][ERROR] context_.device is null; postpone constant buffer creation.");
                return false;
            }

            UINT size = Align256(sizeof(CommonParams));

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

            HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(constantBuffer_.GetAddressOf())
            );
            if (!HIKARI_DX_CHECK(hr, "PostEffect::CreateConstantBuffer")) {
                return false;
            }

            hr = constantBuffer_->Map(0, nullptr, &mappedPtr_);
            if (!HIKARI_DX_CHECK(hr, "PostEffect::Map constant buffer")) {
                constantBuffer_.Reset();
                mappedPtr_ = nullptr;
                return false;
            }
            return true;
        }
        bool PostEffect::LoadPixelShader(const wchar_t* path)
        {
            if (!GFX::CompileShaderFileSm6(path, "main", GFX::ShaderStage::Pixel, psBlob_.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[PostEffect][ERROR] SM6 pixel shader compile failed.");
                return false;
            }

            return true;
        }

        void PostEffect::ApplyCommonParams(const CommonParams& p)
        {
            params_ = p;
        }

        void PostEffect::SetTime(float t)
        {
            params_.time = t;
        }


        void PostEffect::SetUser(int index, const DirectX::XMFLOAT4& v)
        {
            if (index < 0 || index >= 16) { return; }
            params_.user[index] = v;
        }

        bool PostEffect::BindAndDraw(QuadDrawer& drawer)
        {
            if (!psBlob_) { return false; }
            if (!mappedPtr_ && !CreateConstantBuffer()) {
                return false;
            }
            if (!constantBuffer_) {
                return false;
            }

            memcpy(mappedPtr_, &params_, sizeof(params_));

            if (!drawer.SetPixelShader(psBlob_.Get())) {
                return false;
            }
            drawer.SetConstantBuffer(constantBuffer_->GetGPUVirtualAddress());
            drawer.DrawFullscreen();
            return true;
        }

    } // POST
} // HIKARI
