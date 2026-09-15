#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Render3D/Material/HIKARI_GpuMaterialSnapshot.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportContract.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"

namespace HIKARI::GFX {
    struct Context;
}

namespace HIKARI::TOOLS::BAKING {

    enum class ProgressiveLightmapBakeState : uint8_t {
        Idle,
        Sampling,
        ReadbackPending,
        Completed,
        Failed,
    };

    struct ProgressiveLightmapBakeStatus {
        ProgressiveLightmapBakeState state =
            ProgressiveLightmapBakeState::Idle;
        uint32_t completedSamples = 0u;
        uint32_t requestedSamples = 0u;
        uint32_t coveredTexelCount = 0u;
        uint32_t overlapTexelCount = 0u;
        std::string message{};
    };

    class DxrProgressiveLightmapBaker final {
    public:
        ~DxrProgressiveLightmapBaker();
        RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportBackendCapabilities
            GetCapabilities(
                const GFX::Context& graphics) const noexcept;

        bool PrepareScene(
            const GFX::Context& graphics,
            const RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportScene& scene,
            std::string* outMessage = nullptr);

        bool BeginTarget(
            const GFX::Context& graphics,
            const RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportScene& scene,
            const RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportLightmapPage& target,
            const RENDER3D::LIGHTING::TRANSPORT::
                LightingTransportBakeSettings& settings,
            std::string* outMessage = nullptr);

        bool RecordSamples(
            const GFX::Context& graphics,
            uint32_t maximumSamplesThisFrame = 4u);
        bool QueueReadback(
            const GFX::Context& graphics,
            std::string* outMessage = nullptr);
        bool TrySave(
            const GFX::Context& graphics,
            const std::filesystem::path& outputPath,
            std::string* outMessage = nullptr);

        const ProgressiveLightmapBakeStatus&
            GetStatus() const noexcept;
        void ResetTarget();
        void Reset();

        // Frozen CPU/GPU transfer records. Public only so translation-unit
        // helpers can validate their exact StructuredBuffer layout.
        struct GpuTriangle;
        struct GpuLight;
        struct DispatchConstants;

    private:
        bool CreatePipeline(
            ID3D12Device* device,
            std::string* outMessage);
        bool CreateTargetResources(
            ID3D12Device* device,
            const void* texelData,
            size_t texelByteSize,
            std::string* outMessage);
        bool SaveReadbackDds(
            const std::filesystem::path& outputPath,
            std::string* outMessage);

        GFX::RAYTRACING::DxrTriangleAccelerationStructure
            accelerationStructure_{};
        RENDER3D::MATERIAL::GpuMaterialSnapshot materialSnapshot_{};
        RENDER3D::TextureResourceSnapshot textureSnapshot_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature>
            rootSignature_{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState>
            pipelineState_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            triangleBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            lightBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> atmosphereBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            texelBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            outputBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            outputInitializationBuffer_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>
            readbackBuffer_{};

        RENDER3D::LIGHTING::TRANSPORT::
            LightingTransportEnvironment environment_{};
        std::vector<uint8_t> validTexels_{};
        std::vector<uint32_t> texelOwnerIndices_{};
        std::vector<ASSETS::LIGHTING::
            LightmapAtlasAllocation> atlasAllocations_{};
        ProgressiveLightmapBakeStatus status_{};
        uint32_t width_ = 0u;
        uint32_t height_ = 0u;
        uint32_t bounceCount_ = 0u;
        uint32_t triangleCount_ = 0u;
        uint32_t lightCount_ = 0u;
        uint32_t skyDescriptorIndex_ = UINT32_MAX;
        uint64_t readbackFenceValue_ = 0u;
        bool scenePrepared_ = false;
    };

} // namespace HIKARI::TOOLS::BAKING
