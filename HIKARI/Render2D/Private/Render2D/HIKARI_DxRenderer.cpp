#include "Render2D/HIKARI_DxRenderer.h"

#include <d3dx12.h>

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_Render2DBuiltinShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI::DX {
namespace {

struct MeshVertex {
    float px, py;
    float u, v;
    float r, g, b, a;
};

struct GlobalCB {
    float screenSize[2];
    float colorScale = 1.0f;
    float pad = 0.0f;
};

// 出力形式ごとの PSO（ストレートアルファ合成のみ使用）
struct PipelineSet {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12PipelineState> pso{};
};

DynamicUploadBuffer g_uploadCB[GFX::kFrameResourceCount];
DynamicUploadBuffer g_uploadVB[GFX::kFrameResourceCount];
GFX::Context g_ctx{};
uint32_t g_uploadFrameIndex = 0;
float g_screenW = static_cast<float>(PRESENTATION::kDefaultLogicalWidth);
float g_screenH = static_cast<float>(PRESENTATION::kDefaultLogicalHeight);
ComPtr<ID3D12RootSignature> g_rootSig;
std::array<PipelineSet, 2> g_pipelineSets{};
DXGI_FORMAT g_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
float g_colorScale = 1.0f;

PipelineSet* FindPipelineSet(DXGI_FORMAT format) {
    for (PipelineSet& set : g_pipelineSets) {
        if (set.format == format) {
            return &set;
        }
    }
    return nullptr;
}

DynamicUploadBuffer& ActiveUploadCB() { return g_uploadCB[g_uploadFrameIndex % GFX::kFrameResourceCount]; }
DynamicUploadBuffer& ActiveUploadVB() { return g_uploadVB[g_uploadFrameIndex % GFX::kFrameResourceCount]; }

D3D12_BLEND_DESC MakeStraightAlphaBlend() {
    D3D12_BLEND_DESC desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& rt = desc.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return desc;
}

bool ResolveEntry(const ASSETS::SHADER::ShaderAssetContractExpectation& contract, const char* entryId,
                  ASSETS::SHADER::ShaderStage stage, const char* profile,
                  ASSETS::SHADER::ShaderAssetEntryLease& outLease) {
    ASSETS::SHADER::ShaderAssetResolveResult resolved = ASSETS::SHADER::ResolveBuiltinShaderEntry(
        "HIKARI.EngineShader.Render2D_Builtin", {entryId, stage, profile}, contract);
    if (!resolved || resolved.lease.GetDxil().empty()) {
        HIKARI_LOG_ERROR(std::string("[DxRenderer] cooked Shader entry ") + entryId +
                         " is unavailable: " + resolved.diagnostic);
        return false;
    }
    outLease = std::move(resolved.lease);
    return true;
}

D3D12_SHADER_BYTECODE ShaderBytecode(const ASSETS::SHADER::ShaderAssetEntryLease& lease) {
    const auto bytes = lease.GetDxil();
    return {bytes.data(), bytes.size()};
}

bool CreateRootSignature(ID3D12Device* device) {
    // b1 はシェーダー契約上のマスク用スロット。レイアウト互換のため維持する。
    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsDescriptorTable(1, &range);
    params[2].InitAsConstantBufferView(1);

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, desc, &g_rootSig), "DxRenderer::CreateRootSignature");
}

} // namespace

bool DxRenderer::Init(const GFX::Context& ctx) {
    g_ctx = ctx;
    g_screenW = static_cast<float>(ctx.backBufferWidth > 0 ? ctx.backBufferWidth : PRESENTATION::kDefaultLogicalWidth);
    g_screenH =
        static_cast<float>(ctx.backBufferHeight > 0 ? ctx.backBufferHeight : PRESENTATION::kDefaultLogicalHeight);
    ID3D12Device* device = g_ctx.device;
    g_pipelineSets = {};
    g_rootSig.Reset();
    g_pipelineSets[0].format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    g_pipelineSets[1].format = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (device == nullptr) {
        HIKARI_LOG_ERROR("[DxRenderer] D3D12 device is unavailable");
        return false;
    }

    bool uploadBuffersReady = true;
    for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        uploadBuffersReady &= g_uploadCB[frameIndex].Init(device, static_cast<size_t>(1024 * 1024) * 8);
        uploadBuffersReady &= g_uploadVB[frameIndex].Init(device, static_cast<size_t>(1024 * 1024) * 16);
    }
    if (!uploadBuffersReady) {
        HIKARI_LOG_ERROR("[DxRenderer] dynamic upload page initialization failed");
        return false;
    }

    const GFX::ShaderDeviceCapabilities caps = GFX::QueryShaderDeviceCapabilities(device);
    const auto contract = ASSETS::SHADER::MakeRender2DBuiltinShaderContract(
        {caps.shaderModel, caps.rootSignatureVersion, caps.featureMask});
    ASSETS::SHADER::ShaderAssetEntryLease meshVs{};
    ASSETS::SHADER::ShaderAssetEntryLease meshPs{};
    if (!ResolveEntry(contract, "MeshVS", ASSETS::SHADER::ShaderStage::Vertex, "vs_6_0", meshVs) ||
        !ResolveEntry(contract, "MeshPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0", meshPs)) {
        return false;
    }
    std::string shaderDiagnostic{};
    if (!ASSETS::SHADER::ValidateRender2DBuiltinShaderArtifact(*meshVs.GetArtifact(), shaderDiagnostic)) {
        HIKARI_LOG_ERROR(std::string("[DxRenderer] cooked Shader contract failed: ") + shaderDiagnostic);
        return false;
    }
    if (!CreateRootSignature(device)) {
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = g_rootSig.Get();
    desc.VS = ShaderBytecode(meshVs);
    desc.PS = ShaderBytecode(meshPs);
    desc.InputLayout = {elems, _countof(elems)};
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.BlendState = MakeStraightAlphaBlend();
    for (PipelineSet& set : g_pipelineSets) {
        desc.RTVFormats[0] = set.format;
        if (!HIKARI_DX_CHECK(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(set.pso.GetAddressOf())),
                             "DxRenderer::CreateColorPipeline")) {
            g_pipelineSets = {};
            g_rootSig.Reset();
            return false;
        }
    }
    return true;
}

void DxRenderer::UpdateContext(const GFX::Context& ctx) {
    g_ctx = ctx;
    SetScreenSize(ctx.backBufferWidth, ctx.backBufferHeight);
}

void DxRenderer::SetScreenSize(int width, int height) {
    if (width > 0 && height > 0) {
        g_screenW = static_cast<float>(width);
        g_screenH = static_cast<float>(height);
    }
}

bool DxRenderer::SetOutputTarget(DXGI_FORMAT format, int width, int height, float colorScale) {
    const PipelineSet* pipelines = FindPipelineSet(format);
    if (pipelines == nullptr || pipelines->pso == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    g_outputFormat = format;
    g_colorScale = std::isfinite(colorScale) && colorScale > 0.0f ? colorScale : 1.0f;
    SetScreenSize(width, height);
    return true;
}

void DxRenderer::Finalize() {
    for (uint32_t frameIndex = 0; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        g_uploadCB[frameIndex].Finalize();
        g_uploadVB[frameIndex].Finalize();
    }
    g_pipelineSets = {};
    g_outputFormat = DXGI_FORMAT_UNKNOWN;
    g_colorScale = 1.0f;
    g_rootSig.Reset();
}

void DxRenderer::BeginFrame() {
    g_uploadFrameIndex = g_ctx.frameIndex % GFX::kFrameResourceCount;
    ActiveUploadCB().Reset();
    ActiveUploadVB().Reset();
}

void DxRenderer::DrawMeshQuad(float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2,
                              float y2, float u2, float v2, float x3, float y3, float u3, float v3, int texHandle,
                              uint32_t color) {
    const PipelineSet* pipelines = FindPipelineSet(g_outputFormat);
    if (texHandle < 0 || pipelines == nullptr || pipelines->pso == nullptr || g_rootSig == nullptr) {
        return;
    }
    // RGBA8 パックのティント（上位バイトが R）
    const float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    const float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    const float b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    const float a = static_cast<float>(color & 0xFF) / 255.0f;
    const MeshVertex verts[4] = {
        {x0, y0, u0, v0, r, g, b, a},
        {x1, y1, u1, v1, r, g, b, a},
        {x2, y2, u2, v2, r, g, b, a},
        {x3, y3, u3, v3, r, g, b, a},
    };

    D3D12_GPU_VIRTUAL_ADDRESS gpuVB = 0;
    void* vb = ActiveUploadVB().Allocate(sizeof(verts), gpuVB);
    D3D12_GPU_VIRTUAL_ADDRESS gpuCB = 0;
    void* cb = vb != nullptr ? ActiveUploadCB().Allocate((sizeof(GlobalCB) + 255) & ~255u, gpuCB) : nullptr;
    if (cb == nullptr) {
        return;
    }
    std::memcpy(vb, verts, sizeof(verts));
    const GlobalCB global{{g_screenW, g_screenH}, g_colorScale};
    std::memcpy(cb, &global, sizeof(global));

    ID3D12GraphicsCommandList* cmd = g_ctx.cmdList;
    cmd->SetGraphicsRootSignature(g_rootSig.Get());
    cmd->SetPipelineState(pipelines->pso.Get());
    cmd->SetGraphicsRootConstantBufferView(0, gpuCB);
    ID3D12DescriptorHeap* heap = DXTEX::DxTextureManager::GetSrvHeap();
    cmd->SetDescriptorHeaps(1, &heap);
    cmd->SetGraphicsRootDescriptorTable(1, DXTEX::DxTextureManager::GetSrvGpuHandle(texHandle));
    const D3D12_VERTEX_BUFFER_VIEW vbv{gpuVB, static_cast<UINT>(sizeof(verts)), sizeof(MeshVertex)};
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmd->IASetVertexBuffers(0, 1, &vbv);
    cmd->DrawInstanced(4, 1, 0, 0);
}

} // namespace HIKARI::DX
