#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI {
struct SceneEnvironment;
}

// 体積流体（VFX の流体層）の GPU 解算と描画。MeshRenderer の視図資源が粒子 pass と並べて持ち、粒子と同じ描画入力
// （ParticleRenderInput.fluids）を受けて域ごとに固定子步で解き、帧图の FluidVolumes pass で視図へ描く。
namespace HIKARI::RENDER3D::FLUID {

// 画質の档位（表は HIKARI_FluidLayout.cpp だけ）。解算の分辨率の倍率、子步ごとの V 循環の数、半分辨率の步进の步数、
// 同時に解く域の数。
struct FluidQualityPolicy {
    float resolutionScale = 1.0f;
    uint32_t vCycles = 2u;
    uint32_t marchSteps = 64u;
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

// 調試の切片の要求：域（実例键）、量、軸（0 X、1 Y、2 Z）と軸方向の位置 [0, 1]。visualize は画面に出す色
// （rgb に見やすい色、a = 1）、偽なら値そのまま（試験・智能体が読む）。
struct FluidDebugSliceRequest {
    PARTICLES::ParticleEmitterInstanceKey key{};
    FluidDebugField field = FluidDebugField::Density;
    uint32_t axis = 2u;
    float position = 0.5f;
    bool visualize = false;
};

// 直前のフレームで書いた切片（RGBA16F）。値そのままなら密度は rgb、速度は xyz と大きさ、散度・圧力は r。
// 色なら密度は 1 - exp(-密度) の灰、速度は ±4 m/s を 0..1 へ、散度と圧力は正を赤・負を青。
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
    uint32_t renderedDomains = 0u;
    std::vector<FluidDomainStats> perDomain{};
};

// 描く視図（主視図と編集器の視図）。行列は場面の深度と同じ（抖动込みの）描画格子。光は絶対量で、前露光は preExposure。
// 前のフレームの行列は描画側が視図ごとに覚える（historyValid が偽なら履歴を使わない）。
struct FluidRenderView {
    uint64_t viewKey = 0u;
    MATH::Mat4 viewProj{};
    MATH::Mat4 invViewProj{};
    MATH::Vec3 cameraPosition{};
    MATH::Vec3 cameraForward{0.0f, 0.0f, 1.0f};
    bool historyValid = false;
    uint32_t renderWidth = 0u;
    uint32_t renderHeight = 0u;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
    DXGI_FORMAT sceneColorFormat = DXGI_FORMAT_UNKNOWN;
    // 太陽へ向かう単位方向、太陽の法線照度（rgb）、空の環境光の輝度（rgb）。
    MATH::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    MATH::Vec3 sunIlluminance{};
    MATH::Vec3 ambientRadiance{};
    float preExposure = 1.0f;
    float exposureRatio = 1.0f;
    bool sceneShadows = false;
    FluidSmokeQuality quality = FluidSmokeQuality::Balanced;
};

// 透明物体が読む相機体積（HIKARI_FluidCameraVolume.hlsli）。params：x near、y log(far/near)、z 層の数、w 1 で有効。
struct FluidCameraVolumeView {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    MATH::Vec4 params{};
};

// 視図の行列・相機の位置と場面の環境から、相機と光（大気を通った地表の太陽、空の環境光、今の露出）と画質を埋める。
// 帧图の主視図と編集器の視図が共用し、描画の大きさ・深度・色の形式・履歴の有無は呼ぶ側が足す。
FluidRenderView MakeFluidRenderView(const SceneEnvironment& environment, uint64_t viewKey, const MATH::Mat4& viewProj,
                                    const MATH::Mat4& invViewProj, const MATH::Vec3& cameraPosition);

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

    // 解いた域を視図へ描く：光照体積、相機体積、半分辨率の步进、時間の再構成、場面への合成。場面の深度は読める状態で
    // 呼ぶ（BeginDepthRead の後）。bindSceneTarget は合成の前に場面の色と読み取り専用の深度を描画先へ戻す。
    bool RenderVolumes(const GFX::Context& context, const FluidRenderView& view,
                       const std::function<bool()>& bindSceneTarget);
    bool HasRenderableDomains() const noexcept;
    // このフレームで最後に描いた視図の相機体積（粒子など透明物体の描画が読む）。
    FluidCameraVolumeView GetCameraVolume() const noexcept;

    void RequestDebugSlice(const FluidDebugSliceRequest& request);
    void ClearDebugSlice();
    FluidDebugSliceView GetDebugSlice() const noexcept;
    const FluidVolumePassStats& GetStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace HIKARI::RENDER3D::FLUID
