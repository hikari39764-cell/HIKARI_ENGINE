#include "Render3D/Vfx/HIKARI_GroundTelegraphRenderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {
    namespace {
        using Microsoft::WRL::ComPtr;

        struct alignas(16) GroundTelegraphConstants {
            MATH::Mat4 viewProj{};
            MATH::Vec4 timeParams{};
        };

        static_assert(sizeof(GroundTelegraphVertex) == 44u);
        static_assert(offsetof(GroundTelegraphVertex, uv) == 28u);
        static_assert(offsetof(GroundTelegraphVertex, effect) == 36u);
        constexpr size_t kPrewarmedGroundTelegraphVertexCapacity = 12288u;

        uint32_t AlignConstantBufferSize(uint32_t size) noexcept {
            return (size + 255u) & ~255u;
        }

        bool CreateUploadBuffer(
            ID3D12Device* device,
            UINT64 bytes,
            const wchar_t* name,
            ComPtr<ID3D12Resource>& resource,
            uint8_t*& mapped) {
            if (device == nullptr || bytes == 0u) {
                return false;
            }
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
            const CD3DX12_RESOURCE_DESC desc =
                CD3DX12_RESOURCE_DESC::Buffer(bytes);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &heap,
                        D3D12_HEAP_FLAG_NONE,
                        &desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(resource.GetAddressOf())),
                    "GroundTelegraph::CreateUploadBuffer")) {
                return false;
            }
            resource->SetName(name);
            if (!HIKARI_DX_CHECK(
                    resource->Map(
                        0u,
                        nullptr,
                        reinterpret_cast<void**>(&mapped)),
                    "GroundTelegraph::MapUploadBuffer")) {
                resource.Reset();
                mapped = nullptr;
                return false;
            }
            return true;
        }

        D3D12_BLEND_DESC PremultipliedBlend() noexcept {
            D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            D3D12_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.LogicOpEnable = FALSE;
            target.SrcBlend = D3D12_BLEND_ONE;
            target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D12_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            return blend;
        }
    }

    bool GroundTelegraphRenderer::Initialize(ID3D12Device* device) {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr &&
            !initializationFailed_) {
            return true;
        }
        initializationAttempted_ = true;
        initializationFailed_ = false;
        if (!EnsurePipeline(device)) {
            initializationFailed_ = true;
            return false;
        }
        for (FrameResources& frame : frames_) {
            if (!EnsureFrameResources(
                    device,
                    frame,
                    kPrewarmedGroundTelegraphVertexCapacity)) {
                initializationFailed_ = true;
                return false;
            }
        }
        return true;
    }

    bool GroundTelegraphRenderer::Render(
        const GFX::Context& graphics,
        const PIPELINE::RenderTargetAccess& targetAccess,
        const CombatPresentationFrameSource& source,
        const MESHRENDERER::CameraCB& camera) {
        debugState_ = {};
        debugState_.requested = source.HasGroundTelegraphs();
        debugState_.telegraphCount = source.activeGroundTelegraphCount;
        debugState_.vertexCount = source.groundTelegraphVertexCount;
        if (!source.HasGroundTelegraphs()) {
            return true;
        }
        if (graphics.device == nullptr || graphics.cmdList == nullptr ||
            source.groundTelegraphVertices == nullptr ||
            source.groundTelegraphVertexCount < 3u) {
            return true;
        }
        if ((pipelineState_ == nullptr || rootSignature_ == nullptr) &&
            (!initializationAttempted_ || !initializationFailed_)) {
            (void)Initialize(graphics.device);
        }
        if (initializationFailed_ || pipelineState_ == nullptr ||
            rootSignature_ == nullptr) {
            // Telegraph presentation is optional. A missing shader/PSO must
            // never fail the main scene/material frame or retry compilation
            // during every Boss attack.
            return true;
        }
        debugState_.pipelineReady = true;
        FrameResources& frame =
            frames_[graphics.frameIndex % GFX::kFrameResourceCount];
        if (!EnsureFrameResources(
                graphics.device,
                frame,
                source.groundTelegraphVertexCount)) {
            return true;
        }
        const GroundTelegraphConstants constants{
            camera.viewProj,
            { source.unscaledTimeSeconds, 0.0f, 0.0f, 0.0f } };
        std::memcpy(
            frame.constantMapped,
            &constants,
            sizeof(constants));
        std::memcpy(
            frame.vertexMapped,
            source.groundTelegraphVertices,
            sizeof(GroundTelegraphVertex) *
                source.groundTelegraphVertexCount);

        if (!targetAccess.BeginDepthRead()) {
            return true;
        }
        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        GFX::PIX::ScopedGpuEvent event(
            cmd,
            GFX::PIX::kColorRender,
            "Combat.GroundTelegraph");
        cmd->SetGraphicsRootSignature(rootSignature_.Get());
        cmd->SetPipelineState(pipelineState_.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0u,
            frame.constantBuffer->GetGPUVirtualAddress());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VERTEX_BUFFER_VIEW vertexView{
            frame.vertexBuffer->GetGPUVirtualAddress(),
            static_cast<UINT>(
                sizeof(GroundTelegraphVertex) *
                source.groundTelegraphVertexCount),
            static_cast<UINT>(sizeof(GroundTelegraphVertex)) };
        cmd->IASetVertexBuffers(0u, 1u, &vertexView);
        cmd->DrawInstanced(
            source.groundTelegraphVertexCount,
            1u,
            0u,
            0u);
        targetAccess.EndDepthRead();
        if (!targetAccess.Rebind()) {
            return true;
        }
        debugState_.rendered = true;
        return true;
    }

    bool GroundTelegraphRenderer::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }
        rootSignature_.Reset();
        pipelineState_.Reset();

        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        // Both stages consume GroundTelegraphConstants: the vertex shader
        // reads viewProj and the pixel shader reads the unscaled time used by
        // the pulse. Restricting b0 to VS makes D3D12 reject the PSO.
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameter.Descriptor.ShaderRegister = 0u;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 1u;
        rootDesc.pParameters = &parameter;
        rootDesc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> signature{};
        ComPtr<ID3DBlob> errors{};
        if (!HIKARI_DX_CHECK(
                D3D12SerializeRootSignature(
                    &rootDesc,
                    D3D_ROOT_SIGNATURE_VERSION_1,
                    signature.GetAddressOf(),
                    errors.GetAddressOf()),
                "GroundTelegraph::SerializeRootSignature") ||
            !HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature_.GetAddressOf())),
                "GroundTelegraph::CreateRootSignature")) {
            return false;
        }

        ComPtr<ID3DBlob> vertexShader{};
        ComPtr<ID3DBlob> pixelShader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GroundTelegraph.hlsl",
                "VSMain",
                GFX::ShaderStage::Vertex,
                vertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_GroundTelegraph.hlsl",
                "PSMain",
                GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[GroundTelegraph][ERROR] shader compile failed.");
            rootSignature_.Reset();
            return false;
        }
        const D3D12_INPUT_ELEMENT_DESC input[] = {
            { "POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(
                    GroundTelegraphVertex, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "COLOR", 0u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u,
                static_cast<UINT>(offsetof(
                    GroundTelegraphVertex, color)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                static_cast<UINT>(offsetof(
                    GroundTelegraphVertex, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 1u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                static_cast<UINT>(offsetof(
                    GroundTelegraphVertex, effect)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.VS = {
            vertexShader->GetBufferPointer(),
            vertexShader->GetBufferSize() };
        description.PS = {
            pixelShader->GetBufferPointer(),
            pixelShader->GetBufferSize() };
        description.BlendState = PremultipliedBlend();
        description.SampleMask = UINT_MAX;
        description.RasterizerState =
            CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.RasterizerState.DepthBias = -8;
        description.RasterizerState.SlopeScaledDepthBias = -1.0f;
        description.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc =
            D3D12_COMPARISON_FUNC_LESS_EQUAL;
        description.InputLayout = {
            input,
            static_cast<UINT>(std::size(input)) };
        description.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1u;
        description.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1u;
        if (!HIKARI_DX_CHECK(
                device->CreateGraphicsPipelineState(
                    &description,
                    IID_PPV_ARGS(pipelineState_.GetAddressOf())),
                "GroundTelegraph::CreatePipelineState")) {
            rootSignature_.Reset();
            return false;
        }
        return true;
    }

    bool GroundTelegraphRenderer::EnsureFrameResources(
        ID3D12Device* device,
        FrameResources& frame,
        size_t vertexCount) {
        if (device == nullptr || vertexCount == 0u) {
            return false;
        }
        if (frame.constantBuffer == nullptr ||
            frame.constantMapped == nullptr) {
            if (!CreateUploadBuffer(
                    device,
                    AlignConstantBufferSize(
                        static_cast<uint32_t>(
                            sizeof(GroundTelegraphConstants))),
                    L"HIKARI.GroundTelegraph.Constants",
                    frame.constantBuffer,
                    frame.constantMapped)) {
                return false;
            }
        }
        if (frame.vertexBuffer != nullptr &&
            frame.vertexMapped != nullptr &&
            frame.vertexCapacity >= vertexCount) {
            return true;
        }
        if (frame.vertexBuffer != nullptr && frame.vertexMapped != nullptr) {
            frame.vertexBuffer->Unmap(0u, nullptr);
        }
        frame.vertexBuffer.Reset();
        frame.vertexMapped = nullptr;
        frame.vertexCapacity = (std::max)(vertexCount, size_t{ 1024u });
        return CreateUploadBuffer(
            device,
            static_cast<UINT64>(frame.vertexCapacity) *
                sizeof(GroundTelegraphVertex),
            L"HIKARI.GroundTelegraph.Vertices",
            frame.vertexBuffer,
            frame.vertexMapped);
    }

    void GroundTelegraphRenderer::ReleaseFrame(
        FrameResources& frame) noexcept {
        if (frame.constantBuffer != nullptr &&
            frame.constantMapped != nullptr) {
            frame.constantBuffer->Unmap(0u, nullptr);
        }
        if (frame.vertexBuffer != nullptr && frame.vertexMapped != nullptr) {
            frame.vertexBuffer->Unmap(0u, nullptr);
        }
        frame = {};
    }

    void GroundTelegraphRenderer::Release() {
        for (FrameResources& frame : frames_) {
            ReleaseFrame(frame);
        }
        pipelineState_.Reset();
        rootSignature_.Reset();
        debugState_ = {};
        initializationAttempted_ = false;
        initializationFailed_ = false;
    }

} // namespace HIKARI::RENDER3D::COMBAT_VFX
