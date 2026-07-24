#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpaceGeometryAux.h"

namespace HIKARI::RENDER3D::SCREENSPACE {

    struct SsaoDebugState {
        bool enabled = false;
        bool valid = false;
        bool suppressed = false;
        SsaoMode mode = SsaoMode::Off;
        bool geometryAuxEnabled = false;
        bool geometryAuxWritten = false;
        DXGI_FORMAT geometryAuxFormat = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t internalWidth = 0;
        uint32_t internalHeight = 0;
        bool halfResolution = false;
        uint32_t referenceSampleCount = 0;
        uint32_t referenceBlurIterations = 0;
        uint32_t sampleCount = 0;
        uint32_t blurIterations = 0;
        float radius = 0.0f;
        float strength = 0.0f;
        float power = 0.0f;
        bool pixMarkersAvailable = false;
        bool gpuTimingAvailable = false;
        float geometryAuxCpuMs = 0.0f;
        float mainCpuMs = 0.0f;
        float blurCpuMs = 0.0f;
        float compositeCpuMs = 0.0f;
        float totalCpuMs = 0.0f;
    };

    class SsaoRenderer {
    public:
        bool Render(
            ID3D12GraphicsCommandList* cmd,
            const ScreenSpaceGeometryAux& geometryAux,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
            const MESHRENDERER::CameraCB& camera,
            const AmbientOcclusionSettings& settings);
        bool RenderDepthOnly(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
            const MESHRENDERER::CameraCB& camera,
            const AmbientOcclusionSettings& settings);

        void Release();
        void RecordSkipped(
            uint32_t width,
            uint32_t height,
            const AmbientOcclusionSettings& settings);

        D3D12_GPU_DESCRIPTOR_HANDLE GetAoSrv() const { return lastAoSrv_; }
        bool IsValid() const { return valid_; }

    private:
        bool EnsureResources(uint32_t width, uint32_t height, bool halfResolution);
        bool EnsurePipeline();
        bool CreateAoResource(
            uint32_t width,
            uint32_t height,
            GFX::DESCRIPTOR::SystemSrv srvSlot,
            Microsoft::WRL::ComPtr<ID3D12Resource>& outResource,
            D3D12_CPU_DESCRIPTOR_HANDLE& outRtv,
            D3D12_CPU_DESCRIPTOR_HANDLE& outSrvCpu,
            D3D12_GPU_DESCRIPTOR_HANDLE& outSrvGpu,
            const wchar_t* debugName);

        void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES nextState);
        void DrawFullscreen(ID3D12GraphicsCommandList* cmd);
        bool RenderInternal(
            ID3D12GraphicsCommandList* cmd,
            uint32_t width,
            uint32_t height,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSrv,
            const MESHRENDERER::CameraCB& camera,
            const AmbientOcclusionSettings& settings,
            bool depthOnlyNormals);

        Microsoft::WRL::ComPtr<ID3D12RootSignature> generateRootSig_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> depthOnlyGenerateRootSig_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> generatePso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> optimizedGeneratePso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthOnlyGeneratePso_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRootSig_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> depthOnlyBlurRootSig_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> upsamplePso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthOnlyBlurPso_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthOnlyUpsamplePso_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_{};
        uint8_t* constantMapped_ = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> rawAo_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> blurredAo_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resolvedAo_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_{};
        D3D12_CPU_DESCRIPTOR_HANDLE rawRtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE blurredRtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE resolvedRtv_{};
        D3D12_CPU_DESCRIPTOR_HANDLE rawSrvCpu_{};
        D3D12_CPU_DESCRIPTOR_HANDLE blurredSrvCpu_{};
        D3D12_CPU_DESCRIPTOR_HANDLE resolvedSrvCpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE rawSrvGpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE blurredSrvGpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE resolvedSrvGpu_{};
        D3D12_GPU_DESCRIPTOR_HANDLE lastAoSrv_{};
        D3D12_RESOURCE_STATES rawState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES blurredState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES resolvedState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        uint32_t screenWidth_ = 0;
        uint32_t screenHeight_ = 0;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        bool halfResolution_ = false;
        bool valid_ = false;
    };

    const SsaoDebugState& GetSsaoDebugState();
    SsaoMode ResolveEffectiveSsaoMode(const AmbientOcclusionSettings& settings);
    bool SsaoRequiresGeometryAux(const AmbientOcclusionSettings& settings);
    void BeginSsaoDebugFrame(uint32_t width, uint32_t height, const AmbientOcclusionSettings& settings);
    void RecordSsaoGeometryAuxDebug(bool written, float cpuMs, DXGI_FORMAT format);
    void RecordSsaoCompositeDebug(float cpuMs);

} // namespace HIKARI::RENDER3D::SCREENSPACE
