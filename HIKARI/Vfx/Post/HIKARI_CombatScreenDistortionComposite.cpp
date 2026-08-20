#include "Vfx/Post/HIKARI_CombatScreenDistortionComposite.h"

#include <cstring>
#include <string>

#include <d3dcommon.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::POST {

    namespace {

        constexpr const char* kCombatDistortionFullscreenVs = R"(
struct VS_OUT {
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
};
VS_OUT main(uint vid : SV_VertexID)
{
  VS_OUT o;
  float2 pos;
  if (vid == 0) pos = float2(-1.0, -1.0);
  else if (vid == 1) pos = float2(-1.0, 3.0);
  else pos = float2(3.0, -1.0);
  o.pos = float4(pos, 0.0, 1.0);
  o.uv = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);
  return o;
}
)";

        static void LogCompositeError(const char* message) {
            DEBUGLOG::PushRenderError(
                std::string("[CombatScreenDistortionComposite][ERROR] ") +
                (message ? message : ""));
        }

    } // namespace

    // Private two-SRV post pass.  PostQuadDrawer intentionally has a compact
    // one-SRV root signature for the general post chain, so the combat path
    // owns a separate descriptor heap/root signature and copies the current
    // scene/mask CPU descriptors into the frame slot before drawing.
    struct CombatScreenDistortionComposite::Impl final {
    public:
        Impl() = default;
        ~Impl() { Shutdown(); }

        void UpdateContext(const GFX::Context& context) {
            if (context_.device != nullptr &&
                context_.device != context.device) {
                Shutdown();
            }
            context_ = context;
            for (auto& output : outputs_) {
                output.UpdateContext(context_);
            }
        }

        void Shutdown() {
            for (auto& output : outputs_) {
                output.Finalize();
            }
            for (auto& frame : frameResources_) {
                GFX::RetireD3D12ObjectForFrame(
                    frame.srvHeap,
                    context_,
                    "CombatScreenDistortionComposite.SRVHeap");
            }
            if (constantBuffer_ && mappedPtr_ != nullptr) {
                constantBuffer_->Unmap(0, nullptr);
                mappedPtr_ = nullptr;
            }
            GFX::RetireD3D12ObjectForFrame(
                constantBuffer_,
                context_,
                "CombatScreenDistortionComposite.ConstantBuffer");
            GFX::RetireD3D12ObjectForFrame(
                pipelineState_,
                context_,
                "CombatScreenDistortionComposite.PSO");
            GFX::RetireD3D12ObjectForFrame(
                rootSignature_,
                context_,
                "CombatScreenDistortionComposite.RootSignature");
            vsBlob_.Reset();
            psBlob_.Reset();
            constantStride_ = 0u;
            pipelineFormat_ = DXGI_FORMAT_UNKNOWN;
        }

        bool Execute(
            RenderTarget2D& source,
            const ActionScreenImpulseProvider::Mask* mask,
            const CommonParams& params,
            uint64_t frameIndex,
            RenderTarget2D*& output,
            bool& maskApplied) {
            output = nullptr;
            maskApplied = false;
            if (context_.device == nullptr || context_.cmdList == nullptr ||
                source.GetResource() == nullptr || source.GetSrvHeap() == nullptr ||
                source.GetSrvGpu().ptr == 0u) {
                return false;
            }

            const uint32_t slot = context_.frameIndex % GFX::kFrameResourceCount;
            if (!EnsureResources() || !EnsureOutput(slot, source)) {
                return false;
            }

            const ActionScreenImpulseProvider::Mask* validMask =
                mask != nullptr && mask->IsValid(frameIndex)
                ? mask
                : nullptr;
            maskApplied = validMask != nullptr;
            source.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            FrameResources& frame = frameResources_[slot];
            if (!CopySrv(source, frame, 0u)) {
                return false;
            }
            // Binding scene again for t1 makes the no-mask path safe: the
            // shader's mask branch is disabled in the constants, so it never
            // samples a missing descriptor.
            if (validMask != nullptr) {
                if (!CopySrv(*validMask, frame, 1u)) {
                    return false;
                }
            } else if (!CopySrv(source, frame, 1u)) {
                return false;
            }

            CommonParams localParams = params;
            if (!maskApplied) {
                localParams.user[15].y = 0.0f;
                localParams.user[15].z = 0.0f;
                localParams.user[15].w = 0.0f;
            }
            const UINT64 byteOffset =
                static_cast<UINT64>(slot) * constantStride_;
            std::memcpy(mappedPtr_ + byteOffset,
                &localParams,
                sizeof(localParams));

            RenderTarget2D& target = outputs_[slot];
            target.BeginCapture();
            ID3D12DescriptorHeap* heaps[] = { frame.srvHeap.Get() };
            auto* cmd = context_.cmdList;
            cmd->SetDescriptorHeaps(1, heaps);
            cmd->SetGraphicsRootSignature(rootSignature_.Get());
            cmd->SetPipelineState(pipelineState_.Get());
            cmd->SetGraphicsRootDescriptorTable(
                0,
                frame.srvHeap->GetGPUDescriptorHandleForHeapStart());
            cmd->SetGraphicsRootConstantBufferView(
                1,
                constantBuffer_->GetGPUVirtualAddress() + byteOffset);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3, 1, 0, 0);
            target.EndCapture();
            output = &target;
            (void)frameIndex;
            return true;
        }

    private:
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap{};
        };

        bool EnsureResources() {
            if (context_.device == nullptr) {
                return false;
            }
            if (!vsBlob_) {
                if (!GFX::CompileShaderSourceSm6(
                        kCombatDistortionFullscreenVs,
                        std::strlen(kCombatDistortionFullscreenVs),
                        L"CombatScreenDistortionComposite.FullscreenVS",
                        "main",
                        GFX::ShaderStage::Vertex,
                        vsBlob_.GetAddressOf())) {
                    LogCompositeError("fullscreen vertex shader compile failed");
                    return false;
                }
            }
            if (!psBlob_) {
                if (!GFX::CompileShaderFileSm6(
                        L"HIKARI/Shaders/Post_CombatScreenDistortionComposite.hlsl",
                        "main",
                        GFX::ShaderStage::Pixel,
                        psBlob_.GetAddressOf())) {
                    LogCompositeError("combat distortion pixel shader compile failed");
                    return false;
                }
            }
            if (!rootSignature_ && !CreateRootSignature()) {
                return false;
            }
            if (!constantBuffer_ && !CreateConstantBuffer()) {
                return false;
            }
            for (auto& frame : frameResources_) {
                if (!frame.srvHeap && !CreateFrameDescriptorHeap(frame)) {
                    return false;
                }
            }
            return true;
        }

        bool EnsureOutput(uint32_t slot, const RenderTarget2D& source) {
            RenderTarget2D& output = outputs_[slot];
            if (output.IsInitialized() &&
                output.GetWidth() == source.GetWidth() &&
                output.GetHeight() == source.GetHeight() &&
                output.GetFormat() == source.GetFormat()) {
                return EnsurePipeline(source.GetFormat());
            }
            output.UpdateContext(context_);
            output.Finalize();
            if (!output.Init(
                    source.GetWidth(),
                    source.GetHeight(),
                    source.GetFormat(),
                    false)) {
                LogCompositeError("output target creation failed");
                return false;
            }
            output.SetDebugName("PostProcessing.CombatScreenDistortion");
            return EnsurePipeline(source.GetFormat());
        }

        bool CreateRootSignature() {
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 2u;
            range.BaseShaderRegister = 0u;
            range.RegisterSpace = 0u;
            range.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER parameters[2]{};
            parameters[0].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
            parameters[0].DescriptorTable.pDescriptorRanges = &range;
            parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[1].Descriptor.ShaderRegister = 0u;
            parameters[1].Descriptor.RegisterSpace = 0u;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.MinLOD = 0.0f;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = 0u;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC description{};
            description.NumParameters = 2u;
            description.pParameters = parameters;
            description.NumStaticSamplers = 1u;
            description.pStaticSamplers = &sampler;
            description.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            Microsoft::WRL::ComPtr<ID3DBlob> serialized;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            HRESULT hr = D3D12SerializeRootSignature(
                &description,
                D3D_ROOT_SIGNATURE_VERSION_1,
                serialized.GetAddressOf(),
                errors.GetAddressOf());
            if (FAILED(hr) || !serialized) {
                LogCompositeError("root signature serialization failed");
                return false;
            }
            hr = context_.device->CreateRootSignature(
                0u,
                serialized->GetBufferPointer(),
                serialized->GetBufferSize(),
                IID_PPV_ARGS(rootSignature_.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr,
                    "CombatScreenDistortionComposite root signature")) {
                return false;
            }
            GFX::SetD3D12Name(
                rootSignature_.Get(),
                L"CombatScreenDistortionComposite.RootSignature");
            return true;
        }

        bool CreateConstantBuffer() {
            constantStride_ = Align256(sizeof(CommonParams));
            const UINT64 size = static_cast<UINT64>(constantStride_) *
                GFX::kFrameResourceCount;
            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC resourceDesc =
                CD3DX12_RESOURCE_DESC::Buffer(size);
            HRESULT hr = context_.device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(constantBuffer_.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr,
                    "CombatScreenDistortionComposite constant buffer")) {
                constantStride_ = 0u;
                return false;
            }
            void* mapped = nullptr;
            hr = constantBuffer_->Map(0u, nullptr, &mapped);
            if (!HIKARI_DX_CHECK(hr,
                    "CombatScreenDistortionComposite map constant buffer")) {
                constantBuffer_.Reset();
                constantStride_ = 0u;
                return false;
            }
            mappedPtr_ = static_cast<uint8_t*>(mapped);
            return true;
        }

        bool CreateFrameDescriptorHeap(FrameResources& frame) {
            D3D12_DESCRIPTOR_HEAP_DESC description{};
            description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            description.NumDescriptors = 2u;
            description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            const HRESULT hr = context_.device->CreateDescriptorHeap(
                &description,
                IID_PPV_ARGS(frame.srvHeap.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr,
                    "CombatScreenDistortionComposite SRV heap")) {
                return false;
            }
            GFX::SetD3D12Name(
                frame.srvHeap.Get(),
                L"CombatScreenDistortionComposite.SRVHeap");
            return true;
        }

        bool EnsurePipeline(DXGI_FORMAT format) {
            if (pipelineState_ && pipelineFormat_ == format) {
                return true;
            }
            GFX::RetireD3D12ObjectForFrame(
                pipelineState_,
                context_,
                "CombatScreenDistortionComposite.PSO");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
            description.pRootSignature = rootSignature_.Get();
            description.VS = {
                vsBlob_->GetBufferPointer(),
                vsBlob_->GetBufferSize() };
            description.PS = {
                psBlob_->GetBufferPointer(),
                psBlob_->GetBufferSize() };
            description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            description.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            description.DepthStencilState =
                CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            description.DepthStencilState.DepthEnable = FALSE;
            description.DepthStencilState.StencilEnable = FALSE;
            description.SampleMask = UINT_MAX;
            description.PrimitiveTopologyType =
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            description.NumRenderTargets = 1u;
            description.RTVFormats[0] = format;
            description.DSVFormat = DXGI_FORMAT_UNKNOWN;
            description.SampleDesc.Count = 1u;
            const HRESULT hr = context_.device->CreateGraphicsPipelineState(
                &description,
                IID_PPV_ARGS(pipelineState_.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr,
                    "CombatScreenDistortionComposite PSO")) {
                pipelineFormat_ = DXGI_FORMAT_UNKNOWN;
                return false;
            }
            pipelineFormat_ = format;
            GFX::SetD3D12Name(
                pipelineState_.Get(),
                L"CombatScreenDistortionComposite.PSO");
            return true;
        }

        bool CopySrv(
            const RenderTarget2D& source,
            FrameResources& frame,
            uint32_t index) {
            if (index >= 2u || source.GetSrvHeap() == nullptr ||
                source.GetSrvGpu().ptr == 0u) {
                return false;
            }
            const UINT increment = context_.device->
                GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE destination =
                frame.srvHeap->GetCPUDescriptorHandleForHeapStart();
            destination.ptr += static_cast<SIZE_T>(increment) * index;
            context_.device->CopyDescriptorsSimple(
                1u,
                destination,
                source.GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return true;
        }

        bool CopySrv(
            const ActionScreenImpulseProvider::Mask& source,
            FrameResources& frame,
            uint32_t index) {
            if (index >= 2u || source.texture.resource == nullptr ||
                !source.texture.IsSampleable() || source.srvCpu.ptr == 0u) {
                return false;
            }
            const UINT increment = context_.device->
                GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE destination =
                frame.srvHeap->GetCPUDescriptorHandleForHeapStart();
            destination.ptr += static_cast<SIZE_T>(increment) * index;
            context_.device->CopyDescriptorsSimple(
                1u,
                destination,
                source.srvCpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return true;
        }

        GFX::Context context_{};
        std::array<RenderTarget2D, GFX::kFrameResourceCount> outputs_{};
        std::array<FrameResources, GFX::kFrameResourceCount> frameResources_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob_{};
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_{};
        uint8_t* mappedPtr_ = nullptr;
        uint32_t constantStride_ = 0u;
        DXGI_FORMAT pipelineFormat_ = DXGI_FORMAT_UNKNOWN;
    };

    CombatScreenDistortionComposite::CombatScreenDistortionComposite() =
        default;

    CombatScreenDistortionComposite::~CombatScreenDistortionComposite() =
        default;

    void CombatScreenDistortionComposite::UpdateContext(
        const GFX::Context& context) {
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
        }
        impl_->UpdateContext(context);
    }

    void CombatScreenDistortionComposite::Shutdown() {
        if (impl_) {
            impl_->Shutdown();
        }
    }

    bool CombatScreenDistortionComposite::Execute(
        RenderTarget2D& source,
        const ActionScreenImpulseProvider::Mask* mask,
        const CommonParams& params,
        uint64_t frameIndex,
        RenderTarget2D*& output,
        bool& maskApplied) {
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
        }
        return impl_->Execute(
            source,
            mask,
            params,
            frameIndex,
            output,
            maskApplied);
    }

} // namespace HIKARI::POST
