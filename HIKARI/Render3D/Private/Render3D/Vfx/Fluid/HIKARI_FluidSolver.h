#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

// 体積流体の解算の GPU 側：根と各 pass の PSO、域ごとの体積、1 子步と V 循環の記録。CPU の段取り（どの域を何子步
// 解くか、定数と源の表）は HIKARI_FluidVolumePass.cpp。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

// Render3D_FluidSimulation.hlsl の入口と同じ並び。
enum class FluidEntry : uint32_t {
    Clear,
    AdvectForward,
    AdvectCorrect,
    Curl,
    ForceField,
    ApplyForces,
    Divergence,
    Smooth,
    ResidualRestrict,
    Prolong,
    CoarsestSolve,
    Project,
    Occupancy,
    Diagnostics,
    Slice,
    Count
};

class FluidSolverPipeline final {
public:
    bool Ensure(ID3D12Device* device);
    void Retire(const GFX::Context& context);
    ID3D12RootSignature* Root() const noexcept { return root_.Get(); }
    ID3D12PipelineState* Get(FluidEntry entry) const noexcept { return pipelines_[static_cast<size_t>(entry)].Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(FluidEntry::Count)> pipelines_{};
    ID3D12Device* device_ = nullptr;
    bool failed_ = false;
};

struct FluidVolume {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
    RenderResourceView srv{};
    RenderResourceView uav{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uint32_t size[3]{};
};

// 1 つの域の体積。速度と密度は 3 枚ずつを子步ごとに役を回して使い（MacCormack の前進・修正・力の一時置き）、
// 圧力と右辺は多重网格の段ごとに 1 枚。統計の buffer は砖块ごとの最大密度と診断（散度・密度の総量）。
struct FluidDomainResources {
    FluidGridLayout layout{};
    std::array<FluidVolume, 3> velocity{};
    std::array<FluidVolume, 3> density{};
    std::vector<FluidVolume> pressure{};
    std::vector<FluidVolume> rhs{};
    Microsoft::WRL::ComPtr<ID3D12Resource> stats{};
    D3D12_RESOURCE_STATES statsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> readback{};
    uint32_t bricks = 0u;
    uint32_t statsBytes = 0u;
    uint64_t bytes = 0u;

    uint32_t BrickCount() const noexcept { return bricks; }
    uint32_t DiagnosticsOffset() const noexcept { return bricks * 4u; }
};

bool CreateFluidDomainResources(ID3D12Device* device, const FluidGridLayout& layout, FluidDomainResources& resources);
bool CreateFluidSliceTexture(ID3D12Device* device, uint32_t width, uint32_t height, FluidVolume& volume);
void RetireVolume(const GFX::Context& context, FluidVolume& volume);
void RetireFluidDomainResources(const GFX::Context& context, FluidDomainResources& resources);

// 記録の窓口：根と PSO を設定し、入出力の状態を移し、dispatch の後に UAV の境界を張る。
struct FluidPassBindings {
    std::array<FluidVolume*, 4> inputs{};
    std::array<FluidVolume*, 2> outputs{};
    D3D12_GPU_VIRTUAL_ADDRESS sources = 0u;
    FluidVolume* slice = nullptr;
    bool stats = false;
};

class FluidRecorder final {
public:
    FluidRecorder(ID3D12GraphicsCommandList* commands, const FluidSolverPipeline& pipeline,
                  FluidDomainResources& domain, D3D12_GPU_VIRTUAL_ADDRESS constants) noexcept;

    void Dispatch(FluidEntry entry, const FluidPassBindings& bindings, const uint32_t (&passSize)[3],
                  const uint32_t (&passParams)[4], const uint32_t (&groups)[3]);
    void Clear(FluidVolume& volume);
    // 1 子步。velocityIndex / densityIndex は今の速度・密度の体積で、終わると新しい方へ進む。
    void Substep(uint32_t& velocityIndex, uint32_t& densityIndex, D3D12_GPU_VIRTUAL_ADDRESS sources, uint32_t vCycles);
    void Occupancy(uint32_t densityIndex);
    void Diagnostics(uint32_t velocityIndex, uint32_t densityIndex);
    void Slice(uint32_t velocityIndex, uint32_t densityIndex, FluidVolume& output, FluidDebugField field, uint32_t axis,
               uint32_t index);
    void CopyStats(ID3D12Resource* readback);

private:
    void Transition(FluidVolume& volume, D3D12_RESOURCE_STATES state);
    void VCycle();
    uint32_t Groups(uint32_t threads, uint32_t groupSize) const noexcept {
        return (threads + groupSize - 1u) / groupSize;
    }

    ID3D12GraphicsCommandList* commands_ = nullptr;
    const FluidSolverPipeline& pipeline_;
    FluidDomainResources& domain_;
    D3D12_GPU_VIRTUAL_ADDRESS constants_ = 0u;
};

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
