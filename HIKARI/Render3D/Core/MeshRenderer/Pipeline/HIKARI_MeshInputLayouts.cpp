#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshInputLayouts.h"

#include <array>
#include <cstddef>

#include "Render3D/HIKARI_Mesh.h"

namespace HIKARI::MESHRENDERER {

    std::span<const D3D12_INPUT_ELEMENT_DESC> GetStaticMeshInputLayout() {
        static constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 5> kElements{ {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, u)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexStatic3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        } };
        return kElements;
    }

    std::span<const D3D12_INPUT_ELEMENT_DESC> GetSkinnedMeshInputLayout() {
        static constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 8> kElements{ {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, uv1)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, color0)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "JOINTS",   0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, joints)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(VertexSkinnedGpu3D, weights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        } };
        return kElements;
    }

} // namespace HIKARI::MESHRENDERER
