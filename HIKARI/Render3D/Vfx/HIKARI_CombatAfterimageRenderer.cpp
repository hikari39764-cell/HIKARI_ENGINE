#include "Render3D/Vfx/HIKARI_CombatAfterimageRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/HIKARI_Mesh.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {
    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr uint32_t kJointCount =
            kMaximumCombatAfterimageJoints;

        struct alignas(16) AfterimageCameraConstants {
            MATH::Mat4 viewProj{};
            MATH::Vec4 cameraPos{};
        };

        struct alignas(16) AfterimageDrawConstants {
            MATH::Mat4 world{};
            MATH::Vec4 effect0{};
            MATH::Vec4 effect1{};
            MATH::Vec4 color{};
            MATH::Mat4 jointMatrices[kJointCount]{};
        };

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
                    "CombatAfterimage::CreateUploadBuffer")) {
                return false;
            }
            resource->SetName(name);
            if (!HIKARI_DX_CHECK(
                    resource->Map(
                        0u,
                        nullptr,
                        reinterpret_cast<void**>(&mapped)),
                    "CombatAfterimage::MapUploadBuffer")) {
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

    bool CombatAfterimageRenderer::Render(
        const GFX::Context& graphics,
        const PIPELINE::RenderTargetAccess& targetAccess,
        const CombatAfterimageFrameSource& source,
        const MESHRENDERER::CameraCB& camera) {
        debugState_ = {};
        debugState_.requested = source.HasAfterimages();
        debugState_.missingPose = source.missingPose;
        debugState_.drawCount = source.drawCount;
        debugState_.silhouetteCount = source.silhouetteCount;
        if (!source.HasAfterimages()) {
            return true;
        }
        if (graphics.device == nullptr || graphics.cmdList == nullptr ||
            source.draws == nullptr || source.drawCount == 0u ||
            !EnsurePipeline(graphics.device)) {
            return false;
        }
        debugState_.pipelineReady = true;

        const uint32_t drawCount = (std::min)(
            source.drawCount,
            kMaximumCombatAfterimageDraws);
        debugState_.skippedDrawCount = source.drawCount - drawCount;
        if (!EnsureFrameResources(
                graphics.device,
                frames_[graphics.frameIndex % GFX::kFrameResourceCount],
                drawCount)) {
            return false;
        }
        FrameResources& frame =
            frames_[graphics.frameIndex % GFX::kFrameResourceCount];
        const AfterimageCameraConstants cameraConstants{
            camera.viewProj,
            camera.cameraPos };
        std::memcpy(
            frame.cameraMapped,
            &cameraConstants,
            sizeof(cameraConstants));

        if (!targetAccess.BeginDepthRead()) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        GFX::PIX::ScopedGpuEvent event(
            cmd,
            GFX::PIX::kColorRender,
            "Combat.Afterimage");
        cmd->SetGraphicsRootSignature(rootSignature_.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0u,
            frame.cameraBuffer->GetGPUVirtualAddress());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const UINT drawStride = AlignConstantBufferSize(
            static_cast<uint32_t>(sizeof(AfterimageDrawConstants)));
        for (uint32_t drawIndex = 0u; drawIndex < drawCount; ++drawIndex) {
            const CombatAfterimageDraw& draw = source.draws[drawIndex];
            const MeshPrimitive* primitive = draw.primitive;
            if (primitive == nullptr) {
                ++debugState_.skippedDrawCount;
                continue;
            }
            const bool skinned = draw.skinned != 0u;
            Mesh* mesh = nullptr;
            ID3D12PipelineState* pipeline = nullptr;
            if (skinned) {
                // The glTF importer can retain the legacy StaticPNTT tag
                // while supplying a populated skinned stream.  Match the
                // live MeshPrimitiveCache semantic: skinned data wins over
                // the legacy layout tag, provided the draw has a valid
                // palette.
                if (primitive->layout != VertexLayoutKind::SkinnedPNTTJW) {
                    debugState_.layoutMismatch = true;
                }
                if (primitive->skinnedVertices.empty() ||
                    primitive->indices.empty() ||
                    draw.jointCount == 0u ||
                    draw.jointCount > kJointCount) {
                    ++debugState_.skippedDrawCount;
                    continue;
                }
                mesh = primitiveCache_.GetOrCreateSkinned(
                    graphics.device,
                    *primitive,
                    nullptr);
                pipeline = skinnedPipelineState_.Get();
            } else {
                if (primitive->layout != VertexLayoutKind::StaticPNTT ||
                    !primitive->skinnedVertices.empty() ||
                    primitive->staticVertices.empty() ||
                    primitive->indices.empty() ||
                    draw.jointCount != 0u) {
                    ++debugState_.skippedDrawCount;
                    continue;
                }
                mesh = primitiveCache_.GetOrCreateStatic(
                    graphics.device,
                    *primitive,
                    nullptr);
                pipeline = staticPipelineState_.Get();
            }
            if (mesh == nullptr || !mesh->IsValid()) {
                ++debugState_.skippedDrawCount;
                continue;
            }
            cmd->SetPipelineState(pipeline);

            AfterimageDrawConstants constants{};
            constants.world = draw.world;
            constants.effect0 = {
                draw.opacity,
                draw.intensity,
                draw.uniformScale,
                draw.shellOffsetWorld };
            constants.effect1 = {
                draw.ageSeconds,
                draw.lifetimeSeconds,
                draw.fresnelPower,
                draw.dissolveStrength };
            constants.color = {
                draw.color.x,
                draw.color.y,
                draw.color.z,
                1.0f };
            for (uint32_t jointIndex = 0u;
                jointIndex < kJointCount;
                ++jointIndex) {
                constants.jointMatrices[jointIndex] =
                    jointIndex < draw.jointCount
                    ? draw.jointPalette[jointIndex]
                    : MATH::Mat4::Identity();
            }
            uint8_t* destination = frame.drawMapped +
                static_cast<size_t>(drawStride) * drawIndex;
            std::memcpy(destination, &constants, sizeof(constants));
            cmd->SetGraphicsRootConstantBufferView(
                1u,
                frame.drawBuffer->GetGPUVirtualAddress() +
                    static_cast<UINT64>(drawStride) * drawIndex);
            const D3D12_VERTEX_BUFFER_VIEW vertexView = mesh->GetVBView();
            const D3D12_INDEX_BUFFER_VIEW indexView = mesh->GetIBView();
            cmd->IASetVertexBuffers(0u, 1u, &vertexView);
            cmd->IASetIndexBuffer(&indexView);
            cmd->DrawIndexedInstanced(
                mesh->GetIndexCount(),
                1u,
                0u,
                0,
                0u);
            if (skinned) {
                ++debugState_.skinnedDrawCount;
            } else {
                ++debugState_.staticDrawCount;
            }
            debugState_.rendered = true;
        }

        targetAccess.EndDepthRead();
        if (!targetAccess.Rebind()) {
            return false;
        }
        return true;
    }

    bool CombatAfterimageRenderer::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr &&
            skinnedPipelineState_ != nullptr &&
            staticPipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }
        rootSignature_.Reset();
        skinnedPipelineState_.Reset();
        staticPipelineState_.Reset();

        D3D12_ROOT_PARAMETER parameters[2]{};
        for (uint32_t index = 0u; index < 2u; ++index) {
            parameters[index].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[index].ShaderVisibility =
                D3D12_SHADER_VISIBILITY_ALL;
            parameters[index].Descriptor.ShaderRegister = index;
        }
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 2u;
        rootDesc.pParameters = parameters;
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
                "CombatAfterimage::SerializeRootSignature") ||
            !HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature_.GetAddressOf())),
                "CombatAfterimage::CreateRootSignature")) {
            return false;
        }

        ComPtr<ID3DBlob> skinnedVertexShader{};
        ComPtr<ID3DBlob> staticVertexShader{};
        ComPtr<ID3DBlob> pixelShader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_CombatAfterimage.hlsl",
                "VSMain",
                GFX::ShaderStage::Vertex,
                skinnedVertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_CombatAfterimage.hlsl",
                "VSStaticMain",
                GFX::ShaderStage::Vertex,
                staticVertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_CombatAfterimage.hlsl",
                "PSMain",
                GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[CombatAfterimage][ERROR] shader compile failed.");
            rootSignature_.Reset();
            return false;
        }

        const D3D12_INPUT_ELEMENT_DESC skinnedInput[] = {
            { "POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "NORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "JOINTS", 0u, DXGI_FORMAT_R16G16B16A16_UINT, 0u,
                static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "WEIGHTS", 0u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
        };
        const D3D12_INPUT_ELEMENT_DESC staticInput[] = {
            { "POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexStatic3D, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "NORMAL", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexStatic3D, normal)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                static_cast<UINT>(offsetof(VertexStatic3D, u)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.VS = {
            skinnedVertexShader->GetBufferPointer(),
            skinnedVertexShader->GetBufferSize() };
        description.PS = {
            pixelShader->GetBufferPointer(),
            pixelShader->GetBufferSize() };
        description.BlendState = PremultipliedBlend();
        description.SampleMask = UINT_MAX;
        description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc =
            D3D12_COMPARISON_FUNC_LESS_EQUAL;
        description.InputLayout = {
            skinnedInput,
            static_cast<UINT>(std::size(skinnedInput)) };
        description.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1u;
        description.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1u;
        if (!HIKARI_DX_CHECK(
                device->CreateGraphicsPipelineState(
                    &description,
                    IID_PPV_ARGS(skinnedPipelineState_.GetAddressOf())),
                "CombatAfterimage::CreateSkinnedPipelineState")) {
            rootSignature_.Reset();
            return false;
        }

        description.VS = {
            staticVertexShader->GetBufferPointer(),
            staticVertexShader->GetBufferSize() };
        description.InputLayout = {
            staticInput,
            static_cast<UINT>(std::size(staticInput)) };
        if (!HIKARI_DX_CHECK(
                device->CreateGraphicsPipelineState(
                    &description,
                    IID_PPV_ARGS(staticPipelineState_.GetAddressOf())),
                "CombatAfterimage::CreateStaticPipelineState")) {
            skinnedPipelineState_.Reset();
            rootSignature_.Reset();
            return false;
        }
        return true;
    }

    bool CombatAfterimageRenderer::EnsureFrameResources(
        ID3D12Device* device,
        FrameResources& frame,
        size_t drawCount) {
        if (device == nullptr) {
            return false;
        }
        if (frame.cameraBuffer == nullptr || frame.cameraMapped == nullptr) {
            const uint32_t cameraSize = AlignConstantBufferSize(
                static_cast<uint32_t>(sizeof(AfterimageCameraConstants)));
            if (!CreateUploadBuffer(
                    device,
                    cameraSize,
                    L"HIKARI.CombatAfterimage.Camera",
                    frame.cameraBuffer,
                    frame.cameraMapped)) {
                return false;
            }
        }
        if (frame.drawBuffer != nullptr && frame.drawMapped != nullptr &&
            frame.drawCapacity >= drawCount) {
            return true;
        }
        if (frame.drawBuffer != nullptr && frame.drawMapped != nullptr) {
            frame.drawBuffer->Unmap(0u, nullptr);
        }
        frame.drawBuffer.Reset();
        frame.drawMapped = nullptr;
        frame.drawCapacity = (std::max)(drawCount, size_t{ 32u });
        const uint32_t drawStride = AlignConstantBufferSize(
            static_cast<uint32_t>(sizeof(AfterimageDrawConstants)));
        return CreateUploadBuffer(
            device,
            static_cast<UINT64>(drawStride) * frame.drawCapacity,
            L"HIKARI.CombatAfterimage.Draw",
            frame.drawBuffer,
            frame.drawMapped);
    }

    void CombatAfterimageRenderer::ReleaseFrame(
        FrameResources& frame) noexcept {
        if (frame.cameraBuffer != nullptr && frame.cameraMapped != nullptr) {
            frame.cameraBuffer->Unmap(0u, nullptr);
        }
        if (frame.drawBuffer != nullptr && frame.drawMapped != nullptr) {
            frame.drawBuffer->Unmap(0u, nullptr);
        }
        frame = {};
    }

    void CombatAfterimageRenderer::Release() {
        for (FrameResources& frame : frames_) {
            ReleaseFrame(frame);
        }
        primitiveCache_.Clear();
        staticPipelineState_.Reset();
        skinnedPipelineState_.Reset();
        rootSignature_.Reset();
        debugState_ = {};
    }

} // namespace HIKARI::RENDER3D::COMBAT_VFX
