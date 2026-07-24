#include "HIKARI_Renderer3D_Debug.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI_Services.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace HIKARI::RENDERER3D::DEBUG {

    using Microsoft::WRL::ComPtr;

    namespace {
        struct DebugLineVertex3D {
            MATH::Vec3 position{};
            MATH::Vec4 color{};
        };

        struct CameraCB {
            MATH::Mat4 viewProj{};
        };

        struct FrameResources {
            ComPtr<ID3D12Resource> cameraCB;
            ComPtr<ID3D12Resource> vertexBuffer;
            CameraCB* cameraMapped = nullptr;
            DebugLineVertex3D* vertexMapped = nullptr;
            size_t vertexCapacity = 0;
        };

        struct State {
            bool initialized = false;
#if defined(HIKARI_WITH_EDITOR)
            bool editorViewResourcesInitialized = false;
#endif
            ComPtr<ID3D12RootSignature> rootSig;
            ComPtr<ID3D12PipelineState> depthTestPso;
            ComPtr<ID3D12PipelineState> xrayPso;
            std::array<FrameResources, GFX::kFrameResourceCount> frameResources{};
#if defined(HIKARI_WITH_EDITOR)
            std::array<FrameResources, GFX::kFrameResourceCount>
                editorViewFrameResources{};
#endif
        };

        std::vector<WireCube> g_cubes;
        std::vector<Line3D> g_lines;
        std::vector<Axis3D> g_axes;
        std::vector<Grid3D> g_grids;
        std::vector<Line3D> g_expandedScratch;
        std::vector<Line3D> g_depthTestScratch;
        std::vector<Line3D> g_xrayScratch;
        State g_state;
        DebugRendererFrameStats g_submitStats{};
        DebugRendererFrameStats g_frameStats{};
#if defined(HIKARI_WITH_EDITOR)
        bool g_renderingEditorView = false;
#endif

        MATH::Vec4 DecodeRgba(uint32_t rgba) {
            constexpr float inv255 = 1.0f / 255.0f;
            return {
                static_cast<float>((rgba >> 24) & 0xFFu) * inv255,
                static_cast<float>((rgba >> 16) & 0xFFu) * inv255,
                static_cast<float>((rgba >> 8) & 0xFFu) * inv255,
                static_cast<float>(rgba & 0xFFu) * inv255
            };
        }

        void PushLine(std::vector<Line3D>& lines, const MATH::Vec3& from, const MATH::Vec3& to, uint32_t rgba, DebugDepthMode depthMode) {
            Line3D line{};
            line.from = from;
            line.to = to;
            line.rgba = rgba;
            line.depthMode = depthMode;
            lines.push_back(line);
        }

        MATH::Vec3 TransformPoint3(const MATH::Mat4& m, const MATH::Vec3& p) {
            const MATH::Vec4 out = m.TransformPoint({ p.x, p.y, p.z, 1.0f });
            return { out.x, out.y, out.z };
        }

        size_t EstimateExpandedLineCount() {
            size_t count = g_lines.size();
            count += g_axes.size() * 3u;
            count += g_cubes.size() * 12u;

            for (const Grid3D& grid : g_grids) {
                const int n = (grid.halfCount < 1) ? 1 : grid.halfCount;
                count += static_cast<size_t>((n * 2 + 1) * 2);
            }

            return count;
        }

        void ExpandSubmittedLines(std::vector<Line3D>& outLines) {
            outLines.clear();
            outLines.reserve(EstimateExpandedLineCount());
            outLines.insert(outLines.end(), g_lines.begin(), g_lines.end());

            for (const Axis3D& axis : g_axes) {
                const MATH::Mat4 world = axis.transform.GetWorldMatrix();
                const MATH::Vec3 origin = TransformPoint3(world, { 0.0f, 0.0f, 0.0f });
                PushLine(outLines, origin, TransformPoint3(world, { axis.length, 0.0f, 0.0f }), axis.xColor, axis.depthMode);
                PushLine(outLines, origin, TransformPoint3(world, { 0.0f, axis.length, 0.0f }), axis.yColor, axis.depthMode);
                PushLine(outLines, origin, TransformPoint3(world, { 0.0f, 0.0f, axis.length }), axis.zColor, axis.depthMode);
            }

            for (const Grid3D& grid : g_grids) {
                const int n = (grid.halfCount < 1) ? 1 : grid.halfCount;
                const float step = (grid.spacing <= 0.0f) ? 1.0f : grid.spacing;
                const float extent = static_cast<float>(n) * step;
                for (int i = -n; i <= n; ++i) {
                    const float pos = static_cast<float>(i) * step;
                    PushLine(outLines, { -extent, 0.0f, pos }, { extent, 0.0f, pos }, grid.rgba, grid.depthMode);
                    PushLine(outLines, { pos, 0.0f, -extent }, { pos, 0.0f, extent }, grid.rgba, grid.depthMode);
                }
            }

            static constexpr int kEdges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},
                {4,5},{5,6},{6,7},{7,4},
                {0,4},{1,5},{2,6},{3,7}
            };

            for (const WireCube& cube : g_cubes) {
                const float hs = cube.size * 0.5f;
                const std::array<MATH::Vec3, 8> local = {{
                    {-hs,-hs,-hs}, {hs,-hs,-hs}, {hs,hs,-hs}, {-hs,hs,-hs},
                    {-hs,-hs, hs}, {hs,-hs, hs}, {hs,hs, hs}, {-hs,hs, hs}
                }};

                const MATH::Mat4 world = cube.transform.GetWorldMatrix();
                std::array<MATH::Vec3, 8> worldPoints{};
                for (size_t i = 0; i < local.size(); ++i) {
                    worldPoints[i] = TransformPoint3(world, local[i]);
                }

                for (const auto& edge : kEdges) {
                    PushLine(outLines, worldPoints[edge[0]], worldPoints[edge[1]], cube.rgba, cube.depthMode);
                }
            }
        }

        bool CreateRootSignature(ID3D12Device* device) {
            D3D12_ROOT_PARAMETER param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            param.Descriptor.ShaderRegister = 0;
            param.Descriptor.RegisterSpace = 0;

            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.NumParameters = 1;
            desc.pParameters = &param;
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            return SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(g_state.rootSig.GetAddressOf())));
        }

        bool CreatePipeline(ID3D12Device* device, bool xray, ID3D12PipelineState** outPso) {
            UINT flags = 0;
#if defined(_DEBUG)
            flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_DebugLineVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, vs.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }
            err.Reset();
            if (FAILED(D3DCompileFromFile(L"HIKARI/Shaders/Render3D_DebugLinePS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, ps.GetAddressOf(), err.GetAddressOf()))) {
                if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
                return false;
            }

            const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(DebugLineVertex3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DebugLineVertex3D, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = g_state.rootSig.Get();
            desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = xray ? FALSE : TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            desc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(outPso)));
        }

        FrameResources& ActiveFrameResources(
#if defined(HIKARI_WITH_EDITOR)
            bool editorView = false
#endif
        ) {
            const uint32_t frameIndex = SERVICES::gCtx.frameIndex % GFX::kFrameResourceCount;
#if defined(HIKARI_WITH_EDITOR)
            return editorView
                ? g_state.editorViewFrameResources[frameIndex]
                : g_state.frameResources[frameIndex];
#else
            return g_state.frameResources[frameIndex];
#endif
        }

        bool CreateCameraBuffer(ID3D12Device* device, FrameResources& frame) {
            const UINT bytes = (sizeof(CameraCB) + 255u) & ~255u;
            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(frame.cameraCB.GetAddressOf())))) {
                return false;
            }
            return SUCCEEDED(frame.cameraCB->Map(0, nullptr, reinterpret_cast<void**>(&frame.cameraMapped)));
        }

        bool EnsureVertexCapacity(ID3D12Device* device, FrameResources& frame, size_t requiredVertices) {
            if (requiredVertices == 0) {
                return true;
            }
            if (frame.vertexBuffer != nullptr && frame.vertexMapped != nullptr && frame.vertexCapacity >= requiredVertices) {
                return true;
            }

            if (frame.vertexBuffer != nullptr) {
                frame.vertexBuffer->Unmap(0, nullptr);
            }
            frame.vertexBuffer.Reset();
            frame.vertexMapped = nullptr;
            frame.vertexCapacity = std::max<size_t>(requiredVertices, 1024u);

            const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(frame.vertexCapacity * sizeof(DebugLineVertex3D)));
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(frame.vertexBuffer.GetAddressOf())))) {
                return false;
            }
            return SUCCEEDED(frame.vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&frame.vertexMapped)));
        }

        bool EnsureInitialized() {
            if (g_state.initialized) {
                return true;
            }

            ID3D12Device* device = SERVICES::gCtx.device;
            if (device == nullptr) {
                return false;
            }

            if (!CreateRootSignature(device) ||
                !CreatePipeline(device, false, g_state.depthTestPso.GetAddressOf()) ||
                !CreatePipeline(device, true, g_state.xrayPso.GetAddressOf())) {
                return false;
            }
            for (FrameResources& frame : g_state.frameResources) {
                if (!CreateCameraBuffer(device, frame)) {
                    return false;
                }
            }

            g_state.initialized = true;
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
        bool EnsureEditorViewResources() {
            if (g_state.editorViewResourcesInitialized) {
                return true;
            }
            if (!EnsureInitialized() || SERVICES::gCtx.device == nullptr) {
                return false;
            }
            for (FrameResources& frame : g_state.editorViewFrameResources) {
                if (frame.cameraCB != nullptr &&
                    frame.cameraMapped != nullptr) {
                    continue;
                }
                frame.cameraCB.Reset();
                frame.cameraMapped = nullptr;
                if (!CreateCameraBuffer(SERVICES::gCtx.device, frame)) {
                    frame.cameraCB.Reset();
                    frame.cameraMapped = nullptr;
                    return false;
                }
            }
            g_state.editorViewResourcesInitialized = true;
            return true;
        }
#endif

        size_t WriteLineVertices(DebugLineVertex3D* dst, size_t startVertex, const std::vector<Line3D>& lines) {
            size_t vertexIndex = startVertex;
            for (const Line3D& line : lines) {
                const MATH::Vec4 color = DecodeRgba(line.rgba);
                dst[vertexIndex++] = { line.from, color };
                dst[vertexIndex++] = { line.to, color };
            }
            return vertexIndex - startVertex;
        }

        void RenderLineBatch(const FrameResources& frame, size_t firstVertex, size_t vertexCount, ID3D12PipelineState* pso) {
            if (vertexCount == 0 || pso == nullptr || SERVICES::gCtx.cmdList == nullptr || !EnsureInitialized()) {
                return;
            }

            if (frame.cameraCB == nullptr || frame.vertexBuffer == nullptr) {
                return;
            }

            D3D12_VERTEX_BUFFER_VIEW vb{};
            vb.BufferLocation = frame.vertexBuffer->GetGPUVirtualAddress() + firstVertex * sizeof(DebugLineVertex3D);
            vb.SizeInBytes = static_cast<UINT>(vertexCount * sizeof(DebugLineVertex3D));
            vb.StrideInBytes = sizeof(DebugLineVertex3D);

            ID3D12GraphicsCommandList* cmd = SERVICES::gCtx.cmdList;
            cmd->SetGraphicsRootSignature(g_state.rootSig.Get());
            cmd->SetPipelineState(pso);
            cmd->SetGraphicsRootConstantBufferView(0, frame.cameraCB->GetGPUVirtualAddress());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            cmd->IASetVertexBuffers(0, 1, &vb);
            cmd->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
        }
    }

    void Reset() {
        g_cubes.clear();
        g_lines.clear();
        g_axes.clear();
        g_grids.clear();
        g_submitStats = {};
    }

    void SubmitWireCube(const WireCube& cube) {
        g_cubes.push_back(cube);
        ++g_submitStats.wireCubeCount;
    }

    void SubmitLine3D(const Line3D& line) {
        g_lines.push_back(line);
        ++g_submitStats.submittedLineCount;
        if (line.depthMode == DebugDepthMode::XRay) {
            ++g_submitStats.submittedXRayLineCount;
        }
    }

    void SubmitAxis3D(const Axis3D& axis) {
        g_axes.push_back(axis);
        ++g_submitStats.axisCount;
    }

    void SubmitGrid3D(const Grid3D& grid) {
        g_grids.push_back(grid);
        ++g_submitStats.gridCount;
    }

    void SetLightProbeVolumeGizmoStats(
        uint32_t totalPointCount,
        uint32_t drawnPointCount,
        uint32_t mode,
        bool capped) {

        g_submitStats.lightProbeGizmoTotalPointCount = totalPointCount;
        g_submitStats.lightProbeGizmoDrawnPointCount = drawnPointCount;
        g_submitStats.lightProbeGizmoMode = mode;
        g_submitStats.lightProbeGizmoCapped = capped;
    }

    const DebugRendererFrameStats& GetDebugRendererFrameStats() {
        return g_frameStats;
    }

    void RenderAll(const Camera3D& camera, float screenW, float screenH) {
        (void)screenW;
        (void)screenH;

        ExpandSubmittedLines(g_expandedScratch);
#if defined(HIKARI_WITH_EDITOR)
        if (!g_renderingEditorView) {
            g_frameStats = g_submitStats;
            g_frameStats.expandedLineCount = g_expandedScratch.size();
        }
#else
        g_frameStats = g_submitStats;
        g_frameStats.expandedLineCount = g_expandedScratch.size();
#endif
        if (g_expandedScratch.empty()) {
            return;
        }

        // 毎フレームの一時 vector 確保を避ける。
        g_depthTestScratch.clear();
        g_xrayScratch.clear();
        g_depthTestScratch.reserve(g_expandedScratch.size());
        g_xrayScratch.reserve(g_expandedScratch.size());
        for (const Line3D& line : g_expandedScratch) {
            if (line.depthMode == DebugDepthMode::XRay) {
                g_xrayScratch.push_back(line);
            } else {
                g_depthTestScratch.push_back(line);
            }
        }
#if defined(HIKARI_WITH_EDITOR)
        if (!g_renderingEditorView) {
            g_frameStats.depthTestLineCount = g_depthTestScratch.size();
            g_frameStats.xrayLineCount = g_xrayScratch.size();
        }
#else
        g_frameStats.depthTestLineCount = g_depthTestScratch.size();
        g_frameStats.xrayLineCount = g_xrayScratch.size();
#endif

#if defined(HIKARI_WITH_EDITOR)
        if (!EnsureInitialized() ||
            (g_renderingEditorView && !EnsureEditorViewResources())) {
            return;
        }
#else
        if (!EnsureInitialized()) {
            return;
        }
#endif
        ID3D12Device* device = SERVICES::gCtx.device;
#if defined(HIKARI_WITH_EDITOR)
        FrameResources& frame = ActiveFrameResources(g_renderingEditorView);
#else
        FrameResources& frame = ActiveFrameResources();
#endif
        const size_t depthVertexCount = g_depthTestScratch.size() * 2u;
        const size_t xrayVertexCount = g_xrayScratch.size() * 2u;
        const size_t totalVertexCount = depthVertexCount + xrayVertexCount;
        if (device == nullptr || !EnsureVertexCapacity(device, frame, totalVertexCount) || frame.vertexMapped == nullptr) {
            return;
        }

        if (frame.cameraMapped != nullptr) {
            frame.cameraMapped->viewProj = camera.GetViewProj();
        }

        size_t vertexCursor = 0;
        const size_t depthVertexStart = vertexCursor;
        vertexCursor += WriteLineVertices(frame.vertexMapped, vertexCursor, g_depthTestScratch);
        const size_t xrayVertexStart = vertexCursor;
        vertexCursor += WriteLineVertices(frame.vertexMapped, vertexCursor, g_xrayScratch);

        RenderLineBatch(frame, depthVertexStart, depthVertexCount, g_state.depthTestPso.Get());
        RenderLineBatch(frame, xrayVertexStart, xrayVertexCount, g_state.xrayPso.Get());
    }

#if defined(HIKARI_WITH_EDITOR)
    void RenderAllForView(
        RENDER3D::RenderViewId viewId,
        const Camera3D& camera,
        float screenW,
        float screenH) {

        if (viewId == RENDER3D::kPrimaryRenderViewId) {
            RenderAll(camera, screenW, screenH);
            return;
        }
        g_renderingEditorView = true;
        RenderAll(camera, screenW, screenH);
        g_renderingEditorView = false;
    }

    void ShutdownEditorViewResources() {
        g_renderingEditorView = false;
        for (FrameResources& frame : g_state.editorViewFrameResources) {
            if (frame.cameraCB != nullptr && frame.cameraMapped != nullptr) {
                frame.cameraCB->Unmap(0, nullptr);
            }
            if (frame.vertexBuffer != nullptr && frame.vertexMapped != nullptr) {
                frame.vertexBuffer->Unmap(0, nullptr);
            }
            frame.cameraCB.Reset();
            frame.vertexBuffer.Reset();
            frame.cameraMapped = nullptr;
            frame.vertexMapped = nullptr;
            frame.vertexCapacity = 0;
        }
        g_state.editorViewResourcesInitialized = false;
    }
#endif

} // namespace HIKARI::RENDERER3D::DEBUG
