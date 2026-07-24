#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "HIKARI_Services.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Mesh.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr uint32_t kFrameSlotCount = 3u;
        constexpr uint32_t kMaxJointCount = 128u;

        struct GeometryConstants {
            MATH::Mat4 renderViewProj{};
            MATH::Mat4 currentViewProj{};
            MATH::Mat4 previousViewProj{};
            MATH::Mat4 currentWorld{};
            MATH::Mat4 previousWorld{};
            MATH::Vec4 screenParams{};
            MATH::Vec4 temporalParams{};
        };

        struct SkinConstants {
            MATH::Mat4 current[kMaxJointCount]{};
            MATH::Mat4 previous[kMaxJointCount]{};
        };

        struct UploadSlot {
            ComPtr<ID3D12Resource> geometryBuffer{};
            ComPtr<ID3D12Resource> skinBuffer{};
            uint8_t* geometryMapped = nullptr;
            uint8_t* skinMapped = nullptr;
            uint32_t capacity = 0;
        };

        struct ObjectHistory {
            MATH::Mat4 world{};
            std::vector<MATH::Mat4> jointPalette{};
            uint64_t frameIndex = 0;
        };

        struct TemporalGeometryPassState {
            ComPtr<ID3D12RootSignature> rootSignature{};
            ComPtr<ID3D12PipelineState> staticPso[2]{};
            ComPtr<ID3D12PipelineState> skinnedPso[2]{};
            std::array<UploadSlot, kFrameSlotCount> uploads{};
            std::unordered_map<uint64_t, ObjectHistory> history{};
            bool ready = false;
        };

        TemporalGeometryPassState& State() {
            static TemporalGeometryPassState state{};
            return state;
        }

        uint32_t GrowCapacity(uint32_t required) {
            uint32_t capacity = 16u;
            while (capacity < required && capacity <= (UINT32_MAX / 2u)) {
                capacity *= 2u;
            }
            return (std::max)(capacity, required);
        }

        void ReleaseUploadSlot(UploadSlot& slot) {
            if (slot.geometryBuffer != nullptr && slot.geometryMapped != nullptr) {
                slot.geometryBuffer->Unmap(0, nullptr);
            }
            if (slot.skinBuffer != nullptr && slot.skinMapped != nullptr) {
                slot.skinBuffer->Unmap(0, nullptr);
            }
            slot.geometryMapped = nullptr;
            slot.skinMapped = nullptr;
            slot.geometryBuffer.Reset();
            slot.skinBuffer.Reset();
            slot.capacity = 0;
        }

        bool CreateUploadBuffer(
            ID3D12Device* device,
            UINT64 bytes,
            const wchar_t* name,
            ComPtr<ID3D12Resource>& resource,
            uint8_t*& mapped) {

            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
            const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            const HRESULT hr = device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf()));
            if (!HIKARI_DX_CHECK(hr, "TemporalGeometryPass::CreateUploadBuffer")) {
                return false;
            }
            resource->SetName(name);
            const CD3DX12_RANGE readRange(0, 0);
            if (FAILED(resource->Map(
                    0,
                    &readRange,
                    reinterpret_cast<void**>(&mapped)))) {
                resource.Reset();
                mapped = nullptr;
                return false;
            }
            return true;
        }

        bool EnsureUploadSlot(
            ID3D12Device* device,
            UploadSlot& slot,
            uint32_t drawCount) {

            if (drawCount == 0u) {
                return true;
            }
            if (slot.capacity >= drawCount &&
                slot.geometryBuffer != nullptr &&
                slot.skinBuffer != nullptr &&
                slot.geometryMapped != nullptr &&
                slot.skinMapped != nullptr) {
                return true;
            }

            ReleaseUploadSlot(slot);
            slot.capacity = GrowCapacity(drawCount);
            const UINT geometryStride =
                GFX::AlignD3D12ConstantBufferByteSize(sizeof(GeometryConstants));
            const UINT skinStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkinConstants));
            if (!CreateUploadBuffer(
                    device,
                    static_cast<UINT64>(geometryStride) * slot.capacity,
                    L"HIKARI.Temporal.GeometryConstants",
                    slot.geometryBuffer,
                    slot.geometryMapped) ||
                !CreateUploadBuffer(
                    device,
                    static_cast<UINT64>(skinStride) * slot.capacity,
                    L"HIKARI.Temporal.SkinConstants",
                    slot.skinBuffer,
                    slot.skinMapped)) {
                ReleaseUploadSlot(slot);
                return false;
            }
            return true;
        }

        bool CreatePipelineVariant(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            ID3DBlob* vertexShader,
            ID3DBlob* pixelShader,
            const D3D12_INPUT_ELEMENT_DESC* inputElements,
            UINT inputElementCount,
            bool doubleSided,
            ID3D12PipelineState** outPipeline) {

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rootSignature;
            desc.VS = {
                vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize()
            };
            desc.PS = {
                pixelShader->GetBufferPointer(),
                pixelShader->GetBufferSize()
            };
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode =
                doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            // Reconstruct coverage from the finished opaque depth buffer. Exact
            // depth ownership preserves alpha-tested silhouettes without
            // rebinding every material texture in this sidecar pass.
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
            desc.InputLayout = { inputElements, inputElementCount };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 2;
            desc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
            desc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(outPipeline)));
        }

        bool EnsurePipeline(ID3D12Device* device) {
            TemporalGeometryPassState& state = State();
            if (state.ready && state.rootSignature != nullptr) {
                return true;
            }
            if (device == nullptr) {
                return false;
            }

            D3D12_ROOT_PARAMETER params[2]{};
            for (uint32_t index = 0; index < 2u; ++index) {
                params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                params[index].Descriptor.ShaderRegister = index;
            }
            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = 2;
            rootDesc.pParameters = params;
            rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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
            if (!HIKARI_DX_CHECK(hr, "TemporalGeometryPass::CreateRootSignature")) {
                return false;
            }
            state.rootSignature->SetName(L"HIKARI.Temporal.GeometryRootSignature");

            ComPtr<ID3DBlob> staticVs;
            ComPtr<ID3DBlob> skinnedVs;
            ComPtr<ID3DBlob> ps;
            if (!GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_GeometryMotion.hlsl",
                    "VSStatic",
                    GFX::ShaderStage::Vertex,
                    staticVs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_GeometryMotion.hlsl",
                    "VSSkinned",
                    GFX::ShaderStage::Vertex,
                    skinnedVs.GetAddressOf()) ||
                !GFX::CompileShaderFileSm6(
                    L"HIKARI/Shaders/Temporal_GeometryMotion.hlsl",
                    "PSMain",
                    GFX::ShaderStage::Pixel,
                    ps.GetAddressOf())) {
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC staticLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                    static_cast<UINT>(offsetof(VertexStatic3D, position)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };
            const D3D12_INPUT_ELEMENT_DESC skinnedLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                    static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0,
                    static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                    static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
            };

            for (uint32_t doubleSided = 0; doubleSided < 2u; ++doubleSided) {
                if (!CreatePipelineVariant(
                        device,
                        state.rootSignature.Get(),
                        staticVs.Get(),
                        ps.Get(),
                        staticLayout,
                        static_cast<UINT>(std::size(staticLayout)),
                        doubleSided != 0u,
                        state.staticPso[doubleSided].GetAddressOf()) ||
                    !CreatePipelineVariant(
                        device,
                        state.rootSignature.Get(),
                        skinnedVs.Get(),
                        ps.Get(),
                        skinnedLayout,
                        static_cast<UINT>(std::size(skinnedLayout)),
                        doubleSided != 0u,
                        state.skinnedPso[doubleSided].GetAddressOf())) {
                    return false;
                }
            }
            state.ready = true;
            return true;
        }

        void FillSkinConstants(
            const std::vector<MATH::Mat4>& current,
            const std::vector<MATH::Mat4>* previous,
            SkinConstants& constants) {

            const MATH::Mat4 identity = MATH::Mat4::Identity();
            for (uint32_t index = 0; index < kMaxJointCount; ++index) {
                constants.current[index] = identity;
                constants.previous[index] = identity;
            }
            const size_t currentCount =
                (std::min)(current.size(), static_cast<size_t>(kMaxJointCount));
            for (size_t index = 0; index < currentCount; ++index) {
                constants.current[index] = current[index];
                constants.previous[index] = current[index];
            }
            if (previous == nullptr) {
                return;
            }
            const size_t previousCount =
                (std::min)(previous->size(), static_cast<size_t>(kMaxJointCount));
            for (size_t index = 0; index < previousCount; ++index) {
                constants.previous[index] = (*previous)[index];
            }
        }

        uint64_t ResolveHistoryKey(const MESHRENDERER::TemporalVelocityDraw& draw) {
            if (draw.objectId != 0u) {
                return draw.objectId;
            }
            return static_cast<uint64_t>(draw.vertexBuffer.BufferLocation);
        }
    }

    bool ExecuteTemporalGeometryPass(
        const TemporalInputs& inputs,
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDepthDsv) {

        ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
        ID3D12Device* device = SERVICES::gCtx.device;
        RenderTarget2D* motionTarget = GetMotionVectorRenderTarget();
        RenderTarget2D* metadataTarget = GetMotionMetadataRenderTarget();
        if (cmd == nullptr ||
            device == nullptr ||
            motionTarget == nullptr ||
            metadataTarget == nullptr ||
            readOnlyDepthDsv.ptr == 0 ||
            !inputs.motionVectors.valid ||
            !inputs.motionMetadata.valid) {
            return false;
        }

        std::vector<MESHRENDERER::TemporalVelocityDraw> draws;
        MESHRENDERER::GatherTemporalVelocityDraws(draws);
        uint32_t rigidDrawCount = 0;
        uint32_t skinnedDrawCount = 0;
        uint32_t alphaMaskedDrawCount = 0;
        for (const MESHRENDERER::TemporalVelocityDraw& draw : draws) {
            if (draw.skinned) {
                ++skinnedDrawCount;
            } else {
                ++rigidDrawCount;
            }
            if (draw.alphaMasked) {
                ++alphaMaskedDrawCount;
            }
        }
        SetTemporalGeometryDrawCounts(
            rigidDrawCount,
            skinnedDrawCount,
            alphaMaskedDrawCount);
        TemporalGeometryPassState& state = State();
        if (inputs.frame.resetHistory) {
            state.history.clear();
        }
        if (draws.empty()) {
            return true;
        }
        if (!EnsurePipeline(device)) {
            return false;
        }

        UploadSlot& upload =
            state.uploads[inputs.frame.frameIndex % kFrameSlotCount];
        if (!EnsureUploadSlot(
                device,
                upload,
                static_cast<uint32_t>(draws.size()))) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorRender,
            "Temporal.GeometryMotion");
        motionTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {
            motionTarget->GetRtvHandle(),
            metadataTarget->GetRtvHandle()
        };
        cmd->OMSetRenderTargets(2, rtvs, FALSE, &readOnlyDepthDsv);
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
        cmd->SetGraphicsRootSignature(state.rootSignature.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const UINT geometryStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(GeometryConstants));
        const UINT skinStride = GFX::AlignD3D12ConstantBufferByteSize(sizeof(SkinConstants));
        for (size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
            const MESHRENDERER::TemporalVelocityDraw& draw = draws[drawIndex];
            const uint64_t historyKey = ResolveHistoryKey(draw);
            const auto historyIt = state.history.find(historyKey);
            const bool previousValid =
                inputs.frame.historyValid &&
                historyIt != state.history.end() &&
                historyIt->second.frameIndex + 1u == inputs.frame.frameIndex;

            GeometryConstants geometry{};
            geometry.renderViewProj = inputs.frame.camera.viewProj;
            geometry.currentViewProj = inputs.frame.camera.unjitteredViewProj;
            geometry.previousViewProj = inputs.frame.camera.prevUnjitteredViewProj;
            geometry.currentWorld = draw.world;
            geometry.previousWorld =
                previousValid ? historyIt->second.world : draw.world;
            geometry.screenParams = inputs.frame.camera.screenParams;
            geometry.temporalParams = {
                inputs.frame.historyValid ? 1.0f : 0.0f,
                0.0f,
                0.0f,
                0.0f
            };
            std::memcpy(
                upload.geometryMapped + geometryStride * drawIndex,
                &geometry,
                sizeof(geometry));

            SkinConstants skin{};
            if (draw.skinned && draw.jointPalette != nullptr) {
                const std::vector<MATH::Mat4>* previousPalette =
                    previousValid ? &historyIt->second.jointPalette : nullptr;
                FillSkinConstants(*draw.jointPalette, previousPalette, skin);
            }
            std::memcpy(
                upload.skinMapped + skinStride * drawIndex,
                &skin,
                sizeof(skin));

            cmd->SetPipelineState(
                draw.skinned
                    ? state.skinnedPso[draw.doubleSided ? 1u : 0u].Get()
                    : state.staticPso[draw.doubleSided ? 1u : 0u].Get());
            cmd->SetGraphicsRootConstantBufferView(
                0,
                upload.geometryBuffer->GetGPUVirtualAddress() +
                    static_cast<UINT64>(geometryStride) * drawIndex);
            cmd->SetGraphicsRootConstantBufferView(
                1,
                upload.skinBuffer->GetGPUVirtualAddress() +
                    static_cast<UINT64>(skinStride) * drawIndex);
            cmd->IASetVertexBuffers(0, 1, &draw.vertexBuffer);
            cmd->IASetIndexBuffer(&draw.indexBuffer);
            cmd->DrawIndexedInstanced(
                draw.indexCount,
                1,
                draw.startIndex,
                draw.baseVertex,
                0);
        }
        motionTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        metadataTarget->TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        for (const MESHRENDERER::TemporalVelocityDraw& draw : draws) {
            ObjectHistory& history = state.history[ResolveHistoryKey(draw)];
            history.world = draw.world;
            history.frameIndex = inputs.frame.frameIndex;
            if (draw.skinned && draw.jointPalette != nullptr) {
                history.jointPalette = *draw.jointPalette;
            } else {
                history.jointPalette.clear();
            }
        }
        for (auto it = state.history.begin(); it != state.history.end();) {
            if (it->second.frameIndex + 120u < inputs.frame.frameIndex) {
                it = state.history.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }

    void ShutdownTemporalGeometryPass() {
        TemporalGeometryPassState& state = State();
        for (UploadSlot& slot : state.uploads) {
            ReleaseUploadSlot(slot);
        }
        for (uint32_t index = 0; index < 2u; ++index) {
            state.staticPso[index].Reset();
            state.skinnedPso[index].Reset();
        }
        state.rootSignature.Reset();
        state.history.clear();
        state.ready = false;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
