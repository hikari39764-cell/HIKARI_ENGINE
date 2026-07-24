#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        using Microsoft::WRL::ComPtr;

        struct TemporalMaskPassState {
            ComPtr<ID3D12RootSignature> rootSignature{};
            ComPtr<ID3D12PipelineState> pipelineState{};
            bool ready = false;
        };

        TemporalMaskPassState& State() {
            static TemporalMaskPassState state{};
            return state;
        }

        bool EnsurePipeline(ID3D12Device* device) {
            TemporalMaskPassState& state = State();
            if (state.ready && state.rootSignature != nullptr && state.pipelineState != nullptr) {
                return true;
            }
            if (device == nullptr) {
                return false;
            }

            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(ranges)); ++index) {
                ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                ranges[index].NumDescriptors = 1;
                ranges[index].BaseShaderRegister = index;
                ranges[index].OffsetInDescriptorsFromTableStart =
                    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            }

            D3D12_ROOT_PARAMETER params[2]{};
            for (uint32_t index = 0; index < static_cast<uint32_t>(std::size(params)); ++index) {
                params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                params[index].DescriptorTable.NumDescriptorRanges = 1;
                params[index].DescriptorTable.pDescriptorRanges = &ranges[index];
            }

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = static_cast<UINT>(std::size(params));
            rootDesc.pParameters = params;
            rootDesc.NumStaticSamplers = 1;
            rootDesc.pStaticSamplers = &sampler;

            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> errors;
            HRESULT hr = D3D12SerializeRootSignature(
                &rootDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                signature.GetAddressOf(),
                errors.GetAddressOf());
            if (FAILED(hr)) {
                if (errors != nullptr) {
                    DEBUGLOG::PushRenderError(
                        static_cast<const char*>(errors->GetBufferPointer()));
                }
                return false;
            }
            hr = device->CreateRootSignature(
                0,
                signature->GetBufferPointer(),
                signature->GetBufferSize(),
                IID_PPV_ARGS(state.rootSignature.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "TemporalMaskPass::CreateRootSignature")) {
                return false;
            }
            state.rootSignature->SetName(L"HIKARI.Temporal.MaskRootSignature");

            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_Masks.hlsl",
                    "VSMain",
                    GFX::ShaderStage::Vertex,
                    vs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_Masks.hlsl",
                    "PSMain",
                    GFX::ShaderStage::Pixel,
                    ps.GetAddressOf())) {
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = state.rootSignature.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 3;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
            psoDesc.RTVFormats[1] = DXGI_FORMAT_R8_UNORM;
            psoDesc.RTVFormats[2] = DXGI_FORMAT_R8_UNORM;
            psoDesc.SampleDesc.Count = 1;

            hr = device->CreateGraphicsPipelineState(
                &psoDesc,
                IID_PPV_ARGS(state.pipelineState.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "TemporalMaskPass::CreatePSO")) {
                return false;
            }
            state.pipelineState->SetName(L"HIKARI.Temporal.MaskPSO");
            state.ready = true;
            return true;
        }
    }

    bool ExecuteTemporalMaskPass(const TemporalInputs& inputs) {
        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        ID3D12Device* device = SERVICES::gCtx.device;
        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        RenderTarget2D* reactive = GetReactiveMaskRenderTarget();
        RenderTarget2D* transparency = GetTransparencyMaskRenderTarget();
        RenderTarget2D* invalidDepthMotion =
            GetInvalidDepthMotionMaskRenderTarget();
        if (cmd == nullptr ||
            device == nullptr ||
            reactive == nullptr ||
            transparency == nullptr ||
            invalidDepthMotion == nullptr) {
            MarkTemporalMasksWritten(false, false);
            return false;
        }

        reactive->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        transparency->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        invalidDepthMotion->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] = {
            reactive->GetRtvHandle(),
            transparency->GetRtvHandle(),
            invalidDepthMotion->GetRtvHandle()
        };
        cmd->OMSetRenderTargets(3, rtvs, FALSE, nullptr);
        const float clear[4] = {};
        cmd->ClearRenderTargetView(rtvs[0], clear, 0, nullptr);
        cmd->ClearRenderTargetView(rtvs[1], clear, 0, nullptr);
        cmd->ClearRenderTargetView(rtvs[2], clear, 0, nullptr);

        const D3D12_GPU_DESCRIPTOR_HANDLE compositionBase =
            GetTemporalCompositionBase();
        const bool canGenerate =
            srvHeap != nullptr &&
            inputs.sceneColor.valid &&
            compositionBase.ptr != 0 &&
            EnsurePipeline(device);
        if (canGenerate) {
            GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorPost, "Temporal.Masks");
            const D3D12_VIEWPORT viewport{
                0.0f,
                0.0f,
                static_cast<float>((std::max)(1, reactive->GetWidth())),
                static_cast<float>((std::max)(1, reactive->GetHeight())),
                0.0f,
                1.0f
            };
            const D3D12_RECT scissor{
                0,
                0,
                (std::max)(1, reactive->GetWidth()),
                (std::max)(1, reactive->GetHeight())
            };
            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
            cmd->SetDescriptorHeaps(1, &srvHeap);
            cmd->SetGraphicsRootSignature(State().rootSignature.Get());
            cmd->SetPipelineState(State().pipelineState.Get());
            cmd->SetGraphicsRootDescriptorTable(0, compositionBase);
            cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneColor.srv);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->DrawInstanced(3, 1, 0, 0);
        }

        reactive->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transparency->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        invalidDepthMotion->TransitionColor(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // Cleared zero masks are a valid opaque-only hint set. When a
        // post-opaque composition base exists, the draw refines them from the
        // actual color difference instead.
        MarkTemporalMasksWritten(true, canGenerate);
        return true;
    }

    void ShutdownTemporalMaskPass() {
        TemporalMaskPassState& state = State();
        state.pipelineState.Reset();
        state.rootSignature.Reset();
        state.ready = false;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
