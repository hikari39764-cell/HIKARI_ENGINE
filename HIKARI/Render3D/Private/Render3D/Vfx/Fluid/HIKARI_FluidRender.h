#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

// 体積流体の描画（Render3D_FluidVolume.hlsl）：域ごとの光照体積、透明物体のための相機体積、半分辨率の步进、時間方向の
// 再構成、場面への合成、場面へ落とす烟の影の表。解いた域の体積は FluidVolumePass から借りる。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

inline constexpr uint32_t kMaximumRenderedFluidDomains = 8u;
// 域がこの数以下の時は 4 枠の步进（MarchPS）、多い時は 8 枠の步进（MarchWidePS）で描く（Render3D_FluidVolume.hlsl の
// HIKARI_FLUID_COMPACT_DOMAINS）。
inline constexpr uint32_t kCompactMarchFluidDomains = 4u;

// 描く域 1 つ（今の密度と速度、光照体積、格子と世界の位置、外観）。
struct FluidRenderDomain {
    FluidVolume* density = nullptr;
    FluidVolume* velocity = nullptr;
    FluidVolume* light = nullptr;
    // 光照体積を最後に書いた時の入力（null なら毎回書く）と、今の密度の版。
    FluidLightKey* lightKey = nullptr;
    uint64_t densityVersion = 0u;
    FluidGridLayout layout{};
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    FluidDomainSettings settings{};
    MATH::Vec4 colorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
    float simulationSeconds = 0.0f;
    // 最後の子步からこのフレームまでに貯まったゲーム時間 [s]（子步 1 つより短い）。描く時に流れに沿ってこの分だけ密度を
    // 進め、子步の無いフレームで烟が止まって次のフレームで跳ぶのを無くす（解算は固定の子步のまま）。
    float pendingSeconds = 0.0f;
    // 局部空間の域が世界で動く速さ（世界空間の域は 0）。速度体積は域に対する流れなので、粒子が乗る時に足す。
    MATH::Vec3 frameVelocity{};
    // 解算の統計の緩衝（砖块の活動状態を步进が読む）と、その今の状態（読む前に読める状態へ移す）。
    ID3D12Resource* stats = nullptr;
    D3D12_RESOURCE_STATES* statsState = nullptr;
    // 粒子からの注入の緩衝（無ければ 0）と、その格子の半分の分辨率の格の数。
    D3D12_GPU_VIRTUAL_ADDRESS injection = 0u;
    uint32_t injectionCells[3]{};
};

class FluidRenderer final {
public:
    // bindSceneTarget は場面の色と読み取り専用の深度を描画先へ戻す（合成の前に呼ぶ）。policy は步进の步数と歩の上限。
    bool Render(const GFX::Context& context, const FluidRenderView& view, std::span<FluidRenderDomain> domains,
                const FluidQualityPolicy& policy, const std::function<bool()>& bindSceneTarget);
    // 場面へ落とす烟の影の表（雲の影の表と同じ並び。前向き・地形・水面・粒子の面が雲の影と掛ける）を作る。domains は影を
    // 落とす域、toSun は太陽への向き。域が無ければ空の表（descriptor 0）。
    LIGHTING::CloudSolarVisibilityGpuData RenderSolarVisibility(const GFX::Context& context,
                                                                std::span<const FluidRenderDomain> domains,
                                                                const MATH::Vec3& toSun);
    // 粒子が乗る流体：domains（このフレームに解いた域）の世界→格子の行の表を上げ、速度体積を計算 shader が読める状態にする。
    FluidFlowView PrepareFlow(const GFX::Context& context, std::span<FluidRenderDomain> domains);
    // BeginFrame の後に最後に描いた視図の相機体積（無ければ 1 texel の空の体積で、params.w = 0）。
    FluidCameraVolumeView GetCameraVolume() const noexcept;
    // 視図ごとに解算の前に呼ぶ（空の相機体積を用意し、前の視図の相機体積を外す）。
    bool BeginFrame(const GFX::Context& context);
    void Shutdown(const GFX::Context& context);

private:
    // Render3D_FluidVolume.hlsl の根の並び：b0、b1、入力 t0..t28（1 つずつの表、kFirstInput + 入力の番号。域の枠ごとの密度・
    // 速度・光照が 8 つずつ、続いて場面の深度・步进の結果 2 つ・ノイズ・影）、出力 u0（体積）と u1（烟の影の表）。
    static constexpr UINT kConstants = 0u;
    static constexpr UINT kPassConstants = 1u;
    static constexpr UINT kFirstInput = 2u;
    static constexpr UINT kDensity = 0u;
    static constexpr UINT kVelocity = kMaximumRenderedFluidDomains;
    static constexpr UINT kLight = 2u * kMaximumRenderedFluidDomains;
    static constexpr UINT kSceneDepth = 3u * kMaximumRenderedFluidDomains;
    static constexpr UINT kInputColor = kSceneDepth + 1u;
    static constexpr UINT kInputDepth = kSceneDepth + 2u;
    static constexpr UINT kNoise = kSceneDepth + 3u;
    static constexpr UINT kShadow = kSceneDepth + 4u;
    static constexpr UINT kInputCount = kSceneDepth + 5u;
    static constexpr UINT kOutput = kFirstInput + kInputCount;
    static constexpr UINT kSolarOutput = kOutput + 1u;
    // t29〜t36：域ごとの解算の統計の緩衝（砖块の活動状態を步进が読み、動いていない砖块を飛ばす）。
    static constexpr UINT kActivity = kSolarOutput + 1u;
    static constexpr UINT kRootCount = kActivity + kMaximumRenderedFluidDomains;
    // 砖块の活動状態（域ごとの解算の統計の緩衝）を読める状態へ移し、t29〜t36 に束ねる住所を返す（空いた枠は空の緩衝）。
    std::array<D3D12_GPU_VIRTUAL_ADDRESS, kMaximumRenderedFluidDomains> PrepareActivity(
        ID3D12GraphicsCommandList* commands, std::span<FluidRenderDomain> domains, size_t count);
    static constexpr D3D12_RESOURCE_STATES kRead =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    static constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // 步进の 2 枚目：x 止めた不透明面の距離、y 烟の平均距離。
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_R32G32_FLOAT;
    // 体積霧の合成へ渡す烟の覆い（全解像度）：x 烟の平均距離 [m]、y 不透明度。
    static constexpr DXGI_FORMAT kCoverageFormat = DXGI_FORMAT_R16G16_FLOAT;
    static constexpr uint32_t kNoiseSize = 64u;
    // 透明物体の相機体積：画面 16 px ごと、奥行き 32 層（near から対数）、各層 2 標本。
    static constexpr uint32_t kCameraVolumeTile = 16u;
    static constexpr uint32_t kCameraVolumeSlices = 32u;
    static constexpr float kCameraVolumeSamples = 2.0f;
    static constexpr float kCameraVolumeNear = 0.1f;
    // 烟の影の表：太陽に垂直な面の列の数（一辺）と、列の中の深さの段数。図集は 8 枚の横並び × 段数 / 8 行（雲と同じ）。
    static constexpr uint32_t kSolarResolution = 128u;
    static constexpr uint32_t kSolarSteps = 16u;

    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
        RenderResourceView srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };
    struct ViewState {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap{};
        Target marchColor{};
        Target marchDepth{};
        Target coverage{};
        FluidVolume cameraVolume{};
        uint32_t width = 0u;
        uint32_t height = 0u;
        // 最後に描いたフレーム（しばらく描かない視図の資源を捨てる）。
        uint64_t lastFrame = UINT64_MAX;
    };

    // sceneColorFormat が UNKNOWN なら根と計算の PSO だけ（烟の影の表）。
    bool EnsurePipelines(ID3D12Device* device, DXGI_FORMAT sceneColorFormat);
    bool EnsureView(const GFX::Context& context, ViewState& state, uint32_t width, uint32_t height);
    bool EnsureNoise(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS constants);
    void RetireView(const GFX::Context& context, ViewState& state);
    void TransitionTarget(ID3D12GraphicsCommandList* commands, Target& target, D3D12_RESOURCE_STATES state);
    void TransitionVolume(ID3D12GraphicsCommandList* commands, FluidVolume& volume, D3D12_RESOURCE_STATES state);

    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> noise_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> light_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> cameraVolume_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> solar_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> march_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> marchWide_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> coverage_{};
    DXGI_FORMAT compositeFormat_ = DXGI_FORMAT_UNKNOWN;
    bool failed_ = false;
    FluidVolume noiseVolume_{};
    bool noiseReady_ = false;
    FluidVolume fallbackVolume_{};
    // 域の無い活動状態の枠と、步进を使わない pass（烟の影の表）の t29〜t36。
    Microsoft::WRL::ComPtr<ID3D12Resource> fallbackBuffer_{};
    // 烟の影の表（RG32F の 2D 図集）と、前向きが読む無絆定の貼図。
    FluidVolume solarAtlas_{};
    TextureResourceHandle solarTexture_{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    std::unordered_map<uint64_t, ViewState> views_{};
    FluidCameraVolumeView cameraVolumeView_{};
};

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
