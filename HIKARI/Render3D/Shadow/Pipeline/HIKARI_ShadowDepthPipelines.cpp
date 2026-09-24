#include "Core/HIKARI_MathConfig.h"
#include "Render3D/Shadow/Pipeline/HIKARI_ShadowDepthPipelines.h"
#include "Render3D/HIKARI_Mesh.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <d3dx12.h>

namespace HIKARI::SHADOW::PIPELINE {

HRESULT ShadowDepthPipelines::Initialize(ID3D12Device* device, ID3D12RootSignature* staticRoot,
                                         ID3D12RootSignature* skinnedRoot, D3D12_SHADER_BYTECODE staticVs,
                                         D3D12_SHADER_BYTECODE skinnedVs, D3D12_SHADER_BYTECODE pixelShader) {
    if (device == nullptr || staticRoot == nullptr || skinnedRoot == nullptr)
        return E_INVALIDARG;
    const D3D12_INPUT_ELEMENT_DESC staticInput[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, u)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, uv1)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    const D3D12_INPUT_ELEMENT_DESC skinnedInput[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.PS = pixelShader;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.FrontCounterClockwise = MATH::kFrontFaceCounterClockwise ? TRUE : FALSE;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    decltype(pipelines_) pipelines{};
    const wchar_t* names[2][2] = {{L"Shadow Static Back Cull PSO", L"Shadow Static Double Sided PSO"},
                                  {L"Shadow Skinned Back Cull PSO", L"Shadow Skinned Double Sided PSO"}};
    for (size_t geometry = 0; geometry < pipelines.size(); ++geometry) {
        const bool skinned = geometry != 0;
        desc.pRootSignature = skinned ? skinnedRoot : staticRoot;
        desc.VS = skinned ? skinnedVs : staticVs;
        desc.InputLayout = skinned ? D3D12_INPUT_LAYOUT_DESC{skinnedInput, static_cast<UINT>(std::size(skinnedInput))}
                                   : D3D12_INPUT_LAYOUT_DESC{staticInput, static_cast<UINT>(std::size(staticInput))};
        for (size_t side = 0; side < pipelines[geometry].size(); ++side) {
            desc.RasterizerState.CullMode = side != 0 ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
            const HRESULT result =
                device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pipelines[geometry][side].GetAddressOf()));
            if (FAILED(result))
                return result;
            pipelines[geometry][side]->SetName(names[geometry][side]);
        }
    }
    pipelines_ = std::move(pipelines);
    return S_OK;
}

ID3D12PipelineState* ShadowDepthPipelines::Resolve(bool skinned, bool doubleSided) const noexcept {
    return pipelines_[skinned ? 1 : 0][doubleSided ? 1 : 0].Get();
}

bool ShadowDepthPipelines::IsReady() const noexcept {
    return std::all_of(pipelines_.begin(), pipelines_.end(), [](const auto& geometry) {
        return std::all_of(geometry.begin(), geometry.end(), [](const auto& pso) { return pso != nullptr; });
    });
}

} // namespace HIKARI::SHADOW::PIPELINE
