// --- START OF FILE HIKARI_DxRenderer.cpp ---

#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>

#include <cassert>
#include <array>
#include <cstring>
#include <vector>
#include <cmath> // for max

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace DX {


        namespace {


            DynamicUploadBuffer g_uploadCB[GFX::kFrameResourceCount];
            DynamicUploadBuffer g_uploadVB[GFX::kFrameResourceCount];
            GFX::Context g_ctx{};
            uint32_t g_uploadFrameIndex = 0;

            float g_screenW =
                static_cast<float>(PRESENTATION::kDefaultLogicalWidth);
            float g_screenH =
                static_cast<float>(PRESENTATION::kDefaultLogicalHeight);

            struct PipelineSet {
                DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
                std::array<ComPtr<ID3D12PipelineState>,
                    static_cast<size_t>(BlendMode::Count)> colorPsos{};
                ComPtr<ID3D12PipelineState> linePso{};
                ComPtr<ID3D12PipelineState> maskPso{};
            };

            std::array<PipelineSet, 2> g_pipelineSets{};
            DXGI_FORMAT g_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

            PipelineSet* FindPipelineSet(DXGI_FORMAT format) {
                for (PipelineSet& set : g_pipelineSets) {
                    if (set.format == format) {
                        return &set;
                    }
                }
                return nullptr;
            }

            PipelineSet* GetActivePipelineSet() {
                return FindPipelineSet(g_outputFormat);
            }

            DynamicUploadBuffer& ActiveUploadCB()
            {
                return g_uploadCB[g_uploadFrameIndex % GFX::kFrameResourceCount];
            }

            DynamicUploadBuffer& ActiveUploadVB()
            {
                return g_uploadVB[g_uploadFrameIndex % GFX::kFrameResourceCount];
            }

            struct LineVertex {
                float px, py;
                float r, g, b, a;
            };

            // =========================================================
            // ConstantBuffer 格式
            // =========================================================

            struct GlobalCB
            {
                float screenSize[2];
                float pad[2];
            };

            // [新增] Mask 参数 CB
            struct MaskCB
            {
                float playerScreenPos[2]; // screenX, screenY
                float radius;
                float softness;
            };

            // 全局存?当前的 Mask 参数
            static MaskCB g_currentMaskParams = { {0.0f, 0.0f}, 0.0f, 0.0f };

            // =========================================================
            // Helper: RGBA → Float (0?1)
            // =========================================================

            static void DecodeRGBA(uint32_t rgba, float& r, float& g, float& b, float& a)
            {
                a = (float)((rgba >> 0) & 0xFF) / 255.0f;
                b = (float)((rgba >> 8) & 0xFF) / 255.0f;
                g = (float)((rgba >> 16) & 0xFF) / 255.0f;
                r = (float)((rgba >> 24) & 0xFF) / 255.0f;
            }

            static D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode)
            {
                // ... (保持原有的 MakeBlendDesc 代?不?) ...
                D3D12_BLEND_DESC desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                auto& rt = desc.RenderTarget[0];

                switch (mode) {
                case BlendMode::StraightAlpha:
                    rt.BlendEnable = TRUE;
                    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                case BlendMode::PremultipliedAlpha:
                    rt.BlendEnable = TRUE;
                    rt.SrcBlend = D3D12_BLEND_ONE;
                    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                case BlendMode::Additive:
                    rt.BlendEnable = TRUE;
                    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    rt.DestBlend = D3D12_BLEND_ONE;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_ONE;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                case BlendMode::Multiply:
                    rt.BlendEnable = TRUE;
                    rt.SrcBlend = D3D12_BLEND_DEST_COLOR;
                    rt.DestBlend = D3D12_BLEND_ZERO;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                case BlendMode::Screen:
                    rt.BlendEnable = TRUE;
                    rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
                    rt.DestBlend = D3D12_BLEND_ONE;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                case BlendMode::Opaque:
                default:
                    rt.BlendEnable = FALSE;
                    rt.SrcBlend = D3D12_BLEND_ONE;
                    rt.DestBlend = D3D12_BLEND_ZERO;
                    rt.BlendOp = D3D12_BLEND_OP_ADD;
                    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
                    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    break;
                }
                return desc;
            }


            // =========================================================
            // Shader（VS/PS）
            // =========================================================

            static const char* kMeshVS = R"(
cbuffer GlobalCB : register(b0)
{
    float2 screenSize;
    float2 pad;
}
struct VS_IN {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
VS_OUT main(VS_IN input)
{
    VS_OUT o;
    float2 ndc;
    ndc.x = (input.pos.x / screenSize.x) * 2.0f - 1.0f;
    ndc.y = 1.0f - (input.pos.y / screenSize.y) * 2.0f;
    o.pos = float4(ndc, 0.0f, 1.0f);
    o.uv = input.uv;
    o.col = input.col;
    return o;
}
)";

            static const char* kMeshPS = R"(
Texture2D tex0 : register(t0);
SamplerState smp : register(s0);
struct PS_IN {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
float4 main(PS_IN input) : SV_TARGET
{
    float4 texCol = tex0.Sample(smp, input.uv);
    return texCol * input.col;
}
)";

            static const char* kMaskPS = R"(
Texture2D tex0 : register(t0);
SamplerState smp : register(s0);

cbuffer MaskCB : register(b1)
{
    float2 playerPos; 
    float radius; 
    float softness; 
};

struct PS_IN {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

float4 main(PS_IN input) : SV_TARGET
{
    float4 texCol = tex0.Sample(smp, input.uv);
    float4 finalColor = texCol * input.col;

    // 1. ?算当前像素到玩家的距?
    float dist = distance(input.pos.xy, playerPos);

    // 2. ?算?? Alpha
    // 从 0.0 (中心) 到 radius (??) 生成 0.0 -> 1.0 的平滑?渡
    // ??中心就是完全透明的，越往外越不透明
    float alphaFactor = smoothstep(0.0f, radius, dist);

    finalColor.a *= alphaFactor;
    return finalColor;
}
)";

            static const char* kLineVS = R"(
cbuffer GlobalCB : register(b0)
{
    float2 screenSize;
    float2 pad;
}
struct VS_IN {
    float2 pos : POSITION;
    float4 col : COLOR0;
};
struct VS_OUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};
VS_OUT main(VS_IN input)
{
    VS_OUT o;
    float2 ndc;
    ndc.x = (input.pos.x / screenSize.x) * 2.0f - 1.0f;
    ndc.y = 1.0f - (input.pos.y / screenSize.y) * 2.0f;
    o.pos = float4(ndc, 0.0f, 1.0f);
    o.col = input.col;
    return o;
}
)";

            static const char* kLinePS = R"(
struct PS_IN { float4 pos : SV_POSITION; float4 col : COLOR0; };
float4 main(PS_IN input) : SV_TARGET { return input.col; }
)";


            // =========================================================
            // Helper - Shader Compile
            // =========================================================

            static ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* profile)
            {
                ComPtr<ID3DBlob> blob;
                if (!GFX::CompileShaderSourceSm6(
                    src,
                    std::strlen(src),
                    L"Render2D.InlineShader",
                    entry,
                    GFX::UpgradeToShaderModel6Profile(profile),
                    blob.GetAddressOf())) {
                    assert(false);
                }
                return blob;
            }


            // =========================================================
            // Init()
            // =========================================================

        } // anonymous namespace

        Microsoft::WRL::ComPtr<ID3D12RootSignature> DxRenderer::rootSig_;
        BlendMode DxRenderer::currentBlendMode_ = BlendMode::StraightAlpha;

        // [新增]
        Microsoft::WRL::ComPtr<ID3D12RootSignature> DxRenderer::rootSigMask_;

        void DxRenderer::Init(const GFX::Context& ctx)
        {
            g_ctx = ctx;
            g_screenW = (ctx.backBufferWidth > 0)
                ? static_cast<float>(ctx.backBufferWidth)
                : static_cast<float>(
                    PRESENTATION::kDefaultLogicalWidth);
            g_screenH = (ctx.backBufferHeight > 0)
                ? static_cast<float>(ctx.backBufferHeight)
                : static_cast<float>(
                    PRESENTATION::kDefaultLogicalHeight);


            auto* device = g_ctx.device;
            g_pipelineSets = {};
            g_pipelineSets[0].format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            g_pipelineSets[1].format = DXGI_FORMAT_R8G8B8A8_UNORM;
            g_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

            // === UploadBuffers ===
            for (uint32_t frameIndex = 0;
                frameIndex < GFX::kFrameResourceCount;
                ++frameIndex) {
                g_uploadCB[frameIndex].Init(
                    device,
                    static_cast<size_t>(1024 * 1024) * 8);
                g_uploadVB[frameIndex].Init(
                    device,
                    static_cast<size_t>(1024 * 1024) * 16);
            }

            // === RootSignature (Default) ===
            {
                CD3DX12_DESCRIPTOR_RANGE range{};
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

                CD3DX12_ROOT_PARAMETER params[2]{};
                params[0].InitAsConstantBufferView(0); // b0
                params[1].InitAsDescriptorTable(1, &range); // t0


                D3D12_STATIC_SAMPLER_DESC staticSampler = {};
                staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.MipLODBias = 0;
                staticSampler.MaxAnisotropy = 0;
                staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
                staticSampler.MinLOD = 0.0f;
                staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
                staticSampler.ShaderRegister = 0;   // s0!!
                staticSampler.RegisterSpace = 0;
                staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


                CD3DX12_ROOT_SIGNATURE_DESC desc;
                desc.Init(
                    _countof(params), params,
                    1, &staticSampler,
                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                );

                ComPtr<ID3DBlob> sigBlob, errBlob;
                HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
                if (FAILED(hr)) { if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer()); assert(false); }

                hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig_));
                assert(SUCCEEDED(hr));
            }

            // === [新增] RootSignature (Mask) ===
            {
                // 需要三个参数：b0 (Global), t0 (Texture), b1 (MaskParams)
                CD3DX12_DESCRIPTOR_RANGE range{};
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

                CD3DX12_ROOT_PARAMETER params[3]{};
                params[0].InitAsConstantBufferView(0);      // b0: ScreenSize
                params[1].InitAsDescriptorTable(1, &range); // t0: Texture
                params[2].InitAsConstantBufferView(1);      // b1: MaskParams (新增)

                D3D12_STATIC_SAMPLER_DESC staticSampler = {};
                staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                staticSampler.MipLODBias = 0;
                staticSampler.MaxAnisotropy = 0;
                staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
                staticSampler.MinLOD = 0.0f;
                staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
                staticSampler.ShaderRegister = 0;
                staticSampler.RegisterSpace = 0;
                staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                CD3DX12_ROOT_SIGNATURE_DESC desc;
                desc.Init(_countof(params), params, 1, &staticSampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

                ComPtr<ID3DBlob> sigBlob, errBlob;
                HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
                if (FAILED(hr)) { if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer()); assert(false); }

                hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSigMask_));
                assert(SUCCEEDED(hr));
            }

            // === PSO: Mesh / Sprite ===
            {
                ComPtr<ID3DBlob> vs = CompileShader(kMeshVS, "main", "vs_6_0");
                ComPtr<ID3DBlob> ps = CompileShader(kMeshPS, "main", "ps_6_0");

                D3D12_INPUT_ELEMENT_DESC elems[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };

                auto FillBasePSO = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
                    {
                        ZeroMemory(&desc, sizeof(desc));
                        desc.pRootSignature = rootSig_.Get();
                        desc.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
                        desc.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
                        desc.InputLayout = { elems, _countof(elems) };
                        desc.SampleMask = UINT_MAX;
                        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
                        desc.DepthStencilState.DepthEnable = FALSE;
                        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                        desc.NumRenderTargets = 1;
                        desc.SampleDesc.Count = 1;
                    };
                D3D12_GRAPHICS_PIPELINE_STATE_DESC baseDesc{};
                FillBasePSO(baseDesc);

                for (PipelineSet& set : g_pipelineSets) {
                    for (size_t i = 0;
                        i < static_cast<size_t>(BlendMode::Count);
                        ++i) {

                        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = baseDesc;
                        desc.RTVFormats[0] = set.format;
                        desc.BlendState = MakeBlendDesc(static_cast<BlendMode>(i));
                        const HRESULT hr = device->CreateGraphicsPipelineState(
                            &desc,
                            IID_PPV_ARGS(set.colorPsos[i].GetAddressOf()));
                        assert(SUCCEEDED(hr));
                    }
                }

                // === [新增] PSO: Mask ===
                // ?? Mask PS
                ComPtr<ID3DBlob> psMask = CompileShader(kMaskPS, "main", "ps_6_0");

                D3D12_GRAPHICS_PIPELINE_STATE_DESC maskDesc = baseDesc;
                maskDesc.pRootSignature = rootSigMask_.Get(); // 使用新的 RS
                maskDesc.PS = CD3DX12_SHADER_BYTECODE(psMask.Get()); // 使用 Mask PS
                maskDesc.BlendState = MakeBlendDesc(BlendMode::StraightAlpha); // 使用?准透明混合

                for (PipelineSet& set : g_pipelineSets) {
                    maskDesc.RTVFormats[0] = set.format;
                    const HRESULT hr = device->CreateGraphicsPipelineState(
                        &maskDesc,
                        IID_PPV_ARGS(set.maskPso.GetAddressOf()));
                    assert(SUCCEEDED(hr));
                }
            }

            // === PSO: Line (保持不?) ===
            {
                ComPtr<ID3DBlob> vs = CompileShader(kLineVS, "main", "vs_6_0");
                ComPtr<ID3DBlob> ps = CompileShader(kLinePS, "main", "ps_6_0");

                D3D12_INPUT_ELEMENT_DESC elems[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };

                D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
                d.pRootSignature = rootSig_.Get();
                d.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
                d.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
                d.InputLayout = { elems, _countof(elems) };
                d.SampleMask = UINT_MAX;
                d.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                d.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
                d.DepthStencilState.DepthEnable = FALSE;
                d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                d.NumRenderTargets = 1;
                d.SampleDesc.Count = 1;

                d.BlendState = MakeBlendDesc(BlendMode::StraightAlpha);

                for (PipelineSet& set : g_pipelineSets) {
                    d.RTVFormats[0] = set.format;
                    const HRESULT hr = device->CreateGraphicsPipelineState(
                        &d,
                        IID_PPV_ARGS(set.linePso.GetAddressOf()));
                    assert(SUCCEEDED(hr));
                }
            }

            OutputDebugStringA("[DxRenderer] Init OK.\n");
        }



        void DxRenderer::UpdateContext(const GFX::Context& ctx)
        {
            g_ctx = ctx;
            SetScreenSize(ctx.backBufferWidth, ctx.backBufferHeight);
        }

        void DxRenderer::SetScreenSize(int width, int height)
        {
            if (width <= 0 || height <= 0) {
                return;
            }

            g_screenW = static_cast<float>(width);
            g_screenH = static_cast<float>(height);
        }

        bool DxRenderer::SetOutputTarget(
            DXGI_FORMAT format,
            int width,
            int height) {

            PipelineSet* pipelines = FindPipelineSet(format);
            if (pipelines == nullptr ||
                pipelines->colorPsos[0] == nullptr ||
                pipelines->linePso == nullptr ||
                pipelines->maskPso == nullptr ||
                width <= 0 ||
                height <= 0) {
                return false;
            }

            g_outputFormat = format;
            SetScreenSize(width, height);
            return true;
        }

        DXGI_FORMAT DxRenderer::GetOutputFormat() {
            return g_outputFormat;
        }

        void DxRenderer::Finalize()
        {
            for (uint32_t frameIndex = 0;
                frameIndex < GFX::kFrameResourceCount;
                ++frameIndex) {
                g_uploadCB[frameIndex].Finalize();
                g_uploadVB[frameIndex].Finalize();
            }
            g_pipelineSets = {};
            g_outputFormat = DXGI_FORMAT_UNKNOWN;
        }


        void DxRenderer::BeginFrame()
        {
            g_uploadFrameIndex = g_ctx.frameIndex % GFX::kFrameResourceCount;
            ActiveUploadCB().Reset();
            ActiveUploadVB().Reset();
        }

        void DxRenderer::EndFrame()
        {
            // 目前是即??制，所以?里不做任何事
        }


        void DxRenderer::SetBlendMode(BlendMode mode)
        {
            if (mode >= BlendMode::Count) {
                currentBlendMode_ = BlendMode::StraightAlpha;
                return;
            }

            currentBlendMode_ = mode;
        }

        void DxRenderer::DrawMesh(
            const MeshVertex* verts,
            int vertexCount,
            D3D_PRIMITIVE_TOPOLOGY topology,
            int texHandle,
            uint32_t color)
        {
            if (!verts || vertexCount <= 0 || texHandle < 0) {
                return;
            }

            auto* cmd = g_ctx.cmdList;

            // ? PSO
            size_t blendIdx = static_cast<size_t>(currentBlendMode_);
            if (blendIdx >= static_cast<size_t>(BlendMode::Count)) {
                blendIdx = 0;
            }

            PipelineSet* pipelines = GetActivePipelineSet();
            if (pipelines == nullptr) {
                return;
            }

            ID3D12PipelineState* pso = pipelines->colorPsos[blendIdx].Get();
            if (!pso) {
                pso = pipelines->colorPsos[
                    static_cast<size_t>(BlendMode::StraightAlpha)].Get();
            }

            cmd->SetGraphicsRootSignature(rootSig_.Get());
            cmd->SetPipelineState(pso);

            // ?色 tint
            float r, g, b, a;
            DecodeRGBA(color, r, g, b, a);

            // Upload VB
            D3D12_GPU_VIRTUAL_ADDRESS gpuVB;
            MeshVertex* vb = (MeshVertex*)ActiveUploadVB().Allocate(
                sizeof(MeshVertex) * vertexCount, gpuVB);

            for (int i = 0; i < vertexCount; ++i) {
                vb[i] = verts[i];
                vb[i].r = r;
                vb[i].g = g;
                vb[i].b = b;
                vb[i].a = a;
            }

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = gpuVB;
            vbv.StrideInBytes = sizeof(MeshVertex);
            vbv.SizeInBytes = sizeof(MeshVertex) * vertexCount;

            // Upload CB（屏幕大小）
            GlobalCB cb{};
            cb.screenSize[0] = g_screenW;
            cb.screenSize[1] = g_screenH;

            D3D12_GPU_VIRTUAL_ADDRESS gpuCB;

            // 【修改前】 memcpy(g_uploadCB.Allocate(sizeof(cb), gpuCB), &cb, sizeof(cb));

            // 【修改后】 ?制 256 字???
            UINT cbSizeAligned = (sizeof(cb) + 255) & ~255;
            memcpy(ActiveUploadCB().Allocate(cbSizeAligned, gpuCB), &cb, sizeof(cb));

            cmd->SetGraphicsRootConstantBufferView(0, gpuCB);


            // ?理 SRV
            ID3D12DescriptorHeap* heap =
                HIKARI::DXTEX::DxTextureManager::GetSrvHeap();
            cmd->SetDescriptorHeaps(1, &heap);

            auto gpuHandle =
                HIKARI::DXTEX::DxTextureManager::GetSrvGpuHandle(texHandle);
            cmd->SetGraphicsRootDescriptorTable(1, gpuHandle);

            // IA & Draw
            cmd->IASetPrimitiveTopology(topology);
            cmd->IASetVertexBuffers(0, 1, &vbv);
            cmd->DrawInstanced(vertexCount, 1, 0, 0);
        }
        // =========================================================
        // DrawMeshQuad
        // =========================================================

        void DxRenderer::DrawMeshQuad(
            float x0, float y0, float u0, float v0,
            float x1, float y1, float u1, float v1,
            float x2, float y2, float u2, float v2,
            float x3, float y3, float u3, float v3,
            int texHandle,
            uint32_t color)
        {
            MeshVertex verts[4] = {
                { x0, y0, u0, v0, 0,0,0,0 },
                { x1, y1, u1, v1, 0,0,0,0 },
                { x2, y2, u2, v2, 0,0,0,0 },
                { x3, y3, u3, v3, 0,0,0,0 },
            };

            DrawMesh(
                verts,
                4,
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
                texHandle,
                color);
        }

        // =========================================================
        // [新增] Mask 参数更新与?制
        // =========================================================

        void DxRenderer::UpdateMaskParams(float screenX, float screenY, float radius, float softness)
        {
            g_currentMaskParams.playerScreenPos[0] = screenX;
            g_currentMaskParams.playerScreenPos[1] = screenY;
            g_currentMaskParams.radius = radius;
            g_currentMaskParams.softness = softness;
        }

        void DxRenderer::DrawMeshQuadMasked(
            float x0, float y0, float u0, float v0,
            float x1, float y1, float u1, float v1,
            float x2, float y2, float u2, float v2,
            float x3, float y3, float u3, float v3,
            int texHandle,
            uint32_t color)
        {
            // ?造?点数据
            MeshVertex verts[4] = {
                { x0, y0, u0, v0, 0,0,0,0 },
                { x1, y1, u1, v1, 0,0,0,0 },
                { x2, y2, u2, v2, 0,0,0,0 },
                { x3, y3, u3, v3, 0,0,0,0 },
            };
            int vertexCount = 4;

            auto* cmd = g_ctx.cmdList;

            // 1. 切?到 Mask ?用的 RootSig 和 PSO
            cmd->SetGraphicsRootSignature(rootSigMask_.Get());
            PipelineSet* pipelines = GetActivePipelineSet();
            if (pipelines == nullptr || pipelines->maskPso == nullptr) {
                return;
            }
            cmd->SetPipelineState(pipelines->maskPso.Get());

            // ?色解析
            float r, g, b, a;
            DecodeRGBA(color, r, g, b, a);

            // 2. Upload Vertex Buffer (和普通?制一?)
            D3D12_GPU_VIRTUAL_ADDRESS gpuVB;
            MeshVertex* vb = (MeshVertex*)ActiveUploadVB().Allocate(
                sizeof(MeshVertex) * vertexCount, gpuVB);

            for (int i = 0; i < vertexCount; ++i) {
                vb[i] = verts[i];
                vb[i].r = r; vb[i].g = g; vb[i].b = b; vb[i].a = a;
            }

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = gpuVB;
            vbv.StrideInBytes = sizeof(MeshVertex);
            vbv.SizeInBytes = sizeof(MeshVertex) * vertexCount;

            // 3. Upload GlobalCB (b0)
            GlobalCB cb{};
            cb.screenSize[0] = g_screenW;
            cb.screenSize[1] = g_screenH;
            D3D12_GPU_VIRTUAL_ADDRESS gpuCB;

            // =============== 修改 GlobalCB ===============
            UINT globalSizeAligned = (sizeof(GlobalCB) + 255) & ~255;
            memcpy(ActiveUploadCB().Allocate(globalSizeAligned, gpuCB), &cb, sizeof(cb));
            cmd->SetGraphicsRootConstantBufferView(0, gpuCB);

            // 4. Upload MaskCB (b1)
            D3D12_GPU_VIRTUAL_ADDRESS gpuMaskCB;

            // =============== 修改 MaskCB ===============
            UINT maskSizeAligned = (sizeof(MaskCB) + 255) & ~255;
            memcpy(ActiveUploadCB().Allocate(maskSizeAligned, gpuMaskCB), &g_currentMaskParams, sizeof(MaskCB));
            cmd->SetGraphicsRootConstantBufferView(2, gpuMaskCB);

            // 5. ?定?理 SRV (t0)
            ID3D12DescriptorHeap* heap = HIKARI::DXTEX::DxTextureManager::GetSrvHeap();
            cmd->SetDescriptorHeaps(1, &heap);
            auto gpuHandle = HIKARI::DXTEX::DxTextureManager::GetSrvGpuHandle(texHandle);
            cmd->SetGraphicsRootDescriptorTable(1, gpuHandle); // RootParam[1]

            // 6. ?制
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            cmd->IASetVertexBuffers(0, 1, &vbv);
            cmd->DrawInstanced(vertexCount, 1, 0, 0);
        }

        // =========================================================
        // DrawSprite
        // =========================================================

        void DxRenderer::DrawSprite(
            int texHandle,
            float x, float y,
            float w, float h,
            uint32_t color)
        {
            float x0 = x;
            float y0 = y;
            float x1 = x + w;
            float y1 = y + h;

            DrawMeshQuad(
                x0, y0, 0.0f, 0.0f,
                x1, y0, 1.0f, 0.0f,
                x0, y1, 0.0f, 1.0f,
                x1, y1, 1.0f, 1.0f,
                texHandle, color);
        }


        void DxRenderer::DrawSpriteUV(
            int texHandle,
            float x, float y,
            float w, float h,
            float u0, float v0,
            float u1, float v1,
            uint32_t color)
        {
            float x0 = x;
            float y0 = y;
            float x1 = x + w;
            float y1 = y + h;

            DrawMeshQuad(
                x0, y0, u0, v0,
                x1, y0, u1, v0,
                x0, y1, u0, v1,
                x1, y1, u1, v1,
                texHandle, color);
        }


        // =========================================================
        // DrawLine
        // =========================================================

        void DxRenderer::DrawLine(
            float x0, float y0,
            float x1, float y1,
            uint32_t color)
        {
            auto* cmd = g_ctx.cmdList;

            cmd->SetGraphicsRootSignature(rootSig_.Get());
            PipelineSet* pipelines = GetActivePipelineSet();
            if (pipelines == nullptr || pipelines->linePso == nullptr) {
                return;
            }
            cmd->SetPipelineState(pipelines->linePso.Get());

            float r, g, b, a;
            DecodeRGBA(color, r, g, b, a);

            LineVertex verts[2] = {
                { x0, y0, r, g, b, a },
                { x1, y1, r, g, b, a }
            };

            D3D12_GPU_VIRTUAL_ADDRESS gpuVB;
            LineVertex* vb = (LineVertex*)ActiveUploadVB().Allocate(sizeof(verts), gpuVB);
            memcpy(vb, verts, sizeof(verts));

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = gpuVB;
            vbv.StrideInBytes = sizeof(LineVertex);
            vbv.SizeInBytes = sizeof(verts);

            GlobalCB cb{};
            cb.screenSize[0] = g_screenW;
            cb.screenSize[1] = g_screenH;
            D3D12_GPU_VIRTUAL_ADDRESS gpuCB;
            const UINT cbSizeAligned = (sizeof(cb) + 255) & ~255u;
            memcpy(ActiveUploadCB().Allocate(cbSizeAligned, gpuCB), &cb, sizeof(cb));

            cmd->SetGraphicsRootConstantBufferView(0, gpuCB);

            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            cmd->IASetVertexBuffers(0, 1, &vbv);

            cmd->DrawInstanced(2, 1, 0, 0);
        }

    } // namespace DX
} // namespace HIKARI
