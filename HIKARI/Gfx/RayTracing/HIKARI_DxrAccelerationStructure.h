#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::GFX {
    struct Context;
}

namespace HIKARI::GFX::RAYTRACING {

    struct DxrPosition {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    class DxrTriangleAccelerationStructure final {
    public:
        ~DxrTriangleAccelerationStructure();
        bool Build(
            const Context& graphics,
            const std::vector<DxrPosition>& positions,
            const std::vector<uint32_t>& indices,
            std::string* outMessage = nullptr);
        void Reset();

        bool IsReady() const noexcept;
        D3D12_GPU_VIRTUAL_ADDRESS
            GetTopLevelGpuAddress() const noexcept;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> bottomScratch_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> bottomLevel_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> topScratch_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> topLevel_{};
    };

} // namespace HIKARI::GFX::RAYTRACING
