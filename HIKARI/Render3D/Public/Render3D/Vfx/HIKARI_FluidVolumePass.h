#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

// 体積流体（VFX の流体層）の GPU 解算。MeshRenderer の視図資源が粒子 pass と並べて持ち、粒子と同じ描画入力
// （ParticleRenderInput.fluids）を受けて、域ごとに固定子步で解く。描画（步进）は第 4 步でここへ加わる。
namespace HIKARI::RENDER3D::FLUID {

// 画質の档位（表はここだけ）。解算の分辨率の倍率、子步ごとの V 循環の数、步进の步数と分辨率、同時に解く域の数。
struct FluidQualityPolicy {
    float resolutionScale = 1.0f;
    uint32_t vCycles = 2u;
    uint32_t marchSteps = 64u;
    uint32_t marchDivisor = 2u;
    uint32_t maxDomains = 4u;
};
FluidQualityPolicy ResolveFluidQualityPolicy(FluidSmokeQuality quality) noexcept;

// 解算の格子：格は立方体（最長辺 / 分辨率）、各辺の格数は多重网格の段の数で割り切れる数（最も粗い段は各辺 8 以下）。
struct FluidGridLayout {
    uint32_t cells[3]{8u, 8u, 8u};
    float cellSize = 0.5f;
    uint32_t levels = 1u;
};
FluidGridLayout ResolveFluidGridLayout(const MATH::Vec3& domainSize, uint32_t resolution) noexcept;
uint64_t EstimateFluidDomainBytes(const FluidGridLayout& layout) noexcept;

// 固定子步の時計。ゲーム時間を貯めて子步の長さで割り、1 フレームの上限を超えた分は捨てる（遅いフレームで
// 解算が積み上がらない）。フレームの長さが違っても同じゲーム時間なら同じ子步の列になる。
struct FluidStepClock {
    double pendingSeconds = 0.0;
    uint64_t completedSteps = 0u;

    uint32_t Advance(double deltaSeconds, double stepSeconds, uint32_t maximumSteps) noexcept;
};

enum class FluidDebugField : uint8_t { Density, Velocity, Divergence, Pressure };

// 調試の切片の要求：域（実例键）、量、軸（0 X、1 Y、2 Z）と軸方向の位置 [0, 1]。
struct FluidDebugSliceRequest {
    PARTICLES::ParticleEmitterInstanceKey key{};
    FluidDebugField field = FluidDebugField::Density;
    uint32_t axis = 2u;
    float position = 0.5f;
};

// 直前のフレームで書いた切片（RGBA16F。密度は rgb、速度は xyz と大きさ、散度・圧力は r）。
struct FluidDebugSliceView {
    ID3D12Resource* resource = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    uint32_t width = 0u;
    uint32_t height = 0u;
    bool valid = false;
};

struct FluidDomainStats {
    PARTICLES::ParticleEmitterInstanceKey key{};
    uint32_t cells[3]{};
    uint32_t levels = 0u;
    uint64_t bytes = 0u;
    uint64_t completedSteps = 0u;
    uint32_t stepsThisFrame = 0u;
    float prewarmRemainingSeconds = 0.0f;
    // 回読（数フレーム遅れ）：密度のある砖块の数（第 7 步の疎な割り当ての量）と、診断を有効にした時の値。
    uint32_t activeBricks = 0u;
    uint32_t totalBricks = 0u;
    uint64_t readbackSteps = 0u;
    bool diagnosticsValid = false;
    float maxDivergence = 0.0f;
    float densityTotal = 0.0f;
};

struct FluidVolumePassStats {
    uint32_t domains = 0u;
    uint32_t skippedDomains = 0u;
    uint32_t stepsThisFrame = 0u;
    uint64_t bytes = 0u;
    uint32_t resourceFailures = 0u;
    std::vector<FluidDomainStats> perDomain{};
};

struct FluidFrameSettings {
    FluidSmokeQuality quality = FluidSmokeQuality::Balanced;
    // 投影後の散度と密度の総量を回読する（編集器の診断・試験・基準が読む）。
    bool diagnostics = false;
};

class FluidVolumePass final {
public:
    FluidVolumePass();
    ~FluidVolumePass();
    FluidVolumePass(const FluidVolumePass&) = delete;
    FluidVolumePass& operator=(const FluidVolumePass&) = delete;

    // 描画入力の域を解く（退役した実例を消し、作り直し・子步・占有の回読を記録する）。入力が無ければ何もしない。
    bool PrepareFrame(const GFX::Context& context, const PARTICLES::ParticleRenderInput* input,
                      const FluidFrameSettings& settings);
    void ReleaseNamespace(uint64_t playbackNamespace);
    void Shutdown();

    void RequestDebugSlice(const FluidDebugSliceRequest& request);
    void ClearDebugSlice();
    FluidDebugSliceView GetDebugSlice() const noexcept;
    const FluidVolumePassStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::FLUID
