#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

// 体積流体の描画（Render3D_FluidVolume.hlsl）：域ごとの光照体積、透明物体のための相機体積、半分辨率の步进、時間方向の
// 再構成、場面への合成。解いた域の体積は FluidVolumePass から借りる。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

inline constexpr uint32_t kMaximumRenderedFluidDomains = 4u;

// 描く域 1 つ（今の密度と速度、光照体積、格子と世界の位置、外観）。
struct FluidRenderDomain {
    FluidVolume* density = nullptr;
    FluidVolume* velocity = nullptr;
    FluidVolume* light = nullptr;
    FluidGridLayout layout{};
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    FluidDomainSettings settings{};
    MATH::Vec4 colorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
    float simulationSeconds = 0.0f;
    // 域を作り直した回数（描く域の組と合わせて履歴の有効性を決める）。
    uint32_t generation = 0u;
};

class FluidRenderer final {
public:
    // bindSceneTarget は場面の色と読み取り専用の深度を描画先へ戻す（合成の前に呼ぶ）。
    bool Render(const GFX::Context& context, const FluidRenderView& view, std::span<FluidRenderDomain> domains,
                uint32_t marchSteps, const std::function<bool()>& bindSceneTarget);
    // BeginFrame の後に最後に描いた視図の相機体積（無ければ 1 texel の空の体積で、params.w = 0）。
    FluidCameraVolumeView GetCameraVolume() const noexcept;
    // 視図ごとに解算の前に呼ぶ（空の相機体積を用意し、前の視図の相機体積を外す）。
    bool BeginFrame(const GFX::Context& context);
    void Shutdown(const GFX::Context& context);

private:
    // Render3D_FluidVolume.hlsl の根の並び：b0、b1、入力 t0..t17（1 つずつの表、kFirstInput + 入力の番号）、出力 u0。
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
    static constexpr UINT kRootCount = kOutput + 1u;
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> march_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> reconstruct_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> composite_{};
    DXGI_FORMAT compositeFormat_ = DXGI_FORMAT_UNKNOWN;
    bool failed_ = false;
    FluidVolume noiseVolume_{};
    bool noiseReady_ = false;
    FluidVolume fallbackVolume_{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads_{};
    std::unordered_map<uint64_t, ViewState> views_{};
    FluidCameraVolumeView cameraVolumeView_{};
};

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
