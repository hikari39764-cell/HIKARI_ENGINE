#include "Tools/Baking/Backends/HIKARI_DxrProgressiveLightmapBaker.h"
#include "Core/Text/HIKARI_OutMessage.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapBakeUtilities.h"
#include "Tools/Baking/Backends/Internal/HIKARI_DxrLightmapGpuData.h"

namespace HIKARI::TOOLS::BAKING {

namespace TRANSPORT = RENDER3D::LIGHTING::TRANSPORT;

namespace {

using enum INTERNAL::DxrLightmapRootParameter;

MATH::Vec4 ToPosition4(const MATH::Vec3& value) { return {value.x, value.y, value.z, 1.0f}; }

} // namespace

bool DxrProgressiveLightmapBaker::BeginTarget(const GFX::Context& graphics,
                                              const TRANSPORT::LightingTransportScene& scene,
                                              const TRANSPORT::LightingTransportLightmapPage& target,
                                              const TRANSPORT::LightingTransportBakeSettings& settings,
                                              std::string* outMessage) {
    ResetTarget();
    if (!scenePrepared_ || target.triangleIndices.empty() || target.atlasAllocations.empty() ||
        target.uvSet != ASSETS::LIGHTING::kSurfaceLightmapUvSet ||
        target.outputResolution != settings.outputResolution || target.outputResolution < 32u ||
        target.outputResolution > ASSETS::LIGHTING::kSurfaceLightmapMaximumAtlasResolution) {
        status_.state = ProgressiveLightmapBakeState::Failed;
        status_.message = "Lightmap page is empty or its UV/resolution contract does not match the bake settings.";
        SetMessage(outMessage, status_.message);
        return false;
    }

    width_ = target.outputResolution;
    height_ = width_;
    bounceCount_ = std::clamp(settings.bounceCount, 1u, ASSETS::LIGHTING::kSurfaceLightmapMaximumBounceCount);
    status_.requestedSamples = std::clamp(settings.sampleCount, 1u, 4096u);
    INTERNAL::DxrLightmapUvRasterization rasterization =
        INTERNAL::RasterizeLightmapTarget(scene, target, width_, height_);
    validTexels_ = std::move(rasterization.validTexels);
    texelOwnerIndices_ = std::move(rasterization.ownerIndices);
    atlasAllocations_ = target.atlasAllocations;
    status_.coveredTexelCount = rasterization.coveredTexelCount;
    status_.overlapTexelCount = rasterization.overlapTexelCount;
    if (status_.coveredTexelCount == 0u || rasterization.uncoveredAllocationCount > 0u ||
        status_.overlapTexelCount > 0u) {
        status_.state = ProgressiveLightmapBakeState::Failed;
        status_.message = status_.coveredTexelCount == 0u               ? "UV1 rasterization covered no texels."
                          : rasterization.uncoveredAllocationCount > 0u ? "One or more Lightmap surface allocations "
                                                                          "covered no texels."
                                                                        : "UV1 contains overlapping islands.";
        SetMessage(outMessage, status_.message);
        return false;
    }
    return StartSampling(graphics, rasterization.texels.data(),
                         rasterization.texels.size() * sizeof(INTERNAL::DxrLightmapSurfaceTexel), outMessage);
}

bool DxrProgressiveLightmapBaker::BeginProbeTarget(const GFX::Context& graphics, std::span<const MATH::Vec3> positions,
                                                   float maximumDistance,
                                                   const TRANSPORT::LightingTransportBakeSettings& settings,
                                                   std::string* outMessage) {
    ResetTarget();
    // 一回の dispatch の高さ（探針の数）は 65535 group × 8 まで。
    if (!scenePrepared_ || positions.empty() || positions.size() > 65535u * 8u || !std::isfinite(maximumDistance) ||
        maximumDistance <= 0.0f ||
        !std::all_of(positions.begin(), positions.end(), [](const MATH::Vec3& p) { return MATH::IsFinite(p); })) {
        status_.state = ProgressiveLightmapBakeState::Failed;
        status_.message = "Probe positions or visibility distance are invalid.";
        SetMessage(outMessage, status_.message);
        return false;
    }
    width_ = kDxrProbeDirectionCount;
    height_ = static_cast<uint32_t>(positions.size());
    bounceCount_ = std::clamp(settings.bounceCount, 1u, ASSETS::LIGHTING::kSurfaceLightmapMaximumBounceCount);
    status_.requestedSamples = std::clamp(settings.sampleCount, 1u, 4096u);
    probeMaximumDistance_ = maximumDistance;
    std::vector<INTERNAL::DxrLightmapSurfaceTexel> texels(static_cast<size_t>(width_) * height_);
    for (size_t probe = 0; probe < positions.size(); ++probe)
        for (uint32_t direction = 0; direction < width_; ++direction)
            texels[probe * width_ + direction] = {ToPosition4(positions[probe]), {0.0f, 0.0f, 1.0f, 1.0f}};
    status_.coveredTexelCount = static_cast<uint32_t>(texels.size());
    return StartSampling(graphics, texels.data(), texels.size() * sizeof(texels[0]), outMessage);
}

bool DxrProgressiveLightmapBaker::RecordSamples(const GFX::Context& graphics, uint32_t maximumSamplesThisFrame) {
    if (status_.state != ProgressiveLightmapBakeState::Sampling || graphics.cmdList == nullptr ||
        pipelineState_ == nullptr || rootSignature_ == nullptr) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[]{textureSnapshot_.GetHeap()};
    graphics.cmdList->SetDescriptorHeaps(1, heaps);
    graphics.cmdList->SetComputeRootSignature(rootSignature_.Get());
    graphics.cmdList->SetPipelineState(pipelineState_.Get());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootScene, accelerationStructure_.GetTopLevelGpuAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootTriangles, triangleBuffer_->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootTexels, texelBuffer_->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootLights, lightBuffer_->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootUnorderedAccessView(kRootOutput, outputBuffer_->GetGPUVirtualAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootMaterials, materialSnapshot_.GetMaterialAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootParameters, materialSnapshot_.GetParameterAddress());
    graphics.cmdList->SetComputeRootShaderResourceView(kRootResources, materialSnapshot_.GetResourceAddress());
    graphics.cmdList->SetComputeRootDescriptorTable(kRootTextures, heaps[0]->GetGPUDescriptorHandleForHeapStart());
    const uint32_t cubeIndex = environment_.skyMode == SkyMode::Cubemap
                                   ? skyDescriptorIndex_
                                   : RENDER3D::TextureResourceSnapshot::GetNullDescriptorIndex(
                                         RENDER3D::TextureResourceDimension::TextureCube);
    graphics.cmdList->SetComputeRootDescriptorTable(kRootEnvironment, textureSnapshot_.GetDescriptor(cubeIndex));
    graphics.cmdList->SetComputeRootConstantBufferView(kRootAtmosphere, atmosphereBuffer_->GetGPUVirtualAddress());

    const uint32_t sampleEnd =
        (std::min)(status_.requestedSamples, status_.completedSamples + (std::max)(1u, maximumSamplesThisFrame));
    uint32_t sampleGridWidth = static_cast<uint32_t>(std::sqrt(status_.requestedSamples));
    while (status_.requestedSamples % sampleGridWidth != 0u)
        --sampleGridWidth;
    while (status_.completedSamples < sampleEnd) {
        DispatchConstants constants{};
        constants.width = width_;
        constants.height = height_;
        constants.sampleIndex = status_.completedSamples;
        constants.bounceCount = bounceCount_;
        constants.triangleCount = triangleCount_;
        constants.opaqueTriangleCount = opaqueTriangleCount_;
        constants.probeTarget = probeMaximumDistance_ > 0.0f ? 1u : 0u;
        constants.lightCount = lightCount_;
        constants.skyMode = static_cast<uint32_t>(environment_.skyMode);
        constants.skyTextureIndex = skyDescriptorIndex_;
        constants.sampleGridWidth = sampleGridWidth;
        constants.sampleGridHeight = status_.requestedSamples / sampleGridWidth;
        constants.skyParams = {environment_.skyStrength, environment_.skyYaw, probeMaximumDistance_, 0.0f};
        constants.skyTintExposure = environment_.skyTintExposure;
        graphics.cmdList->SetComputeRoot32BitConstants(kRootConstants, sizeof(constants) / sizeof(uint32_t), &constants,
                                                       0u);
        graphics.cmdList->Dispatch((width_ + 7u) / 8u, (height_ + 7u) / 8u, 1u);
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = outputBuffer_.Get();
        graphics.cmdList->ResourceBarrier(1u, &uavBarrier);
        ++status_.completedSamples;
    }
    // 後続の Editor 描画は通常の texture heap と自身の root table を再バインドする。
    if (auto* textureHeap = RENDER3D::GetTextureResourceSrvHeap())
        graphics.cmdList->SetDescriptorHeaps(1, &textureHeap);
    return true;
}

bool DxrProgressiveLightmapBaker::QueueReadback(const GFX::Context& graphics, std::string* outMessage) {
    if (status_.state != ProgressiveLightmapBakeState::Sampling ||
        status_.completedSamples < status_.requestedSamples) {
        SetMessage(outMessage, "DXR lightmap samples are not complete.");
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outputBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    graphics.cmdList->ResourceBarrier(1u, &barrier);
    graphics.cmdList->CopyBufferRegion(readbackBuffer_.Get(), 0u, outputBuffer_.Get(), 0u, AccumulationBytes());
    readbackFenceValue_ = graphics.currentFrameRetireFenceValue;
    status_.state = ProgressiveLightmapBakeState::ReadbackPending;
    status_.message = "DXR lightmap readback queued.";
    SetMessage(outMessage, status_.message);
    return true;
}

bool DxrProgressiveLightmapBaker::ReadbackReady(const GFX::Context& graphics) const noexcept {
    return status_.state == ProgressiveLightmapBakeState::ReadbackPending && graphics.frameFence != nullptr &&
           graphics.frameFence->GetCompletedValue() >= readbackFenceValue_;
}

bool DxrProgressiveLightmapBaker::TryReadProbes(const GFX::Context& graphics,
                                                std::vector<DxrTransportAccumulation>& outCells) {
    if (probeMaximumDistance_ <= 0.0f || !ReadbackReady(graphics))
        return false;
    void* mapped = nullptr;
    if (FAILED(readbackBuffer_->Map(0u, nullptr, &mapped)) || mapped == nullptr) {
        status_.state = ProgressiveLightmapBakeState::Failed;
        status_.message = "Failed to map DXR probe readback.";
        return false;
    }
    const auto* cells = static_cast<const DxrTransportAccumulation*>(mapped);
    outCells.assign(cells, cells + static_cast<size_t>(width_) * height_);
    readbackBuffer_->Unmap(0u, nullptr);
    status_.state = ProgressiveLightmapBakeState::Completed;
    status_.message = "Progressive DXR probes read back.";
    return true;
}

bool DxrProgressiveLightmapBaker::TrySave(const GFX::Context& graphics, const std::filesystem::path& outputPath,
                                          std::string* outMessage) {
    if (status_.state != ProgressiveLightmapBakeState::ReadbackPending) {
        return status_.state == ProgressiveLightmapBakeState::Completed;
    }
    if (!ReadbackReady(graphics)) {
        return false;
    }
    if (!SaveReadbackDds(outputPath, outMessage)) {
        status_.state = ProgressiveLightmapBakeState::Failed;
        status_.message = outMessage != nullptr ? *outMessage : "Failed to save DXR lightmap.";
        return false;
    }
    status_.state = ProgressiveLightmapBakeState::Completed;
    status_.message = "Progressive DXR lightmap saved.";
    return true;
}

const ProgressiveLightmapBakeStatus& DxrProgressiveLightmapBaker::GetStatus() const noexcept { return status_; }

} // namespace HIKARI::TOOLS::BAKING
