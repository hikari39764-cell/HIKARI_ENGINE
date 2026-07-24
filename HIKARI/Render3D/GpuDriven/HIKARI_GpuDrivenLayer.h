#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrameContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenProducer.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuDrivenSceneSource;
    class IGpuDrivenProducer;

    struct GpuDrivenSceneResidency {
        bool resident = false;
        uint64_t layoutVersion = 0;
        uint64_t sourceVersion = 0;
        size_t instanceCount = 0;

        void Reset();
    };

    struct GpuDrivenSceneUploadStats {
        size_t sourceInstanceCount = 0;
        bool sceneResident = false;
        bool reusedResidentFrame = false;
        bool patchedDirtyRanges = false;
        bool uploadedFullScene = false;
        std::array<uint32_t, kGpuDrivenPassCount> passInstanceCounts{};
        SurfaceGpuSceneFrameBufferStats bufferStats{};
    };

    struct GpuDrivenSceneUploadDesc {
        ID3D12GraphicsCommandList* commandList = nullptr;
        GpuDrivenSceneResidency* residency = nullptr;
        const uint32_t* materialSlotBySourceRecord = nullptr;
        size_t materialSourceRecordCount = 0;
        uint64_t materialBindingVersion = 0;
        uint32_t frameIndex = 0;
        bool allowDirtyRangePatching = true;
    };

    struct GpuDrivenCommandFrameDesc {
        ID3D12GraphicsCommandList* commandList = nullptr;
        const MATH::Mat4* cullViewProj = nullptr;
        uint32_t frameIndex = 0;
        bool resetTraditionalIndirectBuffer = true;
        bool publishCommandBuffers = true;
        bool enableSurfaceFrustumCull = true;
    };

    struct GpuDrivenCommandFrameStats {
        bool commandFramePublished = false;
        GpuTraditionalCommandStreamStats traditionalCommandStreamStats{};
    };

    class GpuDrivenLayer final {
    public:
        bool Initialize(
            ID3D12Device* device,
            ID3D12RootSignature* staticRootSignature,
            UINT rootConstantParameterIndex,
            UINT rootConstantCount);

        void Attach(
            SurfaceGpuSceneFrameBuffer* sceneBuffer,
            GpuTraditionalCommandStreamBuffer* traditionalCommandStreamBuffer,
            IGpuDrivenProducer* producer);

        void ResetFrame();
        bool BeginFrame(const GpuDrivenSceneSource* source);
        const GpuDrivenSceneUploadStats& UploadSceneFrame(
            const GpuDrivenSceneUploadDesc& desc);
        void UploadSurfaceGpuSceneFrame(
            uint32_t instanceCount,
            uint32_t opaqueBaseIndex,
            uint32_t depthPrepassBaseIndex,
            uint32_t depthAwareBaseIndex,
            uint32_t transparentBaseIndex,
            uint32_t shadowBaseIndex,
            bool sceneResident);
        void CommitSurfaceGpuSceneMaterialFrame(ID3D12GraphicsCommandList* commandList);
        void ImportProducerOutput(
            const GpuDrivenProducerFrameOutput& output);
        void SetBackendAvailability(
            const GpuDrivenBackendAvailability& availability);
        const GpuDrivenCommandFrameStats& BuildCommandFrame(
            const GpuDrivenCommandFrameDesc& desc);
        void BuildCommandBuffers();

        bool IsBackendConsumable(
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;
        bool IsPassGpuReady(GpuDrivenPassKind pass) const;
        GeometryBackendExecutionPlan GetPassExecutionPlan(
            GpuDrivenPassKind pass) const;
        GeometryBackendContext BuildGeometryBackendContext(
            ID3D12GraphicsCommandList* commandList,
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const;

        const GpuDrivenPassExecutionState& GetPassExecutionState(
            GpuDrivenPassKind pass) const;
        SurfaceGpuSceneFrameBuffer* GetSceneBuffer() const;
        GpuTraditionalCommandStreamBuffer* GetTraditionalCommandStreamBuffer() const;
        IGpuDrivenProducer* GetProducer() const;
        const GpuDrivenFrameContext& GetFrameContext() const;
        const GpuDrivenDrawCommandStream& GetDrawCommandStream() const;
        const GpuDrivenSceneUploadStats& GetSceneUploadStats() const;
        const GpuDrivenCommandFrameStats& GetCommandFrameStats() const;

    private:
        // 同一フレーム内で BuildCommandFrame が複数回呼ばれても、入力が
        // 変わらない限り伝統ストリームの再アップロード + GPU 圧縮を繰り返さ
        // ないための冪等キー。ResetFrame / ソース差し替えで無効化される。
        struct TraditionalCommandFrameKey {
            bool valid = false;
            uint32_t frameIndex = 0;
            const GpuDrivenSceneSource* source = nullptr;
            uint64_t sourceVersion = 0;
            uint64_t layoutVersion = 0;
            bool hasCullViewProj = false;
            bool enableSurfaceFrustumCull = false;
            MATH::Mat4 cullViewProj{};
        };

        SurfaceGpuSceneFrameBuffer* sceneBuffer_ = nullptr;
        GpuTraditionalCommandStreamBuffer* traditionalCommandStreamBuffer_ = nullptr;
        IGpuDrivenProducer* producer_ = nullptr;
        const GpuDrivenSceneSource* frameSource_ = nullptr;
        GpuDrivenFrameContext frameContext_{};
        GpuDrivenSceneUploadStats sceneUploadStats_{};
        GpuDrivenCommandFrameStats commandFrameStats_{};
        TraditionalCommandFrameKey traditionalCommandFrameKey_{};

        void InitializePassExecutionStates(
            const GpuDrivenSceneSource* source);
        void RefreshPassExecutionStates();
        void RebuildDrawCommandStream();
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
