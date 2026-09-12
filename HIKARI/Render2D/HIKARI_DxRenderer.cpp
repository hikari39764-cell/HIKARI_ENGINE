// --- START OF FILE HIKARI_DxRenderer.cpp ---

#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_Render2DBuiltinShaderContract.h"
#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>

#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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


            bool ResolveRender2DBuiltinEntry(
                const ASSETS::SHADER::ShaderAssetContractExpectation& contract,
                const char* entryId,
                ASSETS::SHADER::ShaderStage stage,
                const char* profile,
                ASSETS::SHADER::ShaderAssetEntryLease& outLease) {
                ASSETS::SHADER::ShaderAssetResolveResult resolved =
                    ASSETS::SHADER::ResolveBuiltinShaderEntry(
                        "HIKARI.EngineShader.Render2D_Builtin",
                        { entryId, stage, profile },
                        contract);
                if (!resolved || resolved.lease.GetDxil().empty()) {
                    HIKARI_LOG_ERROR(
                        std::string("[DxRenderer] cooked Shader entry ") +
                        entryId + " is unavailable: " + resolved.diagnostic);
                    return false;
                }
                outLease = std::move(resolved.lease);
                return true;
            }

            D3D12_SHADER_BYTECODE ShaderBytecode(
                const ASSETS::SHADER::ShaderAssetEntryLease& lease) {
                const auto bytes = lease.GetDxil();
                return { bytes.data(), bytes.size() };
            }


            // =========================================================
            // Init()
            // =========================================================

        } // anonymous namespace

        Microsoft::WRL::ComPtr<ID3D12RootSignature> DxRenderer::rootSig_;
        BlendMode DxRenderer::currentBlendMode_ = BlendMode::StraightAlpha;

        bool DxRenderer::Init(const GFX::Context& ctx)
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
            rootSig_.Reset();
            g_pipelineSets[0].format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            g_pipelineSets[1].format = DXGI_FORMAT_R8G8B8A8_UNORM;
            g_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (device == nullptr) {
                HIKARI_LOG_ERROR("[DxRenderer] D3D12 device is unavailable");
                return false;
            }

            // === UploadBuffers ===
            bool uploadBuffersReady = true;
            for (uint32_t frameIndex = 0;
                frameIndex < GFX::kFrameResourceCount;
                ++frameIndex) {
                uploadBuffersReady &= g_uploadCB[frameIndex].Init(
                    device,
                    static_cast<size_t>(1024 * 1024) * 8);
                uploadBuffersReady &= g_uploadVB[frameIndex].Init(
                    device,
                    static_cast<size_t>(1024 * 1024) * 16);
            }
            if (!uploadBuffersReady) {
                HIKARI_LOG_ERROR(
                    "[DxRenderer] dynamic upload page initialization failed");
                return false;
            }

            const GFX::ShaderDeviceCapabilities capabilities =
                GFX::QueryShaderDeviceCapabilities(device);
            const ASSETS::SHADER::ShaderAssetContractExpectation
                shaderContract =
                    ASSETS::SHADER::MakeRender2DBuiltinShaderContract({
                        capabilities.shaderModel,
                        capabilities.rootSignatureVersion,
                        capabilities.featureMask
                    });
            ASSETS::SHADER::ShaderAssetEntryLease meshVs{};
            ASSETS::SHADER::ShaderAssetEntryLease meshPs{};
            ASSETS::SHADER::ShaderAssetEntryLease maskPs{};
            ASSETS::SHADER::ShaderAssetEntryLease lineVs{};
            ASSETS::SHADER::ShaderAssetEntryLease linePs{};
            if (!ResolveRender2DBuiltinEntry(
                    shaderContract,
                    "MeshVS",
                    ASSETS::SHADER::ShaderStage::Vertex,
                    "vs_6_0",
                    meshVs) ||
                !ResolveRender2DBuiltinEntry(
                    shaderContract,
                    "MeshPS",
                    ASSETS::SHADER::ShaderStage::Pixel,
                    "ps_6_0",
                    meshPs) ||
                !ResolveRender2DBuiltinEntry(
                    shaderContract,
                    "MaskPS",
                    ASSETS::SHADER::ShaderStage::Pixel,
                    "ps_6_0",
                    maskPs) ||
                !ResolveRender2DBuiltinEntry(
                    shaderContract,
                    "LineVS",
                    ASSETS::SHADER::ShaderStage::Vertex,
                    "vs_6_0",
                    lineVs) ||
                !ResolveRender2DBuiltinEntry(
                    shaderContract,
                    "LinePS",
                    ASSETS::SHADER::ShaderStage::Pixel,
                    "ps_6_0",
                    linePs)) {
                return false;
            }
            std::string shaderDiagnostic{};
            if (!ASSETS::SHADER::ValidateRender2DBuiltinShaderArtifact(
                    *meshVs.GetArtifact(),
                    shaderDiagnostic)) {
                HIKARI_LOG_ERROR(
                    std::string("[DxRenderer] cooked Shader contract failed: ") +
                    shaderDiagnostic);
                return false;
            }

            // === RootSignature (Global + texture + optional mask) ===
            {
                CD3DX12_DESCRIPTOR_RANGE range{};
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

                CD3DX12_ROOT_PARAMETER params[3]{};
                params[0].InitAsConstantBufferView(0); // b0
                params[1].InitAsDescriptorTable(1, &range); // t0
                params[2].InitAsConstantBufferView(1); // b1


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
                if (!HIKARI_DX_CHECK(
                        hr,
                        "DxRenderer::SerializeRootSignature")) {
                    if (errBlob) {
                        OutputDebugStringA(
                            static_cast<const char*>(
                                errBlob->GetBufferPointer()));
                    }
                    return false;
                }

                hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig_));
                if (!HIKARI_DX_CHECK(
                        hr,
                        "DxRenderer::CreateRootSignature")) {
                    return false;
                }
            }

            // === PSO: Mesh / Sprite ===
            {
                D3D12_INPUT_ELEMENT_DESC elems[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };

                auto FillBasePSO = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
                    {
                        ZeroMemory(&desc, sizeof(desc));
                        desc.pRootSignature = rootSig_.Get();
                        desc.VS = ShaderBytecode(meshVs);
                        desc.PS = ShaderBytecode(meshPs);
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
                        if (!HIKARI_DX_CHECK(
                                hr,
                                "DxRenderer::CreateColorPipeline")) {
                            g_pipelineSets = {};
                            rootSig_.Reset();
                            return false;
                        }
                    }
                }

                D3D12_GRAPHICS_PIPELINE_STATE_DESC maskDesc = baseDesc;
                maskDesc.PS = ShaderBytecode(maskPs);
                maskDesc.BlendState = MakeBlendDesc(BlendMode::StraightAlpha); // 使用?准透明混合

                for (PipelineSet& set : g_pipelineSets) {
                    maskDesc.RTVFormats[0] = set.format;
                    const HRESULT hr = device->CreateGraphicsPipelineState(
                        &maskDesc,
                        IID_PPV_ARGS(set.maskPso.GetAddressOf()));
                    if (!HIKARI_DX_CHECK(
                            hr,
                            "DxRenderer::CreateMaskPipeline")) {
                        g_pipelineSets = {};
                        rootSig_.Reset();
                        return false;
                    }
                }
            }

            // === PSO: Line (保持不?) ===
            {
                D3D12_INPUT_ELEMENT_DESC elems[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                };

                D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
                d.pRootSignature = rootSig_.Get();
                d.VS = ShaderBytecode(lineVs);
                d.PS = ShaderBytecode(linePs);
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
                    if (!HIKARI_DX_CHECK(
                            hr,
                            "DxRenderer::CreateLinePipeline")) {
                        g_pipelineSets = {};
                        rootSig_.Reset();
                        return false;
                    }
                }
            }

            OutputDebugStringA("[DxRenderer] Init OK.\n");
            return true;
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
            rootSig_.Reset();
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
            if (pso == nullptr || rootSig_ == nullptr) {
                return;
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
            if (vb == nullptr) {
                return;
            }

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
            void* cbUpload =
                ActiveUploadCB().Allocate(cbSizeAligned, gpuCB);
            if (cbUpload == nullptr) {
                return;
            }
            memcpy(cbUpload, &cb, sizeof(cb));

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

            PipelineSet* pipelines = GetActivePipelineSet();
            if (pipelines == nullptr || pipelines->maskPso == nullptr ||
                rootSig_ == nullptr) {
                return;
            }
            cmd->SetGraphicsRootSignature(rootSig_.Get());
            cmd->SetPipelineState(pipelines->maskPso.Get());

            // ?色解析
            float r, g, b, a;
            DecodeRGBA(color, r, g, b, a);

            // 2. Upload Vertex Buffer (和普通?制一?)
            D3D12_GPU_VIRTUAL_ADDRESS gpuVB;
            MeshVertex* vb = (MeshVertex*)ActiveUploadVB().Allocate(
                sizeof(MeshVertex) * vertexCount, gpuVB);
            if (vb == nullptr) {
                return;
            }

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
            void* globalUpload =
                ActiveUploadCB().Allocate(globalSizeAligned, gpuCB);
            if (globalUpload == nullptr) {
                return;
            }
            memcpy(globalUpload, &cb, sizeof(cb));
            cmd->SetGraphicsRootConstantBufferView(0, gpuCB);

            // 4. Upload MaskCB (b1)
            D3D12_GPU_VIRTUAL_ADDRESS gpuMaskCB;

            // =============== 修改 MaskCB ===============
            UINT maskSizeAligned = (sizeof(MaskCB) + 255) & ~255;
            void* maskUpload =
                ActiveUploadCB().Allocate(maskSizeAligned, gpuMaskCB);
            if (maskUpload == nullptr) {
                return;
            }
            memcpy(maskUpload, &g_currentMaskParams, sizeof(MaskCB));
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

            PipelineSet* pipelines = GetActivePipelineSet();
            if (pipelines == nullptr || pipelines->linePso == nullptr ||
                rootSig_ == nullptr) {
                return;
            }
            cmd->SetGraphicsRootSignature(rootSig_.Get());
            cmd->SetPipelineState(pipelines->linePso.Get());

            float r, g, b, a;
            DecodeRGBA(color, r, g, b, a);

            LineVertex verts[2] = {
                { x0, y0, r, g, b, a },
                { x1, y1, r, g, b, a }
            };

            D3D12_GPU_VIRTUAL_ADDRESS gpuVB;
            LineVertex* vb = (LineVertex*)ActiveUploadVB().Allocate(sizeof(verts), gpuVB);
            if (vb == nullptr) {
                return;
            }
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
            void* cbUpload =
                ActiveUploadCB().Allocate(cbSizeAligned, gpuCB);
            if (cbUpload == nullptr) {
                return;
            }
            memcpy(cbUpload, &cb, sizeof(cb));

            cmd->SetGraphicsRootConstantBufferView(0, gpuCB);

            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            cmd->IASetVertexBuffers(0, 1, &vbv);

            cmd->DrawInstanced(2, 1, 0, 0);
        }

    } // namespace DX
} // namespace HIKARI
