#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenPass.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPlan.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenTraditionalIndirectView;

    constexpr size_t kDefaultGpuTraditionalCommandCapacity = 4096u;
    constexpr UINT kGpuTraditionalCommandStreamRootConstantCount = 4u;

    constexpr size_t kGpuTraditionalCommandBucketCount = 32u;

    struct GpuTraditionalCommandArgument {
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};
        uint32_t rootConstants[kGpuTraditionalCommandStreamRootConstantCount]{};
        D3D12_DRAW_INDEXED_ARGUMENTS draw{};
        uint32_t reserved0 = 0;
    };

    static_assert(sizeof(GpuTraditionalCommandArgument) == 72u);

    struct GpuTraditionalSkinnedCommandArgument {
        D3D12_VERTEX_BUFFER_VIEW vertexBuffer{};
        D3D12_INDEX_BUFFER_VIEW indexBuffer{};
        uint32_t rootConstants[kGpuTraditionalCommandStreamRootConstantCount]{};
        D3D12_GPU_VIRTUAL_ADDRESS jointPalette = 0;
        D3D12_DRAW_INDEXED_ARGUMENTS draw{};
        uint32_t reserved0 = 0;
    };

    static_assert(sizeof(GpuTraditionalSkinnedCommandArgument) == 80u);

    struct GpuTraditionalCommandStreamStats {
        size_t capacity = 0;
        size_t requestedCommandCount = 0;
        size_t uploadedCommandCount = 0;
        size_t uploadedPayloadCount = 0;
        size_t reusedPayloadCount = 0;
        size_t uploadedStaticSeedCount = 0;
        size_t uploadedSkinnedSeedCount = 0;
        size_t overflowCommandCount = 0;
        size_t missingDrawArgsCommandCount = 0;
        size_t missingJointPaletteCommandCount = 0;
        size_t uploadCallCount = 0;
        size_t uploadedSeedCount = 0;
        size_t gpuBuildDispatchCount = 0;
        size_t gpuCompactedCommandCapacity = 0;
        size_t commandBucketCount = 0;
        size_t inputUploadBytes = 0;
        size_t inputUploadCopyCount = 0;
        bool initialized = false;
        bool commandSignatureReady = false;
        bool skinnedCommandSignatureReady = false;
        bool seedBufferReady = false;
        bool payloadBufferReady = false;
        bool counterBufferReady = false;
        bool gpuCompactionPipelineReady = false;
        bool gpuCompactionReady = false;
        bool gpuCounterBacked = false;
        bool reusedResidentInput = false;
        D3D12_GPU_VIRTUAL_ADDRESS argumentBufferAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skinnedArgumentBufferAddress = 0;
        UINT commandStride = 0;
        UINT skinnedCommandStride = 0;
    };

    class GpuTraditionalCommandStreamBuffer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* rootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount,
            size_t capacity = kDefaultGpuTraditionalCommandCapacity);
        bool InitializeSkinnedCommandStream(
            ID3D12Device* device,
            ID3D12RootSignature* skinnedRootSignature,
            UINT rootConstantParameterIndex,
            UINT jointPaletteParameterIndex);

        void BeginFrame(uint32_t frameIndex);
        void ResetFrame();
        bool PrepareCommandInputs(
            uintptr_t sourceIdentity,
            uint64_t layoutVersion,
            uint64_t sourceVersion);
        void UploadCommandSeeds(
            const GpuDrivenTraditionalIndirectView& view);
        bool BuildGpuCompactedCommands(
            ID3D12GraphicsCommandList* commandList,
            const MATH::Mat4& viewProj,
            bool enableFrustumCull = true);

        ID3D12Resource* GetArgumentBuffer() const;
        ID3D12Resource* GetSkinnedArgumentBuffer() const;
        ID3D12Resource* GetCounterBuffer() const;
        ID3D12CommandSignature* GetCommandSignature() const;
        ID3D12CommandSignature* GetSkinnedCommandSignature() const;
        UINT64 GetCommandCounterOffset() const;
        UINT64 GetCommandCounterOffset(GpuDrivenPassKind pass) const;
        UINT64 GetSkinnedCommandCounterOffset() const;
        UINT64 GetSkinnedCommandCounterOffset(GpuDrivenPassKind pass) const;
        UINT64 GetArgumentBufferOffset(GpuDrivenPassKind pass) const;
        UINT64 GetSkinnedArgumentBufferOffset(GpuDrivenPassKind pass) const;
        UINT64 GetArgumentBucketStride() const;
        UINT64 GetSkinnedArgumentBucketStride() const;
        UINT64 GetCounterBucketStride() const;
        size_t GetCommandBucketCapacity() const;
        size_t GetCommandBucketCount() const;
        size_t GetUploadedSeedCount() const;
        size_t GetUploadedSkinnedSeedCount() const;
        bool HasGpuCompactedCommands() const;
        const GpuTraditionalCommandStreamStats& GetStats() const;

    private:
        struct FrameResources {
            Microsoft::WRL::ComPtr<ID3D12Resource> argumentBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> skinnedArgumentBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> seedBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> seedUploadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> payloadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> payloadUploadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> constantsUploadBuffer;
            std::byte* seedMapped = nullptr;
            std::byte* payloadMapped = nullptr;
            std::byte* counterResetMapped = nullptr;
            std::byte* constantsMapped = nullptr;
            D3D12_RESOURCE_STATES argumentBufferState =
                D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES skinnedArgumentBufferState =
                D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES seedBufferState =
                D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES payloadBufferState =
                D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES counterBufferState =
                D3D12_RESOURCE_STATE_COMMON;
            GpuTraditionalCommandStreamStats residentInputStats{};
            uintptr_t inputSourceIdentity = 0;
            uint64_t inputLayoutVersion = 0;
            uint64_t inputSourceVersion = 0;
            size_t residentSeedCount = 0;
            size_t residentPayloadCount = 0;
            bool inputResident = false;
        };

        void BindFrameResources(uint32_t frameIndex);
        void StoreActiveFrameResourceStates();

        Microsoft::WRL::ComPtr<ID3D12Resource> argumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> skinnedArgumentBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> seedBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> seedUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> payloadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> payloadUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> counterResetUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> constantsUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> compactPipelineState_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> commandSignature_;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> skinnedCommandSignature_;
        std::byte* seedMapped_ = nullptr;
        std::byte* payloadMapped_ = nullptr;
        std::byte* counterResetMapped_ = nullptr;
        std::byte* constantsMapped_ = nullptr;
        size_t capacity_ = 0;
        size_t seedCursor_ = 0;
        size_t payloadCursor_ = 0;
        UINT rootConstantCount_ = 0;
        D3D12_RESOURCE_STATES argumentBufferState_ =
            D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES skinnedArgumentBufferState_ =
            D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES seedBufferState_ =
            D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES payloadBufferState_ =
            D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES counterBufferState_ =
            D3D12_RESOURCE_STATE_COMMON;
        std::array<FrameResources, GFX::kFrameResourceCount> frameResources_{};
        uint32_t activeFrameResourceIndex_ = 0;
        uintptr_t pendingInputSourceIdentity_ = 0;
        uint64_t pendingInputLayoutVersion_ = 0;
        uint64_t pendingInputSourceVersion_ = 0;
        bool inputUploadPending_ = false;
        std::unordered_map<uint64_t, size_t> payloadIndexByCommandKey_{};
        GpuTraditionalCommandStreamStats stats_{};
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
