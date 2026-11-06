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

inline constexpr uint32_t kMaximumRenderedFluidDomains = 4u;

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
    // 局部空間の域が世界で動く速さ（世界空間の域は 0）。速度体積は域に対する流れなので、粒子が乗る時に足す。
    MATH::Vec3 frameVelocity{};
    // 域を作り直した回数（描く域の組と合わせて履歴の有効性を決める）。
    uint32_t generation = 0u;
};

class FluidRenderer final {
public:
    // bindSceneTarget は場面の色と読み取り専用の深度を描画先へ戻す（合成の前に呼ぶ）。
    bool Render(const GFX::Context& context, const FluidRenderView& view, std::span<FluidRenderDomain> domains,
                uint32_t marchSteps, const std::function<bool()>& bindSceneTarget);
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
    // Render3D_FluidVolume.hlsl の根の並び：b0、b1、入力 t0..t17（1 つずつの表、kFirstInput + 入力の番号）、出力 u0（体積）
    // と u1（烟の影の表）。
    static constexpr UINT kConstants = 0u;
    static constexpr UINT kPassConstants = 1u;
    static constexpr UINT kFirstInput = 2u;
    static constexpr UINT kDensity = 0u;
    static constexpr UINT kVelocity = 4u;
    static constexpr UINT kLight = 8u;
    static constexpr UINT kSceneDepth = 12u;
    static constexpr UINT kInputColor = 13u;
    static constexpr UINT kInputDepth = 14u;
    static constexpr UINT kHistoryColor = 15u;
    static constexpr UINT kNoise = 16u;
    static constexpr UINT kShadow = 17u;
    static constexpr UINT kInputCount = 18u;
    static constexpr UINT kOutput = kFirstInput + kInputCount;
    static constexpr UINT kSolarOutput = kOutput + 1u;
    static constexpr UINT kRootCount = kSolarOutput + 1u;
    static constexpr D3D12_RESOURCE_STATES kRead =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    static constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_R32G32_FLOAT;
    static constexpr uint32_t kNoiseSize = 64u;
    // 透明物体の相機体積：画面 16 px ごと、奥行き 32 層（near から対数）、各層 2 標本。
    static constexpr uint32_t kCameraVolumeTile = 16u;
    static constexpr uint32_t kCameraVolumeSlices = 32u;
    static constexpr float kCameraVolumeSamples = 2.0f;
    static constexpr float kCameraVolumeNear = 0.1f;
    static constexpr float kHistoryWeight = 0.85f;
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
        std::array<Target, 2> history{};
        FluidVolume cameraVolume{};
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t historyRead = 0u;
        uint64_t lastFrame = UINT64_MAX;
        // 前のフレームにこの視図を描いた行列（履歴の再投影）と、描いた域の組（域ごとに変わらない光照体積と作り直しの回数）。
        MATH::Mat4 previousViewProj{};
        uint64_t domainSignature = 0u;
        bool historyValid = false;
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> reconstruct_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_{};
    DXGI_FORMAT compositeFormat_ = DXGI_FORMAT_UNKNOWN;
    bool failed_ = false;
    FluidVolume noiseVolume_{};
    bool noiseReady_ = false;
    FluidVolume fallbackVolume_{};
    // 烟の影の表（RG32F の 2D 図集）と、前向きが読む無絆定の貼図。
    FluidVolume solarAtlas_{};
    TextureResourceHandle solarTexture_{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    std::unordered_map<uint64_t, ViewState> views_{};
    FluidCameraVolumeView cameraVolumeView_{};
};

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
