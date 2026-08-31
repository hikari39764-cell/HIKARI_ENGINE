#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_PostEffectShaderContract.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include <Windows.h>
#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <d3dcommon.h>
#include <cstring>
#include <d3dx12.h>
#include <limits>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace POST {

        GFX::Context PostEffect::context_{};

        PostEffect::PostEffect()
        {
            ZeroMemory(&params_, sizeof(params_));
            constantFrameSerials_.fill(
                (std::numeric_limits<uint64_t>::max)());
            constantDrawCursors_.fill(0u);
        }

        void PostEffect::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        PostEffect::~PostEffect()
        {
            ReleaseConstantBuffer();
        }

        void PostEffect::ReleaseConstantBuffer() noexcept
        {
            if (constantBuffer_ && mappedPtr_) {
                constantBuffer_->Unmap(0, nullptr);
            }
            mappedPtr_ = nullptr;
            GFX::RetireD3D12ObjectForFrame(
                constantBuffer_,
                constantBufferContext_,
                "PostEffect.ConstantBuffer");
            constantBufferContext_ = {};
            constantStride_ = 0u;
            constantFrameSerials_.fill(
                (std::numeric_limits<uint64_t>::max)());
            constantDrawCursors_.fill(0u);
        }

        bool PostEffect::CreateConstantBuffer()
        {
            const bool sameDevice = constantBuffer_ && mappedPtr_ &&
                constantBufferContext_.device == context_.device &&
                constantBufferContext_.deviceGeneration != 0u &&
                constantBufferContext_.deviceGeneration ==
                    context_.deviceGeneration;
            if (sameDevice) {
                return true;
            }
            if (constantBuffer_ || mappedPtr_ != nullptr) {
                ReleaseConstantBuffer();
            }

            auto* device = context_.device;
            if (!device) {
                DEBUGLOG::PushRenderError("[PostEffect][ERROR] context_.device is null; postpone constant buffer creation.");
                return false;
            }

            constantStride_ = Align256(sizeof(CommonParams));
            const UINT64 size =
                static_cast<UINT64>(constantStride_) *
                kConstantDrawCapacityPerFrame *
                GFX::kFrameResourceCount;

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

            void* mapped = nullptr;
            hr = constantBuffer_->Map(0, nullptr, &mapped);
            if (!HIKARI_DX_CHECK(hr, "PostEffect::Map constant buffer")) {
                constantBuffer_.Reset();
                mappedPtr_ = nullptr;
                constantStride_ = 0u;
                return false;
            }
            mappedPtr_ = static_cast<uint8_t*>(mapped);
            constantBufferContext_ = context_;
            constantFrameSerials_.fill(
                (std::numeric_limits<uint64_t>::max)());
            constantDrawCursors_.fill(0u);
            return true;
        }
        bool PostEffect::LoadPixelShader(const wchar_t* path)
        {
            ComPtr<ID3DBlob> blob{};
            if (!GFX::CompileShaderFileSm6(
                    path,
                    "main",
                    GFX::ShaderStage::Pixel,
                    blob.GetAddressOf())) {
                DEBUGLOG::PushRenderError("[PostEffect][ERROR] SM6 pixel shader compile failed.");
                return false;
            }
            psBlob_ = std::move(blob);
            psLease_ = {};
            return true;
        }

        bool PostEffect::LoadBuiltinPixelShader(
            std::string_view programId,
            std::string_view entryId)
        {
            if (context_.device == nullptr || programId.empty() ||
                entryId.empty()) {
                DEBUGLOG::PushRenderError(
                    "[PostEffect][ERROR] Built-in Shader request is invalid.");
                return false;
            }
            const GFX::ShaderDeviceCapabilities capabilities =
                GFX::QueryShaderDeviceCapabilities(context_.device);
            const ASSETS::SHADER::ShaderAssetContractExpectation contract =
                ASSETS::SHADER::MakePostEffectShaderContract({
                    capabilities.shaderModel,
                    capabilities.rootSignatureVersion,
                    capabilities.featureMask
                });
            ASSETS::SHADER::ShaderAssetResolveResult resolved =
                ASSETS::SHADER::ResolveBuiltinShaderEntry(
                    programId,
                    { std::string(entryId),
                      ASSETS::SHADER::ShaderStage::Pixel,
                      "ps_6_0" },
                    contract);
            std::string diagnostic{};
            const ASSETS::SHADER::ShaderCookedArtifact* artifact =
                resolved ? resolved.lease.GetArtifact() : nullptr;
            if (!resolved || artifact == nullptr ||
                !ASSETS::SHADER::ValidatePostEffectShaderArtifact(
                    *artifact,
                    diagnostic)) {
                DEBUGLOG::PushRenderError(
                    "[PostEffect][ERROR] Built-in HSHADERC resolve failed: " +
                    (diagnostic.empty() ? resolved.diagnostic : diagnostic));
                return false;
            }
            psBlob_.Reset();
            psLease_ = std::move(resolved.lease);
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
            const std::span<const uint8_t> cooked = psLease_.GetDxil();
            if (!psBlob_ && cooked.empty()) { return false; }
            if (!CreateConstantBuffer()) {
                return false;
            }
            if (!constantBuffer_) {
                return false;
            }
            constantBufferContext_ = context_;

            const uint32_t frameSlot =
                context_.frameIndex % GFX::kFrameResourceCount;
            const uint64_t frameSerial =
                TIME::GetFrameContext().frameIndex;
            if (constantFrameSerials_[frameSlot] != frameSerial) {
                constantFrameSerials_[frameSlot] = frameSerial;
                constantDrawCursors_[frameSlot] = 0u;
            }

            const uint32_t drawSlot = constantDrawCursors_[frameSlot];
            if (drawSlot >= kConstantDrawCapacityPerFrame) {
                DEBUGLOG::PushRenderError(
                    "[PostEffect][ERROR] Per-frame constant draw capacity exhausted; "
                    "the draw was skipped to preserve GPU resource lifetime.");
                return false;
            }
            ++constantDrawCursors_[frameSlot];

            const UINT64 allocationIndex =
                static_cast<UINT64>(frameSlot) *
                    kConstantDrawCapacityPerFrame +
                drawSlot;
            const UINT64 byteOffset =
                allocationIndex * constantStride_;
            memcpy(mappedPtr_ + byteOffset, &params_, sizeof(params_));

            const bool shaderReady = !cooked.empty()
                ? drawer.SetPixelShader(cooked)
                : drawer.SetPixelShader(psBlob_.Get());
            if (!shaderReady) {
                return false;
            }
            drawer.SetConstantBuffer(
                constantBuffer_->GetGPUVirtualAddress() + byteOffset);
            drawer.DrawFullscreen();
            return true;
        }

    } // POST
} // HIKARI
