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
    Activity,
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
// 圧力と右辺は多重网格の段ごとに 1 枚。統計の buffer は砖块ごとの最大密度、診断（散度・密度の総量）、疎な解算の活動状態。
struct FluidDomainResources {
    FluidGridLayout layout{};
    std::array<FluidVolume, 3> velocity{};
    std::array<FluidVolume, 3> density{};
    std::vector<FluidVolume> pressure{};
    std::vector<FluidVolume> rhs{};
    // 描画の光照体積（格子の半分の分辨率、RGBA16F：x 太陽への光学的な厚さ、y 空への光学的な厚さ、z 近くの遮り）。
    // 描く視図が毎フレーム書く。
    FluidVolume light{};
    Microsoft::WRL::ComPtr<ID3D12Resource> stats{};
    D3D12_RESOURCE_STATES statsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> readback{};
    uint32_t bricks = 0u;
    uint32_t statsBytes = 0u;
    uint64_t bytes = 0u;

    uint32_t BrickCount() const noexcept { return bricks; }
    uint32_t DiagnosticsOffset() const noexcept { return bricks * 4u; }
    uint32_t ActivityOffset() const noexcept { return bricks * 12u; }
};

// 世界（または親）の向き v を、回転 rotation の局所の向きへ移す。
inline MATH::Vec3 RotateToFluidLocal(const MATH::Quat& rotation, const MATH::Vec3& v) noexcept {
    return MATH::RotateVector({-rotation.x, -rotation.y, -rotation.z, rotation.w}, v);
}

// HIKARI_FluidGrid.hlsli の HikariFluidSource と同じ並び（源の表 t6 の 1 行）。
struct FluidSourceGpu {
    float rows[16][4];
};
static_assert(sizeof(FluidSourceGpu) == 256u);

// 回転と倍率の逆で、域の局所の点を源の局所へ、源の局所の向きを域の局所へ移す行と、形・注入・力場を詰める。
FluidSourceGpu PackFluidSource(const FluidSourceSubmission& source, const MATH::Vec3& domainPosition,
                               const MATH::Quat& domainRotation, float cellSize, float burstDensity);

// HIKARI_FluidGrid.hlsli の HikariFluidInteraction と同じ並び（押すものの表 t7 の 1 行）。
struct FluidInteractionGpu {
    float rows[6][4];
};
static_assert(sizeof(FluidInteractionGpu) == 96u);

// 押すもの 1 つを域の局所空間へ移して詰める。domainVelocity は域の動く速さ（障碍物の速度を域に対する速度にする）。
FluidInteractionGpu PackFluidInteraction(const FluidInteraction& interaction, const MATH::Vec3& domainPosition,
                                         const MATH::Quat& domainRotation, const MATH::Vec3& domainVelocity);

bool CreateFluidDomainResources(ID3D12Device* device, const FluidGridLayout& layout, FluidDomainResources& resources);
bool CreateFluidVolumeTexture(ID3D12Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth,
                              FluidVolume& volume);
bool CreateFluidSliceTexture(ID3D12Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height,
                             FluidVolume& volume);
void RetireVolume(const GFX::Context& context, FluidVolume& volume);
void RetireFluidDomainResources(const GFX::Context& context, FluidDomainResources& resources);

// 記録の窓口：根と PSO を設定し、入出力の状態を移し、dispatch の後に UAV の境界を張る。統計の buffer（占有・診断・疎な
// 解算の活動状態）はいつも束ねる。
struct FluidPassBindings {
    std::array<FluidVolume*, 4> inputs{};
    std::array<FluidVolume*, 2> outputs{};
    D3D12_GPU_VIRTUAL_ADDRESS sources = 0u;
    D3D12_GPU_VIRTUAL_ADDRESS interactions = 0u;
    FluidVolume* slice = nullptr;
};

class FluidRecorder final {
public:
    FluidRecorder(ID3D12GraphicsCommandList* commands, const FluidSolverPipeline& pipeline,
                  FluidDomainResources& domain, D3D12_GPU_VIRTUAL_ADDRESS constants) noexcept;

    void Dispatch(FluidEntry entry, const FluidPassBindings& bindings, const uint32_t (&passSize)[3],
                  const uint32_t (&passParams)[4], const uint32_t (&groups)[3]);
    void Clear(FluidVolume& volume);
    // 1 子步。velocityIndex / densityIndex は今の速度・密度の体積で、終わると新しい方へ進む。sources と interactions は
    // 源と押すものの表（数は域の定数）。頭で占有を数え直して砖块の活動状態を決め、動いていない砖块は解かない。
    void Substep(uint32_t& velocityIndex, uint32_t& densityIndex, D3D12_GPU_VIRTUAL_ADDRESS sources,
                 D3D12_GPU_VIRTUAL_ADDRESS interactions, uint32_t vCycles);
    void Occupancy(uint32_t densityIndex);
    void Diagnostics(uint32_t velocityIndex, uint32_t densityIndex);
    void Slice(uint32_t velocityIndex, uint32_t densityIndex, FluidVolume& output, FluidDebugField field, uint32_t axis,
               uint32_t index, bool visualize);
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
