#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalConstantBufferSlots.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        using Microsoft::WRL::ComPtr;
        constexpr uint32_t kFrameSlotCount = 3u;

        struct MotionVectorConstants {
            MATH::Mat4 invViewProj{};
            MATH::Mat4 viewProj{};
            MATH::Mat4 prevViewProj{};
            MATH::Vec4 screenParams{};
            MATH::Vec4 historyParams{};
            MATH::Vec4 cameraPos{};
            MATH::Vec4 prevCameraPos{};
        };

        struct ConstantSlot {
            ComPtr<ID3D12Resource> buffer{};
            MotionVectorConstants* mapped = nullptr;
        };

        struct MotionVectorPassState {
            ComPtr<ID3D12RootSignature> rootSignature{};
            ComPtr<ID3D12PipelineState> pipelineState{};
            std::array<ConstantSlot, kFrameSlotCount> constants{};
            bool ready = false;
        };

        MotionVectorPassState& State() {
            static MotionVectorPassState state{};
            return state;
        }

        bool EnsurePipeline(ID3D12Device* device) {
            MotionVectorPassState& state = State();
            if (state.ready &&
                state.rootSignature != nullptr &&
                state.pipelineState != nullptr) {
                return true;
            }
            if (device == nullptr) {
                return false;
            }

            D3D12_DESCRIPTOR_RANGE depthRange{};
            depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            depthRange.NumDescriptors = 1;
            depthRange.BaseShaderRegister = 0;
            depthRange.RegisterSpace = 0;
            depthRange.OffsetInDescriptorsFromTableStart =
                D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Descriptor.ShaderRegister = 0;
            params[0].Descriptor.RegisterSpace = 0;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &depthRange;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = static_cast<UINT>(std::size(params));
            rsDesc.pParameters = params;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            HRESULT hr = D3D12SerializeRootSignature(
                &rsDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                sigBlob.GetAddressOf(),
                errBlob.GetAddressOf());
            if (FAILED(hr)) {
                if (errBlob != nullptr) {
                    DEBUGLOG::PushRenderError(
                        static_cast<const char*>(errBlob->GetBufferPointer()));
                }
                return false;
            }
            hr = device->CreateRootSignature(
                0,
                sigBlob->GetBufferPointer(),
                sigBlob->GetBufferSize(),
                IID_PPV_ARGS(state.rootSignature.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "TemporalMotionVectorPass::CreateRootSignature")) {
                return false;
            }
            state.rootSignature->SetName(L"HIKARI.Temporal.MotionVectorRootSignature");

            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_MotionVector.hlsl",
                    "VSMain",
                    GFX::ShaderStage::Vertex,
                    vs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_MotionVector.hlsl",
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
            psoDesc.InputLayout = {};
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 2;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
            psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            hr = device->CreateGraphicsPipelineState(
                &psoDesc,
                IID_PPV_ARGS(state.pipelineState.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "TemporalMotionVectorPass::CreatePSO")) {
                return false;
            }
            state.pipelineState->SetName(L"HIKARI.Temporal.MotionVectorPSO");

            state.ready =
                INTERNAL::EnsureMappedConstantBufferSlots<
                    MotionVectorConstants>(
                    device,
                    state.constants,
                    L"HIKARI.Temporal.MotionVectorCB");
            return state.ready;
        }
    }

    bool ExecuteMotionVectorPass(const TemporalInputs& inputs) {
        MotionVectorPassState& state = State();
        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        ID3D12Device* device = SERVICES::gCtx.device;
        ID3D12DescriptorHeap* srvHeap = SERVICES::gCtx.srvHeap;
        RenderTarget2D* motionTarget = GetMotionVectorRenderTarget();
        RenderTarget2D* metadataTarget = GetMotionMetadataRenderTarget();
        if (cmd == nullptr ||
            device == nullptr ||
            srvHeap == nullptr ||
            motionTarget == nullptr ||
            metadataTarget == nullptr ||
            !inputs.motionVectors.valid ||
            !inputs.motionMetadata.valid) {
            MarkMotionVectorsWritten(false);
            return false;
        }
        if (!EnsurePipeline(device)) {
            MarkMotionVectorsWritten(false);
            return false;
        }

        ConstantSlot& constantSlot =
            state.constants[inputs.frame.frameIndex % kFrameSlotCount];
        if (constantSlot.mapped == nullptr || constantSlot.buffer == nullptr) {
            MarkMotionVectorsWritten(false);
            return false;
        }

        MotionVectorConstants constants{};
        constants.invViewProj = inputs.frame.camera.invViewProj;
        constants.viewProj = inputs.frame.camera.unjitteredViewProj;
        constants.prevViewProj = inputs.frame.camera.prevUnjitteredViewProj;
        constants.screenParams = inputs.frame.camera.screenParams;
        constants.historyParams = {
            inputs.frame.historyValid ? 1.0f : 0.0f,
            inputs.hasSceneDepth ? 1.0f : 0.0f,
            (std::max)(inputs.frame.camera.farZ, 1.0f),
            0.0f
        };
        constants.cameraPos = inputs.frame.camera.cameraPos;
        constants.prevCameraPos = inputs.frame.camera.prevCameraPos;
        *constantSlot.mapped = constants;

        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorRender,
            "Temporal.MotionVectors");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuTimer(
            cmd,
            GFX::GPU_PROFILE::Pass::TemporalMotionVectors);

        if (!inputs.hasSceneDepth || inputs.sceneDepthSrv.ptr == 0) {
            MarkMotionVectorsWritten(false);
            return false;
        }

        motionTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {
            motionTarget->GetRtvHandle(),
            metadataTarget->GetRtvHandle()
        };
        cmd->OMSetRenderTargets(2, rtvs, FALSE, nullptr);
        const float motionClear[4] = {};
        const float metadataClear[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        cmd->ClearRenderTargetView(rtvs[0], motionClear, 0, nullptr);
        cmd->ClearRenderTargetView(rtvs[1], metadataClear, 0, nullptr);
        const D3D12_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>((std::max)(1, motionTarget->GetWidth())),
            static_cast<float>((std::max)(1, motionTarget->GetHeight())),
            0.0f,
            1.0f
        };
        const D3D12_RECT scissor{
            0,
            0,
            (std::max)(1, motionTarget->GetWidth()),
            (std::max)(1, motionTarget->GetHeight())
        };
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);

        cmd->SetDescriptorHeaps(1, &srvHeap);
        cmd->SetGraphicsRootSignature(state.rootSignature.Get());
        cmd->SetPipelineState(state.pipelineState.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0,
            constantSlot.buffer->GetGPUVirtualAddress());
        cmd->SetGraphicsRootDescriptorTable(1, inputs.sceneDepthSrv);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
        motionTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        MarkMotionVectorsWritten(true);
        return true;
    }

    void ShutdownMotionVectorPass() {
        MotionVectorPassState& state = State();
        for (ConstantSlot& slot : state.constants) {
            if (slot.buffer != nullptr && slot.mapped != nullptr) {
                slot.buffer->Unmap(0, nullptr);
            }
            slot.mapped = nullptr;
            slot.buffer.Reset();
        }
        state.pipelineState.Reset();
        state.rootSignature.Reset();
        state.ready = false;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
