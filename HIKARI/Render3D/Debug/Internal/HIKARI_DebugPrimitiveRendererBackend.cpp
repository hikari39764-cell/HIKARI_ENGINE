#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/Debug/Internal/HIKARI_DebugPrimitiveRendererBackend.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_ShaderCompiler.h"
#pragma comment(lib, "d3dcompiler.lib")
namespace HIKARI::RENDERER3D::DEBUG::INTERNAL {
namespace {
using Microsoft::WRL::ComPtr;

struct StrokeVertex3D {
    MATH::Vec3 position{};
    MATH::Vec4 color{};
    float thicknessPixels = 1.5f;
};
struct CameraCB {
    MATH::Mat4 viewProj{};
    MATH::Vec4 viewport{};
};
struct FrameResources {
    ComPtr<ID3D12Resource> cameraCB;
    ComPtr<ID3D12Resource> strokeBuffer;
    ComPtr<ID3D12Resource> solidBuffer;
    CameraCB* cameraMapped = nullptr;
    StrokeVertex3D* strokeMapped = nullptr;
    SolidVertex3D* solidMapped = nullptr;
    size_t strokeCapacity = 0;
    size_t solidCapacity = 0;
};
struct BackendState {
    bool initialized = false;
#if defined(HIKARI_WITH_EDITOR)
    bool editorResourcesInitialized = false;
#endif
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> strokeDepthPso;
    ComPtr<ID3D12PipelineState> strokeXRayPso;
    ComPtr<ID3D12PipelineState> solidDepthPso;
    std::array<FrameResources, GFX::kFrameResourceCount> frames{};
#if defined(HIKARI_WITH_EDITOR)
    std::array<FrameResources, GFX::kFrameResourceCount> editorFrames{};
#endif
};
BackendState g_backend;
MATH::Vec4 DecodeRgba(uint32_t rgba) {
    constexpr float inv255 = 1.0f / 255.0f;
    return {static_cast<float>((rgba >> 24) & 0xFFu) * inv255, static_cast<float>((rgba >> 16) & 0xFFu) * inv255,
            static_cast<float>((rgba >> 8) & 0xFFu) * inv255, static_cast<float>(rgba & 0xFFu) * inv255};
}
bool CompileShader(const wchar_t* relativePath, const char* target, ID3DBlob** output) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const std::filesystem::path path = GFX::ResolveShaderSourcePath(relativePath);
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", target,
                                              flags, 0, output, errors.GetAddressOf());
    if (FAILED(result)) {
        DEBUGLOG::PushRenderError("[DebugPrimitiveRenderer] shader compile failed. path=" + path.generic_string() +
                                  (errors != nullptr
                                       ? "\n" + std::string(static_cast<const char*>(errors->GetBufferPointer()))
                                       : std::string{}));
    }
    return SUCCEEDED(result);
}
bool CreateRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter.Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 1;
    description.pParameters = &parameter;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return SUCCEEDED(GFX::CreateRootSignature(device, description, g_backend.rootSignature.ReleaseAndGetAddressOf()));
}
D3D12_BLEND_DESC AlphaBlendState() {
    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}
D3D12_DEPTH_STENCIL_DESC DepthState(bool xray) {
    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = xray ? FALSE : TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    return depth;
}
bool CreateStrokePso(ID3D12Device* device, bool xray, ID3D12PipelineState** output) {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> geometryShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!CompileShader(L"HIKARI/Shaders/Render3D_DebugStrokeVS.hlsl", "vs_5_0", vertexShader.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Render3D_DebugStrokeGS.hlsl", "gs_5_0", geometryShader.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Render3D_DebugStrokePS.hlsl", "ps_5_0", pixelShader.GetAddressOf())) {
        return false;
    }
    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(StrokeVertex3D, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(StrokeVertex3D, color)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(StrokeVertex3D, thicknessPixels)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = g_backend.rootSignature.Get();
    description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    description.GS = {geometryShader->GetBufferPointer(), geometryShader->GetBufferSize()};
    description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    description.BlendState = AlphaBlendState();
    description.SampleMask = UINT_MAX;
    description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthStencilState = DepthState(xray);
    description.InputLayout = {input, static_cast<UINT>(std::size(input))};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(output)));
}
bool CreateSolidPso(ID3D12Device* device, ID3D12PipelineState** output) {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!CompileShader(L"HIKARI/Shaders/Render3D_DebugSolidVS.hlsl", "vs_5_0", vertexShader.GetAddressOf()) ||
        !CompileShader(L"HIKARI/Shaders/Render3D_DebugSolidPS.hlsl", "ps_5_0", pixelShader.GetAddressOf())) {
        return false;
    }
    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(SolidVertex3D, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(SolidVertex3D, color)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = g_backend.rootSignature.Get();
    description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    description.BlendState = AlphaBlendState();
    description.SampleMask = UINT_MAX;
    description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthStencilState = DepthState(false);
    description.InputLayout = {input, static_cast<UINT>(std::size(input))};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(output)));
}
bool CreateCameraBuffer(ID3D12Device* device, FrameResources& frame) {
    const UINT byteCount = (sizeof(CameraCB) + 255u) & ~255u;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = CD3DX12_RESOURCE_DESC::Buffer(byteCount);
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(frame.cameraCB.GetAddressOf())))) {
        return false;
    }
    return SUCCEEDED(frame.cameraCB->Map(0, nullptr, reinterpret_cast<void**>(&frame.cameraMapped)));
}
template <class TVertex>
bool EnsureCapacity(ID3D12Device* device, ComPtr<ID3D12Resource>& buffer, TVertex*& mapped, size_t& capacity,
                    size_t required) {
    if (required == 0u) {
        return true;
    }
    if (buffer != nullptr && mapped != nullptr && capacity >= required) {
        return true;
    }
    if (buffer != nullptr && mapped != nullptr) {
        buffer->Unmap(0, nullptr);
    }
    buffer.Reset();
    mapped = nullptr;
    capacity = (std::max)(required, size_t{1024u});
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(capacity * sizeof(TVertex)));
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(buffer.GetAddressOf())))) {
        return false;
    }
    return SUCCEEDED(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
}
bool EnsureInitialized() {
    if (g_backend.initialized) {
        return true;
    }
    ID3D12Device* device = GFX::GetCurrentContext().device;
    if (device == nullptr || !CreateRootSignature(device) ||
        !CreateStrokePso(device, false, g_backend.strokeDepthPso.ReleaseAndGetAddressOf()) ||
        !CreateStrokePso(device, true, g_backend.strokeXRayPso.ReleaseAndGetAddressOf()) ||
        !CreateSolidPso(device, g_backend.solidDepthPso.ReleaseAndGetAddressOf())) {
        return false;
    }
    for (FrameResources& frame : g_backend.frames) {
        if (!CreateCameraBuffer(device, frame)) {
            return false;
        }
    }
    g_backend.initialized = true;
    return true;
}
#if defined(HIKARI_WITH_EDITOR)
bool EnsureEditorResources() {
    if (g_backend.editorResourcesInitialized) {
        return true;
    }
    if (!EnsureInitialized() || GFX::GetCurrentContext().device == nullptr) {
        return false;
    }
    for (FrameResources& frame : g_backend.editorFrames) {
        if (!CreateCameraBuffer(GFX::GetCurrentContext().device, frame)) {
            return false;
        }
    }
    g_backend.editorResourcesInitialized = true;
    return true;
}
#endif
FrameResources& ActiveFrame(bool editorView) {
    const uint32_t frameIndex = GFX::GetCurrentContext().frameIndex % GFX::kFrameResourceCount;
#if defined(HIKARI_WITH_EDITOR)
    return editorView ? g_backend.editorFrames[frameIndex] : g_backend.frames[frameIndex];
#else
    (void)editorView;
    return g_backend.frames[frameIndex];
#endif
}
size_t WriteStrokes(StrokeVertex3D* destination, size_t firstVertex, std::span<const Stroke3D> strokes) {
    size_t cursor = firstVertex;
    for (const Stroke3D& stroke : strokes) {
        const MATH::Vec4 color = DecodeRgba(stroke.rgba);
        const float thickness = (std::max)(stroke.thicknessPixels, 0.5f);
        destination[cursor++] = {stroke.from, color, thickness};
        destination[cursor++] = {stroke.to, color, thickness};
    }
    return cursor - firstVertex;
}
void BindCommon(ID3D12GraphicsCommandList* commandList, const FrameResources& frame, ID3D12PipelineState* pso) {
    commandList->SetGraphicsRootSignature(g_backend.rootSignature.Get());
    commandList->SetPipelineState(pso);
    commandList->SetGraphicsRootConstantBufferView(0, frame.cameraCB->GetGPUVirtualAddress());
}
void DrawStrokes(const FrameResources& frame, size_t firstVertex, size_t vertexCount, ID3D12PipelineState* pso) {
    if (vertexCount == 0u || pso == nullptr) {
        return;
    }
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = frame.strokeBuffer->GetGPUVirtualAddress() + firstVertex * sizeof(StrokeVertex3D);
    view.SizeInBytes = static_cast<UINT>(vertexCount * sizeof(StrokeVertex3D));
    view.StrideInBytes = sizeof(StrokeVertex3D);
    ID3D12GraphicsCommandList* commandList = GFX::GetCurrentContext().cmdList;
    BindCommon(commandList, frame, pso);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &view);
    commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
}
void DrawSolids(const FrameResources& frame, size_t vertexCount) {
    if (vertexCount == 0u) {
        return;
    }
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = frame.solidBuffer->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(vertexCount * sizeof(SolidVertex3D));
    view.StrideInBytes = sizeof(SolidVertex3D);
    ID3D12GraphicsCommandList* commandList = GFX::GetCurrentContext().cmdList;
    BindCommon(commandList, frame, g_backend.solidDepthPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &view);
    commandList->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
}
void ReleaseFrame(FrameResources& frame) {
    if (frame.cameraCB != nullptr && frame.cameraMapped != nullptr) {
        frame.cameraCB->Unmap(0, nullptr);
    }
    if (frame.strokeBuffer != nullptr && frame.strokeMapped != nullptr) {
        frame.strokeBuffer->Unmap(0, nullptr);
    }
    if (frame.solidBuffer != nullptr && frame.solidMapped != nullptr) {
        frame.solidBuffer->Unmap(0, nullptr);
    }
    frame = {};
}
} // namespace
void RenderPrimitiveBatches(const Camera3D& camera, float screenWidth, float screenHeight, bool editorView,
                            std::span<const Stroke3D> xrayStrokes, std::span<const Stroke3D> depthStrokes,
                            std::span<const SolidVertex3D> solidVertices) {
    if (xrayStrokes.empty() && depthStrokes.empty() && solidVertices.empty()) {
        return;
    }
#if defined(HIKARI_WITH_EDITOR)
    if (!EnsureInitialized() || (editorView && !EnsureEditorResources())) {
        return;
    }
#else
    if (!EnsureInitialized()) {
        return;
    }
#endif
    ID3D12Device* device = GFX::GetCurrentContext().device;
    if (device == nullptr || GFX::GetCurrentContext().cmdList == nullptr) {
        return;
    }
    FrameResources& frame = ActiveFrame(editorView);
    const size_t xrayVertexCount = xrayStrokes.size() * 2u;
    const size_t depthVertexCount = depthStrokes.size() * 2u;
    const size_t strokeVertexCount = xrayVertexCount + depthVertexCount;
    if (!EnsureCapacity(device, frame.strokeBuffer, frame.strokeMapped, frame.strokeCapacity, strokeVertexCount) ||
        !EnsureCapacity(device, frame.solidBuffer, frame.solidMapped, frame.solidCapacity, solidVertices.size())) {
        return;
    }
    const float safeWidth = (std::max)(screenWidth, 1.0f);
    const float safeHeight = (std::max)(screenHeight, 1.0f);
    frame.cameraMapped->viewProj = camera.GetViewProj();
    frame.cameraMapped->viewport = {safeWidth, safeHeight, 1.0f / safeWidth, 1.0f / safeHeight};
    if (!solidVertices.empty()) {
        std::copy(solidVertices.begin(), solidVertices.end(), frame.solidMapped);
    }
    size_t cursor = 0;
    const size_t xrayStart = cursor;
    cursor += WriteStrokes(frame.strokeMapped, cursor, xrayStrokes);
    const size_t depthStart = cursor;
    cursor += WriteStrokes(frame.strokeMapped, cursor, depthStrokes);
    DrawSolids(frame, solidVertices.size());
    DrawStrokes(frame, xrayStart, xrayVertexCount, g_backend.strokeXRayPso.Get());
    DrawStrokes(frame, depthStart, depthVertexCount, g_backend.strokeDepthPso.Get());
}
#if defined(HIKARI_WITH_EDITOR)
void ShutdownEditorViewResources() {
    for (FrameResources& frame : g_backend.editorFrames) {
        ReleaseFrame(frame);
    }
    g_backend.editorResourcesInitialized = false;
}
#endif
} // namespace HIKARI::RENDERER3D::DEBUG::INTERNAL
