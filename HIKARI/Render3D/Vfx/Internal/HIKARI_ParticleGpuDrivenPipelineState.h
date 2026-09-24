#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

class ParticleGpuDrivenPipelineState final {
public:
    bool Initialize(ID3D12Device* device);
    void Shutdown() noexcept;

    ID3D12RootSignature* GetSimulationRootSignature() const noexcept;
    ID3D12PipelineState* GetSimulationPipeline(uint32_t pipelineIndex) const noexcept;
    ID3D12RootSignature* GetCompactionRootSignature() const noexcept;
    ID3D12PipelineState* GetCompactionPipeline(uint32_t pipelineIndex) const noexcept;
    ID3D12RootSignature* GetDrawRootSignature() const noexcept;
    ID3D12PipelineState* GetDrawPipeline(uint32_t shapeIndex, uint32_t blendIndex, uint32_t depthIndex) const noexcept;
    bool UsesMeshPipeline() const noexcept { return usesMeshPipeline_; }
    ID3D12CommandSignature* GetDrawCommandSignature() const noexcept;

private:
    bool usesMeshPipeline_ = false;
    bool CreateSimulationPipelines(ID3D12Device* device);
    bool CreateCompactionPipelines(ID3D12Device* device);
    bool CreateDrawPipelines(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> simulationRootSignature_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 3u> simulationPipelines_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> compactionRootSignature_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 3u> compactionPipelines_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSignature_{};
    std::array<std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 2u>, 2u>, 2u> drawPipelines_{};
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCommandSignature_{};
};

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
