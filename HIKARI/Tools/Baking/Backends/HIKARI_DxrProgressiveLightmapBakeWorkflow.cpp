#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"

#include <algorithm>
#include <utility>

#include "Gfx/HIKARI_GfxContext.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapGpuData.h"

namespace HIKARI::TOOLS::BAKING {

    namespace TRANSPORT =
        RENDER3D::LIGHTING::TRANSPORT;

    namespace {

        using enum INTERNAL::DxrLightmapRootParameter;

        void SetMessage(
            std::string* outMessage,
            const std::string& message) {
            if (outMessage != nullptr) {
                *outMessage = message;
            }
        }

        MATH::Vec4 ToPosition4(const MATH::Vec3& value) {
            return { value.x, value.y, value.z, 1.0f };
        }

    } // namespace

    bool DxrProgressiveLightmapBaker::BeginTarget(
        const GFX::Context& graphics,
        const TRANSPORT::LightingTransportScene& scene,
        const TRANSPORT::LightingTransportSurfaceTarget& target,
        const TRANSPORT::LightingTransportBakeSettings& settings,
        std::string* outMessage) {

        ResetTarget();
        if (!scenePrepared_ ||
            !target.eligible ||
            target.triangleIndices.empty()) {
            status_.state =
                ProgressiveLightmapBakeState::Failed;
            status_.message =
                "Lightmap target is not eligible.";
            SetMessage(outMessage, status_.message);
            return false;
        }

        width_ = std::clamp(
            settings.outputResolution,
            32u,
            ASSETS::LIGHTING::
                kSurfaceLightmapMaximumAtlasResolution);
        height_ = width_;
        bounceCount_ = std::clamp(
            settings.bounceCount,
            1u,
            4u);
        status_.requestedSamples = std::clamp(
            settings.sampleCount,
            1u,
            4096u);
        INTERNAL::DxrLightmapUvRasterization rasterization =
            INTERNAL::RasterizeLightmapTarget(
                scene,
                target,
                width_,
                height_);
        validTexels_ =
            std::move(rasterization.validTexels);
        texelOwnerIndices_ =
            std::move(rasterization.ownerIndices);
        atlasAllocations_ =
            target.atlasAllocations;
        status_.coveredTexelCount =
            rasterization.coveredTexelCount;
        status_.overlapTexelCount =
            rasterization.overlapTexelCount;
        if (status_.coveredTexelCount == 0u ||
            rasterization.uncoveredAllocationCount > 0u ||
            status_.overlapTexelCount > 0u) {
            status_.state =
                ProgressiveLightmapBakeState::Failed;
            status_.message =
                status_.coveredTexelCount == 0u
                ? "UV1 rasterization covered no texels."
                : rasterization.uncoveredAllocationCount > 0u
                ? "One or more Lightmap surface allocations "
                  "covered no texels."
                : "UV1 contains overlapping islands.";
            SetMessage(outMessage, status_.message);
            return false;
        }
        if (!CreateTargetResources(
                graphics.device,
                rasterization.texels.data(),
                rasterization.texels.size() *
                    sizeof(INTERNAL::DxrLightmapSurfaceTexel),
                outMessage)) {
            status_.state =
                ProgressiveLightmapBakeState::Failed;
            return false;
        }

        graphics.cmdList->CopyBufferRegion(
            outputBuffer_.Get(),
            0u,
            outputInitializationBuffer_.Get(),
            0u,
            static_cast<UINT64>(width_) *
                height_ *
                sizeof(MATH::Vec4));
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource =
            outputBuffer_.Get();
        barrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        graphics.cmdList->ResourceBarrier(1u, &barrier);

        status_.state =
            ProgressiveLightmapBakeState::Sampling;
        status_.message =
            "Progressive DXR sampling started.";
        SetMessage(outMessage, status_.message);
        return true;
    }

    bool DxrProgressiveLightmapBaker::RecordSamples(
        const GFX::Context& graphics,
        uint32_t maximumSamplesThisFrame) {

        if (status_.state !=
                ProgressiveLightmapBakeState::Sampling ||
            graphics.cmdList == nullptr ||
            pipelineState_ == nullptr ||
            rootSignature_ == nullptr) {
            return false;
        }

        ID3D12DescriptorHeap* heaps[]{materialSnapshot_.GetTextureHeap()};
        graphics.cmdList->SetDescriptorHeaps(1, heaps);
        graphics.cmdList->SetComputeRootSignature(
            rootSignature_.Get());
        graphics.cmdList->SetPipelineState(
            pipelineState_.Get());
        graphics.cmdList->SetComputeRootShaderResourceView(
            kRootScene,
            accelerationStructure_.
                GetTopLevelGpuAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(
            kRootTriangles,
            triangleBuffer_->GetGPUVirtualAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(
            kRootTexels,
            texelBuffer_->GetGPUVirtualAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(
            kRootLights,
            lightBuffer_->GetGPUVirtualAddress());
        graphics.cmdList->SetComputeRootUnorderedAccessView(
            kRootOutput,
            outputBuffer_->GetGPUVirtualAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(kRootMaterials, materialSnapshot_.GetMaterialAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(kRootParameters, materialSnapshot_.GetParameterAddress());
        graphics.cmdList->SetComputeRootShaderResourceView(kRootResources, materialSnapshot_.GetResourceAddress());
        graphics.cmdList->SetComputeRootDescriptorTable(kRootTextures, heaps[0]->GetGPUDescriptorHandleForHeapStart());

        const uint32_t sampleEnd = (std::min)(
            status_.requestedSamples,
            status_.completedSamples +
                (std::max)(1u, maximumSamplesThisFrame));
        while (status_.completedSamples < sampleEnd) {
            DispatchConstants constants{};
            constants.width = width_;
            constants.height = height_;
            constants.sampleIndex =
                status_.completedSamples;
            constants.bounceCount = bounceCount_;
            constants.triangleCount = triangleCount_;
            constants.lightCount = lightCount_;
            constants.ambientSkyStrength = {
                environment_.ambientRadiance.x,
                environment_.ambientRadiance.y,
                environment_.ambientRadiance.z,
                environment_.skyStrength
            };
            constants.skyZenith = ToPosition4(
                environment_.skyZenithRadiance);
            constants.skyHorizon = ToPosition4(
                environment_.skyHorizonRadiance);
            constants.skyGround = ToPosition4(
                environment_.skyGroundRadiance);
            graphics.cmdList->SetComputeRoot32BitConstants(
                kRootConstants,
                sizeof(constants) / sizeof(uint32_t),
                &constants,
                0u);
            graphics.cmdList->Dispatch(
                (width_ + 7u) / 8u,
                (height_ + 7u) / 8u,
                1u);
            D3D12_RESOURCE_BARRIER uavBarrier{};
            uavBarrier.Type =
                D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource =
                outputBuffer_.Get();
            graphics.cmdList->ResourceBarrier(
                1u,
                &uavBarrier);
            ++status_.completedSamples;
        }
        // 後続の Editor 描画は通常の texture heap と自身の root table を再バインドする。
        if (auto* textureHeap = RENDER3D::GetTextureResourceSrvHeap())
            graphics.cmdList->SetDescriptorHeaps(1, &textureHeap);
        return true;
    }

    bool DxrProgressiveLightmapBaker::QueueReadback(
        const GFX::Context& graphics,
        std::string* outMessage) {

        if (status_.state !=
                ProgressiveLightmapBakeState::Sampling ||
            status_.completedSamples <
                status_.requestedSamples) {
            SetMessage(
                outMessage,
                "DXR lightmap samples are not complete.");
            return false;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource =
            outputBuffer_.Get();
        barrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        graphics.cmdList->ResourceBarrier(1u, &barrier);
        graphics.cmdList->CopyBufferRegion(
            readbackBuffer_.Get(),
            0u,
            outputBuffer_.Get(),
            0u,
            static_cast<UINT64>(width_) *
                height_ *
                sizeof(MATH::Vec4));
        readbackFenceValue_ =
            graphics.currentFrameRetireFenceValue;
        status_.state =
            ProgressiveLightmapBakeState::ReadbackPending;
        status_.message =
            "DXR lightmap readback queued.";
        SetMessage(outMessage, status_.message);
        return true;
    }

    bool DxrProgressiveLightmapBaker::TrySave(
        const GFX::Context& graphics,
        const std::filesystem::path& outputPath,
        std::string* outMessage) {

        if (status_.state !=
            ProgressiveLightmapBakeState::ReadbackPending) {
            return status_.state ==
                ProgressiveLightmapBakeState::Completed;
        }
        if (graphics.frameFence == nullptr ||
            graphics.frameFence->GetCompletedValue() <
                readbackFenceValue_) {
            return false;
        }
        if (!SaveReadbackDds(outputPath, outMessage)) {
            status_.state =
                ProgressiveLightmapBakeState::Failed;
            status_.message =
                outMessage != nullptr
                ? *outMessage
                : "Failed to save DXR lightmap.";
            return false;
        }
        status_.state =
            ProgressiveLightmapBakeState::Completed;
        status_.message =
            "Progressive DXR lightmap saved.";
        return true;
    }

    const ProgressiveLightmapBakeStatus&
        DxrProgressiveLightmapBaker::GetStatus() const noexcept {
        return status_;
    }

} // namespace HIKARI::TOOLS::BAKING
