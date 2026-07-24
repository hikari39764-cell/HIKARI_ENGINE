#pragma once
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct VertexStatic3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        float u = 0.0f;
        float v = 0.0f;
        MATH::Vec2 uv1{};
    };

    struct VertexSkinnedGpu3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color0{ 1, 1, 1, 1 };
        uint16_t joints[4]{};
        float weights[4]{};
    };

    class Mesh {
    public:
        bool CreateStatic(ID3D12Device* device, const std::vector<VertexStatic3D>& vertices, const std::vector<uint32_t>& indices);
        bool CreateSkinned(ID3D12Device* device, const std::vector<VertexSkinnedGpu3D>& vertices, const std::vector<uint32_t>& indices);

        bool IsValid() const;
        const D3D12_VERTEX_BUFFER_VIEW& GetVBView() const;
        const D3D12_INDEX_BUFFER_VIEW& GetIBView() const;
        uint32_t GetIndexCount() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vbView_{};
        D3D12_INDEX_BUFFER_VIEW ibView_{};
        uint32_t indexCount_ = 0;
    };

} // namespace HIKARI
