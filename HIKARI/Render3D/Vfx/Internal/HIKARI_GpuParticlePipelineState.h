#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

    class GpuParticlePipelineState final {
    public:
        bool Initialize(ID3D12Device* device);
        void Shutdown() noexcept;

        ID3D12RootSignature* GetComputeRootSignature() const noexcept;
        ID3D12PipelineState* GetComputePipeline(
            uint32_t pipelineIndex) const noexcept;
        ID3D12RootSignature* GetDrawRootSignature() const noexcept;
        ID3D12PipelineState* GetDrawPipeline(
            uint32_t blendIndex,
            uint32_t depthIndex) const noexcept;

    private:
        bool CreateComputePipeline(ID3D12Device* device);
        bool CreateDrawPipeline(ID3D12Device* device);

        Microsoft::WRL::ComPtr<ID3D12RootSignature>
            computeRootSignature_{};
        std::array<
            Microsoft::WRL::ComPtr<ID3D12PipelineState>,
            3u> computePipelines_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature>
            drawRootSignature_{};
        std::array<
            std::array<
                Microsoft::WRL::ComPtr<ID3D12PipelineState>,
                2u>,
            2u> drawPipelines_{};
    };

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
