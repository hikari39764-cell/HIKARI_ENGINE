#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::GFX {
    struct Context;
}

namespace HIKARI::GFX::RAYTRACING {

    // BLAS は共有ジオメトリ、TLAS は View 非依存のインスタンス集合を表す。
    // 入力ジオメトリは build 完了まで、参照 BLAS は TLAS の最終使用まで
    // 呼び出し側が保持する。ここでは Scene/材質/選択予算を所有しない。
    class DxrAccelerationStructure final {
    public:
        DxrAccelerationStructure() = default;
        ~DxrAccelerationStructure();
        DxrAccelerationStructure(const DxrAccelerationStructure&) = delete;
        DxrAccelerationStructure& operator=(const DxrAccelerationStructure&) = delete;

        bool BuildBottomLevel(const Context& graphics,
            std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometry,
            uint64_t maximumAllocationBytes, std::string* outMessage = nullptr);
        bool BuildTopLevel(const Context& graphics,
            std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances,
            uint64_t maximumAllocationBytes, bool allowUpdate = false,
            const DxrAccelerationStructure* updateSource = nullptr,
            std::string* outMessage = nullptr);
        void Reset(const Context* retireContext = nullptr);

        bool IsReady() const noexcept { return result_ != nullptr; }
        D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const noexcept;
        // 現在の result + scratch + instance upload の committed allocation。
        // 遅延解放待ちの旧世代、共有入力ジオメトリは含まない。
        uint64_t GetAllocationBytes() const noexcept { return allocationBytes_; }

    private:
        bool Build(const Context& graphics,
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs,
            std::span<const D3D12_RAYTRACING_INSTANCE_DESC> instances,
            uint64_t maximumAllocationBytes,
            const DxrAccelerationStructure* updateSource, std::string* outMessage);

        Microsoft::WRL::ComPtr<ID3D12Resource> result_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer_{};
        ID3D12Device* device_ = nullptr;
        uint64_t deviceGeneration_ = 0;
        uint64_t allocationBytes_ = 0;
        uint32_t descriptorCount_ = 0;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE type_{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags_{};
    };

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
        DxrAccelerationStructure bottomLevel_{};
        DxrAccelerationStructure topLevel_{};
    };

} // namespace HIKARI::GFX::RAYTRACING
