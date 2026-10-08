#include "Render3D/ScreenSpace/HIKARI_SubsurfaceScatteringRenderer.h"

#include <algorithm>
#include <cstring>

#include <d3dcompiler.h>
#include <d3dx12.h>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::SCREENSPACE {
namespace {

struct SubsurfacePassCB {
    MATH::Mat4 invViewProj{};
    MATH::Vec4 screenParams{};
    MATH::Vec4 blurParams{};
    MATH::Vec4 channelFalloff{};
};

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr size_t kUploadBytes = 4096u;
constexpr const wchar_t* kShaderPath = L"HIKARI/Shaders/Post_SubsurfaceScatterPS.hlsl";

bool CompileShader(const char* entry, const wchar_t* target, ID3DBlob** outBlob) {
    if (!GFX::CompileShaderFileSm6(kShaderPath, entry, target, outBlob)) {
        DEBUGLOG::PushRenderError("[SubsurfaceScattering][ERROR] SM6 shader compile failed.");
        return false;
    }
    return true;
}

} // namespace

bool SubsurfaceScatteringRenderer::Render(const GFX::Context& graphics,
                                          const PIPELINE::RenderTargetAccess& targetAccess,
                                          const RenderTextureView& subsurfaceDiffuse,
                                          const RenderTextureView& sceneDepth, const MESHRENDERER::CameraCB& camera,
                                          const SubsurfaceScatteringSettings& settings) {
    debugState_ = {};
    debugState_.requested = settings.enabled;
    debugState_.width = subsurfaceDiffuse.width;
    debugState_.height = subsurfaceDiffuse.height;
    auto* commands = graphics.cmdList;
    if (!settings.enabled || commands == nullptr || graphics.device == nullptr || graphics.srvHeap == nullptr ||
        !subsurfaceDiffuse.IsSampleable() || !sceneDepth.IsSampleable() || settings.strength <= 0.0f) {
        return false;
    }
    if (!EnsurePipeline(graphics) || !EnsureTarget(graphics, subsurfaceDiffuse.width, subsurfaceDiffuse.height)) {
        return false;
    }
    auto& upload = uploads_[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!upload.IsInitialized() && !upload.Init(graphics.device, kUploadBytes)) {
        return false;
    }
    upload.ResetForFrame(graphics.currentFrameRetireFenceValue);

    SubsurfacePassCB constants{};
    constants.invViewProj = camera.invViewProj;
    constants.screenParams = {static_cast<float>(width_), static_cast<float>(height_), 1.0f / float(width_),
                              1.0f / float(height_)};
    constants.channelFalloff = {settings.channelFalloff.x, settings.channelFalloff.y, settings.channelFalloff.z, 0.0f};
    const float maxPixelRadius = std::clamp(settings.maxPixelRadius, 1.0f, 256.0f);
    constants.blurParams = {1.0f, 0.0f, settings.strength, maxPixelRadius};
    const auto horizontalSlice = upload.Allocate(sizeof(constants));
    if (!horizontalSlice) {
        return false;
    }
    std::memcpy(horizontalSlice.cpuAddress, &constants, sizeof(constants));
    constants.blurParams = {0.0f, 1.0f, settings.strength, maxPixelRadius};
    const auto verticalSlice = upload.Allocate(sizeof(constants));
    if (!verticalSlice) {
        return false;
    }
    std::memcpy(verticalSlice.cpuAddress, &constants, sizeof(constants));

    // 深度は両 pass で読む。BeginDepthRead が場面色 + 読み取り専用深度を束ねるので、水平 pass の後にもう一度束ね直す。
    if (!targetAccess.BeginDepthRead()) {
        return false;
    }
    GFX::PIX::ScopedGpuEvent event(commands, GFX::PIX::kColorPost, "SubsurfaceScattering");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(commands, GFX::GPU_PROFILE::Pass::SubsurfaceScattering);
    ID3D12DescriptorHeap* heaps[]{graphics.srvHeap};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootSignature(rootSignature_.Get());
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, float(width_), float(height_), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, LONG(width_), LONG(height_)};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->IASetVertexBuffers(0, 0, nullptr);
    commands->IASetIndexBuffer(nullptr);

    Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->OMSetRenderTargets(1, &horizontalRtv_, FALSE, nullptr);
    commands->SetGraphicsRootConstantBufferView(0, horizontalSlice.gpuAddress);
    commands->SetGraphicsRootDescriptorTable(1, subsurfaceDiffuse.srv);
    commands->SetGraphicsRootDescriptorTable(2, sceneDepth.srv);
    commands->SetGraphicsRootDescriptorTable(3, subsurfaceDiffuse.srv);
    commands->SetPipelineState(horizontalPso_.Get());
    commands->DrawInstanced(3, 1, 0, 0);
    Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (!targetAccess.BeginDepthRead()) {
        targetAccess.EndDepthRead();
        return false;
    }
    commands->SetGraphicsRootConstantBufferView(0, verticalSlice.gpuAddress);
    commands->SetGraphicsRootDescriptorTable(1, horizontalSrv_.gpu);
    commands->SetGraphicsRootDescriptorTable(2, sceneDepth.srv);
    commands->SetGraphicsRootDescriptorTable(3, subsurfaceDiffuse.srv);
    commands->SetPipelineState(compositePso_.Get());
    commands->DrawInstanced(3, 1, 0, 0);
    targetAccess.EndDepthRead();
    debugState_.rendered = true;
    return true;
}

bool SubsurfaceScatteringRenderer::EnsurePipeline(const GFX::Context& graphics) {
    if (rootSignature_ && horizontalPso_ && compositePso_ && deviceGeneration_ == graphics.deviceGeneration) {
        return true;
    }
    ID3D12Device* device = graphics.device;
    rootSignature_.Reset();
    horizontalPso_.Reset();
    compositePso_.Reset();
    if (device == nullptr || !GFX::SupportsShaderModel6(device)) {
        return false;
    }

    D3D12_STATIC_SAMPLER_DESC pointSampler{};
    pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    pointSampler.ShaderRegister = 0;
    pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC linearSampler = pointSampler;
    linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linearSampler.ShaderRegister = 1;
    const D3D12_STATIC_SAMPLER_DESC samplers[]{pointSampler, linearSampler};

    constexpr UINT kTextureCount = 3u;
    std::array<D3D12_DESCRIPTOR_RANGE, kTextureCount> ranges{};
    std::array<D3D12_ROOT_PARAMETER, kTextureCount + 1u> params{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0;
    for (UINT i = 0; i < kTextureCount; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
    rootDesc.pStaticSamplers = samplers;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (!HIKARI_DX_CHECK(GFX::CreateRootSignature(device, rootDesc, rootSignature_.GetAddressOf()),
                         "SubsurfaceScattering root signature")) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> vs;
    Microsoft::WRL::ComPtr<ID3DBlob> horizontalPs;
    Microsoft::WRL::ComPtr<ID3DBlob> compositePs;
    if (!CompileShader("VSMain", L"vs_6_0", vs.GetAddressOf()) ||
        !CompileShader("PSBlurHorizontal", L"ps_6_0", horizontalPs.GetAddressOf()) ||
        !CompileShader("PSBlurVerticalComposite", L"ps_6_0", compositePs.GetAddressOf())) {
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {horizontalPs->GetBufferPointer(), horizontalPs->GetBufferSize()};
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.InputLayout = {nullptr, 0};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kFormat;
    desc.SampleDesc.Count = 1;
    if (!HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(horizontalPso_.GetAddressOf())),
                         "SubsurfaceScattering horizontal PSO")) {
        return false;
    }
    // 合成は場面色（HDR, 深度バッファ付き）へ差分を加算する。アルファは触らない。
    desc.PS = {compositePs->GetBufferPointer(), compositePs->GetBufferSize()};
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    auto& blend = desc.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ONE;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (!HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(compositePso_.GetAddressOf())),
                         "SubsurfaceScattering composite PSO")) {
        return false;
    }
    deviceGeneration_ = graphics.deviceGeneration;
    return true;
}

bool SubsurfaceScatteringRenderer::EnsureTarget(const GFX::Context& graphics, uint32_t width, uint32_t height) {
    if (horizontal_ && width_ == width && height_ == height && horizontalSrv_.IsValid()) {
        return true;
    }
    ReleaseTarget(graphics);
    ID3D12Device* device = graphics.device;
    const D3D12_CLEAR_VALUE clear{kFormat, {0.0f, 0.0f, 0.0f, 0.0f}};
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc =
        CD3DX12_RESOURCE_DESC::Tex2D(kFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!HIKARI_DX_CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state_, &clear,
                                                         IID_PPV_ARGS(horizontal_.GetAddressOf())),
                         "SubsurfaceScattering horizontal target")) {
        return false;
    }
    horizontal_->SetName(L"HIKARI.SubsurfaceScattering.Horizontal");
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (!HIKARI_DX_CHECK(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf())),
                         "SubsurfaceScattering RTV heap")) {
        return false;
    }
    horizontalRtv_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(horizontal_.Get(), nullptr, horizontalRtv_);
    horizontalSrv_ = AllocateTexture2DSrvDescriptor(horizontal_.Get(), kFormat);
    if (!horizontalSrv_.IsValid()) {
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void SubsurfaceScatteringRenderer::ReleaseTarget(const GFX::Context& graphics) {
    if (horizontalSrv_.IsValid()) {
        RetireRenderResourceDescriptor(horizontalSrv_, graphics, "SubsurfaceScattering.HorizontalSRV");
    }
    if (horizontal_) {
        GFX::RetireD3D12ObjectForFrame(horizontal_, graphics, "SubsurfaceScattering.Horizontal");
    }
    if (rtvHeap_) {
        GFX::RetireD3D12ObjectForFrame(rtvHeap_, graphics, "SubsurfaceScattering.RTVHeap");
    }
    horizontalRtv_ = {};
    width_ = 0u;
    height_ = 0u;
}

void SubsurfaceScatteringRenderer::Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES next) {
    if (state_ == next || !horizontal_) {
        state_ = next;
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(horizontal_.Get(), state_, next);
    commands->ResourceBarrier(1, &barrier);
    state_ = next;
}

void SubsurfaceScatteringRenderer::Release() {
    ReleaseTarget(GFX::GetCurrentContext());
    for (auto& upload : uploads_) {
        upload.Finalize();
    }
    rootSignature_.Reset();
    horizontalPso_.Reset();
    compositePso_.Reset();
    deviceGeneration_ = 0u;
    debugState_ = {};
}

} // namespace HIKARI::RENDER3D::SCREENSPACE
