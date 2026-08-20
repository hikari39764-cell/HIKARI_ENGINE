#include "Render3D/Vfx/HIKARI_WeaponTrailRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {
    namespace {
        struct alignas(16) WeaponTrailConstants {
            MATH::Mat4 viewProj{};
            MATH::Vec4 timeParams{};
            MATH::Vec4 screenParams{};
        };

        static_assert(sizeof(WeaponTrailConstants) == 96u);
        static_assert(sizeof(WeaponTrailVertex) == 84u);
        static_assert(offsetof(WeaponTrailVertex, tangentWorld) == 44u);
        static_assert(offsetof(WeaponTrailVertex, normalWorld) == 56u);
        static_assert(offsetof(WeaponTrailVertex, flowData) == 68u);

        uint32_t AlignConstantBufferSize(uint32_t size) noexcept {
            return (size + 255u) & ~255u;
        }

        void TransitionResource(
            ID3D12GraphicsCommandList* cmd,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            D3D12_RESOURCE_STATES targetState) noexcept {
            if (cmd == nullptr || resource == nullptr ||
                currentState == targetState) {
                return;
            }
            const D3D12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    resource,
                    currentState,
                    targetState);
            cmd->ResourceBarrier(1u, &barrier);
            currentState = targetState;
        }

        const char* ToString(WeaponTrailRendererFailure failure) noexcept {
            switch (failure) {
            case WeaponTrailRendererFailure::None:
                return "none";
            case WeaponTrailRendererFailure::MissingGraphicsContext:
                return "missing graphics context";
            case WeaponTrailRendererFailure::InvalidFrameSource:
                return "invalid or stale frame source";
            case WeaponTrailRendererFailure::MissingRenderInputs:
                return "missing render inputs";
            case WeaponTrailRendererFailure::DegenerateGeometry:
                return "all swept triangles are degenerate";
            case WeaponTrailRendererFailure::PipelineInitialization:
                return "pipeline or mask initialization failed";
            case WeaponTrailRendererFailure::DepthRead:
                return "scene depth read failed";
            case WeaponTrailRendererFailure::FrameResources:
                return "frame resources failed";
            case WeaponTrailRendererFailure::MaskDescriptor:
                return "mask RTV descriptor is invalid";
            case WeaponTrailRendererFailure::RenderTargetRebind:
                return "scene render target rebind failed";
            case WeaponTrailRendererFailure::InvalidMaskView:
                return "generated mask SRV is invalid";
            default:
                return "unknown";
            }
        }

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        float TriangleAreaSquared(
            const WeaponTrailVertex& a,
            const WeaponTrailVertex& b,
            const WeaponTrailVertex& c) noexcept {
            if (!IsFinite(a.position) ||
                !IsFinite(b.position) ||
                !IsFinite(c.position)) {
                return 0.0f;
            }
            const MATH::Vec3 ab = b.position - a.position;
            const MATH::Vec3 ac = c.position - a.position;
            const MATH::Vec3 cross = MATH::Cross(ab, ac);
            return MATH::Dot(cross, cross);
        }
    }

    void WeaponTrailRenderer::SetFailure(
        WeaponTrailRendererFailure failure) noexcept {
        debugState_.failureReason = failure;
        readyLogged_ = false;
        if (failure == WeaponTrailRendererFailure::None ||
            failure == lastLoggedFailure_) {
            return;
        }
        lastLoggedFailure_ = failure;
        try {
            DEBUGLOG::WriteRenderLogLine(
                std::string("[CombatVfx][WeaponTrail] skipped: ") +
                ToString(failure));
        }
        catch (...) {
        }
    }

    void WeaponTrailRenderer::ClearFailure() noexcept {
        debugState_.failureReason = WeaponTrailRendererFailure::None;
        lastLoggedFailure_ = WeaponTrailRendererFailure::None;
    }

    bool WeaponTrailRenderer::Render(
        const PIPELINE::ScreenSpacePassContext& pass,
        const CombatPresentationFrameSource& source,
        const MATH::Mat4& viewProj) {
        debugState_ = {};
        if (pass.graphics == nullptr) {
            SetFailure(WeaponTrailRendererFailure::MissingGraphicsContext);
            return false;
        }
        const GFX::Context& graphics = *pass.graphics;
        debugState_.requested = source.HasWeaponTrails();
        debugState_.maskExtentReady = pass.width != 0u && pass.height != 0u;
        debugState_.vertexCount = source.weaponTrailVertexCount;
        debugState_.activeTrailCount = source.activeWeaponTrailCount;

        // Mark the view invalid before doing any work. A frame with no
        // samples, a resize, or a lost device must not expose the previous
        // mask to a later post pass.
        distortionMaskView_.valid = false;
        distortionMaskFrame_ = 0u;
        if (!source.HasWeaponTrails()) {
            if (graphics.cmdList != nullptr && distortionMaskTarget_ != nullptr) {
                (void)ClearDistortionMask(graphics);
            }
            ClearFailure();
            return true;
        }
        if (source.frameIndex == 0u) {
            SetFailure(WeaponTrailRendererFailure::InvalidFrameSource);
            return false;
        }
        if (graphics.device == nullptr || graphics.cmdList == nullptr ||
            graphics.srvHeap == nullptr || pass.width == 0u ||
            pass.height == 0u || !pass.sceneDepth.IsSampleable() ||
            pass.readOnlySceneDsv.ptr == 0u ||
            source.weaponTrailVertexCount == 0u ||
            source.weaponTrailVertices == nullptr) {
            SetFailure(WeaponTrailRendererFailure::MissingRenderInputs);
            return false;
        }
        debugState_.triangleCount = source.weaponTrailVertexCount / 3u;
        for (uint32_t triangle = 0u;
            triangle < debugState_.triangleCount;
            ++triangle) {
            const WeaponTrailVertex* vertices =
                source.weaponTrailVertices + triangle * 3u;
            if (TriangleAreaSquared(
                    vertices[0],
                    vertices[1],
                    vertices[2]) > 1.0e-10f) {
                ++debugState_.nonDegenerateTriangleCount;
            }
        }
        if (debugState_.nonDegenerateTriangleCount == 0u) {
            SetFailure(WeaponTrailRendererFailure::DegenerateGeometry);
            if (distortionMaskTarget_ != nullptr) {
                (void)ClearDistortionMask(graphics);
            }
            // Combat presentation is optional. A bad socket/sample history
            // must not fail the authoritative scene render.
            return true;
        }
        if (!EnsurePipeline(graphics.device) ||
            !EnsureDistortionMask(
                graphics.device,
                pass.width,
                pass.height)) {
            SetFailure(WeaponTrailRendererFailure::PipelineInitialization);
            return false;
        }
        debugState_.pipelineReady = true;

        // The pipeline owns the authoritative scene-depth contract. We borrow
        // its read-only DSV while writing our private mask RTV, so world
        // geometry still occludes the trail without allowing depth writes.
        if (!pass.renderTargetAccess.BeginDepthRead()) {
            SetFailure(WeaponTrailRendererFailure::DepthRead);
            return false;
        }

        FrameResources& frame = frames_[
            graphics.frameIndex % GFX::kFrameResourceCount];
        if (!EnsureFrameResources(
                graphics.device,
                frame,
                source.weaponTrailVertexCount)) {
            pass.renderTargetAccess.EndDepthRead();
            SetFailure(WeaponTrailRendererFailure::FrameResources);
            return false;
        }
        std::memcpy(
            frame.vertexMapped,
            source.weaponTrailVertices,
            sizeof(WeaponTrailVertex) * source.weaponTrailVertexCount);
        WeaponTrailConstants constants{};
        constants.viewProj = viewProj;
        constants.timeParams = {
            source.unscaledTimeSeconds,
            static_cast<float>(pass.width),
            static_cast<float>(pass.height),
            1.0f };
        constants.screenParams = {
            1.0f / static_cast<float>((std::max)(1u, pass.width)),
            1.0f / static_cast<float>((std::max)(1u, pass.height)),
            2.25f,
            1.0f };
        std::memcpy(frame.constantMapped, &constants, sizeof(constants));

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        const D3D12_CPU_DESCRIPTOR_HANDLE distortionMaskRtv =
            GetDistortionMaskRtv(graphics.frameIndex);
        if (distortionMaskRtv.ptr == 0u) {
            pass.renderTargetAccess.EndDepthRead();
            SetFailure(WeaponTrailRendererFailure::MaskDescriptor);
            return false;
        }
        // RTV descriptors are partitioned by the fenced frame-resource slot.
        // Rewriting only the current slot avoids mutating a descriptor still
        // referenced by an in-flight command list after a resize.
        graphics.device->CreateRenderTargetView(
            distortionMaskTarget_.Get(),
            nullptr,
            distortionMaskRtv);
        GFX::PIX::ScopedGpuEvent event(
            cmd,
            GFX::PIX::kColorRender,
            "Combat.WeaponTrailDistortionMask");
        ID3D12DescriptorHeap* heaps[] = { graphics.srvHeap };
        cmd->SetDescriptorHeaps(1u, heaps);
        cmd->SetGraphicsRootSignature(rootSignature_.Get());
        cmd->SetPipelineState(pipelineState_.Get());
        cmd->SetGraphicsRootConstantBufferView(
            0u,
            frame.constantBuffer->GetGPUVirtualAddress());

        TransitionResource(
            cmd,
            distortionMaskTarget_.Get(),
            distortionMaskState_,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clearValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        cmd->ClearRenderTargetView(
            distortionMaskRtv,
            clearValue,
            0u,
            nullptr);
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>((std::max)(1u, pass.width));
        viewport.Height = static_cast<float>((std::max)(1u, pass.height));
        viewport.MaxDepth = 1.0f;
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>((std::max)(1u, pass.width)),
            static_cast<LONG>((std::max)(1u, pass.height)),
        };
        cmd->RSSetViewports(1u, &viewport);
        cmd->RSSetScissorRects(1u, &scissor);
        cmd->OMSetRenderTargets(
            1u,
            &distortionMaskRtv,
            FALSE,
            &pass.readOnlySceneDsv);
        const D3D12_VERTEX_BUFFER_VIEW vertexView{
            frame.vertexBuffer->GetGPUVirtualAddress(),
            static_cast<UINT>(
                sizeof(WeaponTrailVertex) * source.weaponTrailVertexCount),
            static_cast<UINT>(sizeof(WeaponTrailVertex))
        };
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0u, 1u, &vertexView);
        cmd->IASetIndexBuffer(nullptr);
        cmd->DrawInstanced(source.weaponTrailVertexCount, 1u, 0u, 0u);
        TransitionResource(
            cmd,
            distortionMaskTarget_.Get(),
            distortionMaskState_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pass.renderTargetAccess.EndDepthRead();
        if (!pass.renderTargetAccess.Rebind()) {
            SetFailure(WeaponTrailRendererFailure::RenderTargetRebind);
            return false;
        }

        distortionMaskView_.resource = distortionMaskTarget_.Get();
        distortionMaskView_.srv =
            GetTextureResourceSrvGpuHandle(distortionMaskTexture_);
        distortionMaskView_.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        distortionMaskView_.width = distortionMaskWidth_;
        distortionMaskView_.height = distortionMaskHeight_;
        distortionMaskView_.mipCount = 1u;
        distortionMaskView_.state = distortionMaskState_;
        // IsSampleable() deliberately includes the producer-owned `valid`
        // bit.  Calling it while assigning that bit makes a freshly rendered
        // view permanently false.  Validate the raw handoff first, then mark
        // the borrowed view current for this frame.
        distortionMaskView_.valid =
            distortionMaskView_.resource != nullptr &&
            distortionMaskView_.srv.ptr != 0u &&
            distortionMaskView_.width != 0u &&
            distortionMaskView_.height != 0u;
        if (!distortionMaskView_.IsSampleable() ||
            distortionMaskSrvCpu_.ptr == 0u) {
            distortionMaskFrame_ = 0u;
            SetFailure(WeaponTrailRendererFailure::InvalidMaskView);
            return false;
        }
        // graphics.frameIndex is only the 0..N upload-resource slot. The
        // source frame is the absolute presentation frame used for freshness.
        distortionMaskFrame_ = source.frameIndex;
        debugState_.rendered = true;
        debugState_.maskReady = true;
        debugState_.maskWidth = distortionMaskWidth_;
        debugState_.maskHeight = distortionMaskHeight_;
        debugState_.maskFrame = distortionMaskFrame_;
        // This pass deliberately does not composite into the main HDR target.
        debugState_.composited = false;
        if (!readyLogged_) {
            try {
                DEBUGLOG::WriteRenderLogLine(
                    std::string("[CombatVfx][WeaponTrail] mask ready") +
                    " trails=" +
                    std::to_string(debugState_.activeTrailCount) +
                    " vertices=" +
                    std::to_string(debugState_.vertexCount) +
                    " triangles=" +
                    std::to_string(debugState_.nonDegenerateTriangleCount) +
                    " extent=" +
                    std::to_string(debugState_.maskWidth) + "x" +
                    std::to_string(debugState_.maskHeight) +
                    " frame=" +
                    std::to_string(debugState_.maskFrame));
                readyLogged_ = true;
            }
            catch (...) {
            }
        }
        ClearFailure();
        return true;
    }

    bool WeaponTrailRenderer::EnsurePipeline(ID3D12Device* device) {
        if (rootSignature_ != nullptr && pipelineState_ != nullptr) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }

        D3D12_ROOT_PARAMETER rootParameter{};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParameter.Descriptor.ShaderRegister = 0u;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 1u;
        rootDesc.pParameters = &rootParameter;
        rootDesc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> signature{};
        Microsoft::WRL::ComPtr<ID3DBlob> errors{};
        if (!HIKARI_DX_CHECK(
                D3D12SerializeRootSignature(
                    &rootDesc,
                    D3D_ROOT_SIGNATURE_VERSION_1,
                    signature.GetAddressOf(),
                    errors.GetAddressOf()),
                "WeaponTrail::SerializeRootSignature") ||
            !HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(rootSignature_.GetAddressOf())),
                "WeaponTrail::CreateRootSignature")) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader{};
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader{};
        if (!GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_WeaponTrail.hlsl",
                "VSMain",
                GFX::ShaderStage::Vertex,
                vertexShader.GetAddressOf()) ||
            !GFX::CompileShaderFileSm6(
                L"HIKARI/Shaders/Render3D_WeaponTrail.hlsl",
                "PSMain",
                GFX::ShaderStage::Pixel,
                pixelShader.GetAddressOf())) {
            DEBUGLOG::PushRenderError(
                "[CombatPresentation][ERROR] weapon trail shader compile failed.");
            return false;
        }
        const D3D12_INPUT_ELEMENT_DESC input[] = {
            { "POSITION", 0u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "COLOR", 0u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, color)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 1u, DXGI_FORMAT_R32_FLOAT, 0u,
                static_cast<UINT>(offsetof(
                    WeaponTrailVertex, emissiveIntensity)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 2u, DXGI_FORMAT_R32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, flowStrength)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 3u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, tangentWorld)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 4u, DXGI_FORMAT_R32G32B32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, normalWorld)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
            { "TEXCOORD", 5u, DXGI_FORMAT_R32G32B32A32_FLOAT, 0u,
                static_cast<UINT>(offsetof(WeaponTrailVertex, flowData)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature_.Get();
        desc.VS = {
            vertexShader->GetBufferPointer(),
            vertexShader->GetBufferSize() };
        desc.PS = {
            pixelShader->GetBufferPointer(),
            pixelShader->GetBufferSize() };
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        D3D12_RENDER_TARGET_BLEND_DESC& blend =
            desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ONE;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        // A small negative bias keeps the sheet attached to the actor while
        // the read-only scene DSV still hides it behind world geometry.
        desc.RasterizerState.DepthBias = -1;
        desc.RasterizerState.DepthBiasClamp = -0.0001f;
        desc.RasterizerState.SlopeScaledDepthBias = -0.25f;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.InputLayout = {
            input,
            static_cast<UINT>(std::size(input)) };
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1u;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1u;
        return HIKARI_DX_CHECK(
            device->CreateGraphicsPipelineState(
                &desc,
                IID_PPV_ARGS(pipelineState_.GetAddressOf())),
            "WeaponTrail::CreateDistortionMaskPipeline");
    }

    bool WeaponTrailRenderer::EnsureFrameResources(
        ID3D12Device* device,
        FrameResources& frame,
        size_t vertexCount) {
        if (device == nullptr || vertexCount == 0u) {
            return false;
        }
        if (frame.constantBuffer == nullptr ||
            frame.constantMapped == nullptr) {
            const uint32_t size = AlignConstantBufferSize(
                sizeof(WeaponTrailConstants));
            const auto heap = CD3DX12_HEAP_PROPERTIES(
                D3D12_HEAP_TYPE_UPLOAD);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &heap,
                        D3D12_HEAP_FLAG_NONE,
                        &desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(
                            frame.constantBuffer.GetAddressOf())),
                    "WeaponTrail::CreateConstantBuffer") ||
                !HIKARI_DX_CHECK(
                    frame.constantBuffer->Map(
                        0u,
                        nullptr,
                        reinterpret_cast<void**>(&frame.constantMapped)),
                    "WeaponTrail::MapConstantBuffer")) {
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
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(
            static_cast<UINT64>(
                frame.vertexCapacity * sizeof(WeaponTrailVertex)));
        return HIKARI_DX_CHECK(
                device->CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(frame.vertexBuffer.GetAddressOf())),
                "WeaponTrail::CreateVertexBuffer") &&
            HIKARI_DX_CHECK(
                frame.vertexBuffer->Map(
                    0u,
                    nullptr,
                    reinterpret_cast<void**>(&frame.vertexMapped)),
                "WeaponTrail::MapVertexBuffer");
    }

    bool WeaponTrailRenderer::EnsureDistortionMask(
        ID3D12Device* device,
        uint32_t width,
        uint32_t height) {
        width = (std::max)(1u, width);
        height = (std::max)(1u, height);
        if (distortionMaskTarget_ != nullptr &&
            distortionMaskTexture_ &&
            distortionMaskWidth_ == width &&
            distortionMaskHeight_ == height) {
            return true;
        }
        if (device == nullptr) {
            return false;
        }
        ReleaseDistortionMask();
        if (distortionMaskRtvHeap_ == nullptr) {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heapDesc.NumDescriptors = GFX::kFrameResourceCount;
            if (!HIKARI_DX_CHECK(
                    device->CreateDescriptorHeap(
                        &heapDesc,
                        IID_PPV_ARGS(
                            distortionMaskRtvHeap_.GetAddressOf())),
                    "WeaponTrail::CreateDistortionMaskRtvHeap")) {
                return false;
            }
            distortionMaskRtvDescriptorSize_ =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
        if (distortionMaskRtvDescriptorSize_ == 0u) {
            distortionMaskRtvDescriptorSize_ =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
        constexpr DXGI_FORMAT kMaskFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        const D3D12_HEAP_PROPERTIES heapProperties =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC textureDesc =
            CD3DX12_RESOURCE_DESC::Tex2D(
                kMaskFormat,
                width,
                height,
                1u,
                1u,
                1u,
                0u,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = kMaskFormat;
        if (!HIKARI_DX_CHECK(
                device->CreateCommittedResource(
                    &heapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &textureDesc,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    &clearValue,
                    IID_PPV_ARGS(distortionMaskTarget_.GetAddressOf())),
                "WeaponTrail::CreateDistortionMask")) {
            return false;
        }
        distortionMaskTarget_->SetName(L"HIKARI.Combat.WeaponTrailMask");
        RenderResourceDesc resourceDesc{};
        resourceDesc.usage =
            RenderResourceUsageFlags::RenderTarget |
            RenderResourceUsageFlags::ShaderResource;
        resourceDesc.lifetime = RenderResourceLifetime::External;
        resourceDesc.debugName = "combat_weapon_trail_distortion_mask";
        resourceDesc.sourceKey = "render/combat/weapon_trail_distortion_mask";
        resourceDesc.width = width;
        resourceDesc.height = height;
        resourceDesc.mipLevels = 1u;
        resourceDesc.depthOrArraySize = 1u;
        resourceDesc.format = kMaskFormat;
        distortionMaskTexture_ = RegisterTextureResourceFromNative(
            distortionMaskTarget_.Get(),
            kMaskFormat,
            std::move(resourceDesc));
        if (!distortionMaskTexture_) {
            ReleaseDistortionMask();
            return false;
        }
        distortionMaskWidth_ = width;
        distortionMaskHeight_ = height;
        distortionMaskSrvCpu_ =
            GetTextureResourceSrvCpuHandle(distortionMaskTexture_);
        distortionMaskState_ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        distortionMaskView_ = {
            distortionMaskTarget_.Get(),
            GetTextureResourceSrvGpuHandle(distortionMaskTexture_),
            kMaskFormat,
            width,
            height,
            1u,
            distortionMaskState_,
            false };
        distortionMaskFrame_ = 0u;
        return distortionMaskView_.srv.ptr != 0u;
    }

    bool WeaponTrailRenderer::ClearDistortionMask(
        const GFX::Context& graphics) noexcept {
        distortionMaskView_.valid = false;
        distortionMaskFrame_ = 0u;
        if (graphics.cmdList == nullptr || distortionMaskTarget_ == nullptr) {
            return false;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE distortionMaskRtv =
            GetDistortionMaskRtv(graphics.frameIndex);
        if (graphics.device == nullptr || distortionMaskRtv.ptr == 0u) {
            return false;
        }
        graphics.device->CreateRenderTargetView(
            distortionMaskTarget_.Get(),
            nullptr,
            distortionMaskRtv);
        TransitionResource(
            graphics.cmdList,
            distortionMaskTarget_.Get(),
            distortionMaskState_,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clearValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        graphics.cmdList->ClearRenderTargetView(
            distortionMaskRtv,
            clearValue,
            0u,
            nullptr);
        TransitionResource(
            graphics.cmdList,
            distortionMaskTarget_.Get(),
            distortionMaskState_,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        distortionMaskView_.state = distortionMaskState_;
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE WeaponTrailRenderer::GetDistortionMaskRtv(
        uint32_t frameSlot) const noexcept {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (distortionMaskRtvHeap_ == nullptr ||
            distortionMaskRtvDescriptorSize_ == 0u) {
            return handle;
        }
        handle = distortionMaskRtvHeap_->
            GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(
            frameSlot % GFX::kFrameResourceCount) *
            distortionMaskRtvDescriptorSize_;
        return handle;
    }

    void WeaponTrailRenderer::ReleaseDistortionMask() noexcept {
        if (distortionMaskTexture_) {
            (void)ReleaseTextureResource(distortionMaskTexture_);
        }
        distortionMaskTexture_ = {};
        distortionMaskTarget_.Reset();
        distortionMaskSrvCpu_ = {};
        distortionMaskWidth_ = 0u;
        distortionMaskHeight_ = 0u;
        distortionMaskState_ =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        distortionMaskView_ = {};
        distortionMaskFrame_ = 0u;
    }

    void WeaponTrailRenderer::ReleaseFrame(
        FrameResources& frame) noexcept {
        if (frame.vertexBuffer != nullptr && frame.vertexMapped != nullptr) {
            frame.vertexBuffer->Unmap(0u, nullptr);
        }
        if (frame.constantBuffer != nullptr &&
            frame.constantMapped != nullptr) {
            frame.constantBuffer->Unmap(0u, nullptr);
        }
        frame = {};
    }

    void WeaponTrailRenderer::Release() {
        for (FrameResources& frame : frames_) {
            ReleaseFrame(frame);
        }
        ReleaseDistortionMask();
        distortionMaskRtvHeap_.Reset();
        distortionMaskRtvDescriptorSize_ = 0u;
        pipelineState_.Reset();
        rootSignature_.Reset();
        debugState_ = {};
        lastLoggedFailure_ = WeaponTrailRendererFailure::None;
        readyLogged_ = false;
    }

} // namespace HIKARI::RENDER3D::COMBAT_VFX
