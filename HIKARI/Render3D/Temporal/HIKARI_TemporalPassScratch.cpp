#include "Render3D/Temporal/Internal/HIKARI_TemporalPassScratch.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"

namespace HIKARI::RENDER3D::TEMPORAL::DETAIL {

namespace {
uint64_t GrowUploadCapacity(uint64_t requiredBytes) noexcept {
    uint64_t capacity = 256u;
    while (capacity < requiredBytes && capacity <= (std::numeric_limits<uint64_t>::max)() / 2u) {
        capacity *= 2u;
    }
    return (std::max)(capacity, requiredBytes);
}

bool MatchesPhysicalFrame(const GFX::Context& graphics, TemporalResourceFrame frame) noexcept {
    return frame.IsValid() && graphics.deviceGeneration == frame.deviceGeneration && graphics.frameFence != nullptr &&
           graphics.currentFrameRetireFenceValue != 0u;
}

bool CanClaimSlot(const GFX::Context& graphics, TemporalResourceFrame claimedFrame, uint64_t retireFenceValue,
                  TemporalResourceFrame requestedFrame) noexcept {
    if (graphics.frameFence == nullptr) {
        return false;
    }
    return CanReuseTemporalPassSlot(claimedFrame, retireFenceValue, requestedFrame,
                                    graphics.frameFence->GetCompletedValue());
}

bool CreateMappedBuffer(ID3D12Device* device, uint64_t requiredBytes, const wchar_t* debugName,
                        Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, uint8_t*& outMapped) {
    outBuffer.Reset();
    outMapped = nullptr;
    if (device == nullptr || requiredBytes == 0u) {
        return false;
    }
    if (!GFX::D3D12_BUFFER::CreateMappedUploadBuffer(device, requiredBytes, outBuffer,
                                                     reinterpret_cast<void**>(&outMapped))) {
        outBuffer.Reset();
        outMapped = nullptr;
        return false;
    }
    if (debugName != nullptr) {
        outBuffer->SetName(debugName);
    }
    return true;
}

void ReleaseTemporaryBuffer(Microsoft::WRL::ComPtr<ID3D12Resource>& buffer, uint8_t*& mapped) noexcept {
    if (buffer != nullptr && mapped != nullptr) {
        buffer->Unmap(0, nullptr);
    }
    mapped = nullptr;
    buffer.Reset();
}

void ReleaseMappedBuffer(TemporalResourceState& state, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer, uint8_t*& mapped,
                         TemporalResourceReleaseMode mode, const char* debugName) {
    if (buffer != nullptr && mapped != nullptr) {
        buffer->Unmap(0, nullptr);
    }
    mapped = nullptr;
    if (buffer == nullptr) {
        return;
    }
    if (UsesDeferredRelease(mode)) {
        GFX::RetireD3D12ObjectForFrame(buffer, state.retirementContext,
                                       debugName != nullptr ? std::string(debugName)
                                                            : std::string("Temporal.PassUpload"));
        return;
    }
    buffer.Reset();
}

bool EnsureMappedSlotCapacity(TemporalResourceState& state, TemporalMappedUploadSlot& slot, uint64_t requiredBytes,
                              const wchar_t* debugName) {
    if (slot.buffer != nullptr && slot.mapped != nullptr && slot.capacityBytes >= requiredBytes) {
        return true;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> replacement{};
    uint8_t* replacementMapped = nullptr;
    const uint64_t replacementCapacity = GrowUploadCapacity(requiredBytes);
    if (!CreateMappedBuffer(state.ownerDevice, replacementCapacity, debugName, replacement, replacementMapped)) {
        return false;
    }

    ReleaseMappedBuffer(state, slot.buffer, slot.mapped, TemporalResourceReleaseMode::ImmediateAfterGpuIdle,
                        "Temporal.PassUpload.Replace");
    slot.buffer = std::move(replacement);
    slot.mapped = replacementMapped;
    slot.capacityBytes = replacementCapacity;
    return true;
}

TemporalMappedUploadSlot& SelectConstantSlot(TemporalPassScratchState& scratch, TemporalConstantUploadKind kind,
                                             uint32_t frameSlot) {
    return kind == TemporalConstantUploadKind::MotionVector ? scratch.motionConstants[frameSlot]
                                                            : scratch.taaConstants[frameSlot];
}

void ReleaseMappedSlot(TemporalResourceState& state, TemporalMappedUploadSlot& slot, TemporalResourceReleaseMode mode,
                       const char* debugName) {
    ReleaseMappedBuffer(state, slot.buffer, slot.mapped, mode, debugName);
    slot.capacityBytes = 0u;
    slot.claimedFrame = {};
    slot.retireFenceValue = 0u;
}

void ReleaseGeometrySlot(TemporalResourceState& state, TemporalGeometryUploadSlot& slot,
                         TemporalResourceReleaseMode mode) {
    ReleaseMappedBuffer(state, slot.geometryBuffer, slot.geometryMapped, mode, "Temporal.GeometryConstants");
    ReleaseMappedBuffer(state, slot.skinBuffer, slot.skinMapped, mode, "Temporal.SkinConstants");
    slot.geometryCapacityBytes = 0u;
    slot.skinCapacityBytes = 0u;
    slot.claimedFrame = {};
    slot.retireFenceValue = 0u;
}
} // namespace

size_t TemporalGeometryHistoryKeyHash::operator()(const TemporalGeometryHistoryKey& key) const noexcept {
    size_t hash = static_cast<size_t>(key.objectId);
    hash ^= static_cast<size_t>(key.objectLifetimeGeneration) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(key.vertexBufferAddress) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(key.indexBufferAddress) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(key.sourceSurfaceInstanceIndex) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(key.startIndex) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(key.indexCount) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    hash ^= static_cast<size_t>(static_cast<uint32_t>(key.baseVertex)) + 0x9e3779b97f4a7c15ull + (hash << 6u) +
            (hash >> 2u);
    return hash;
}

bool CanReuseTemporalPassSlot(TemporalResourceFrame claimedFrame, uint64_t retireFenceValue,
                              TemporalResourceFrame requestedFrame, uint64_t completedFenceValue) noexcept {
    if (!requestedFrame.IsValid()) {
        return false;
    }
    if (!claimedFrame.IsValid()) {
        return true;
    }
    if (claimedFrame.Matches(requestedFrame) || retireFenceValue == 0u) {
        return false;
    }
    return completedFenceValue >= retireFenceValue;
}

bool TryAcquireTemporalConstantUpload(const GFX::Context& graphics, TemporalResourceFrame frame,
                                      TemporalConstantUploadKind kind, uint64_t requiredBytes, const wchar_t* debugName,
                                      TemporalPassUploadSlice& outSlice) {
    outSlice = {};
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr || requiredBytes == 0u || requiredBytes > kTemporalMaximumPassUploadBytes ||
        !MatchesPhysicalFrame(graphics, frame) || !ContextMatchesOwner(*state, graphics)) {
        return false;
    }

    const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
    TemporalMappedUploadSlot& slot = SelectConstantSlot(state->passScratch, kind, frameSlot);
    if (!CanClaimSlot(graphics, slot.claimedFrame, slot.retireFenceValue, frame) ||
        !EnsureMappedSlotCapacity(*state, slot, requiredBytes, debugName)) {
        return false;
    }

    slot.claimedFrame = frame;
    slot.retireFenceValue = graphics.currentFrameRetireFenceValue;
    state->retirementContext = graphics;
    outSlice.mapped = slot.mapped;
    outSlice.gpuAddress = slot.buffer->GetGPUVirtualAddress();
    outSlice.capacityBytes = slot.capacityBytes;
    return outSlice.mapped != nullptr && outSlice.gpuAddress != 0u && outSlice.capacityBytes >= requiredBytes;
}

bool TryAcquireTemporalGeometryUpload(const GFX::Context& graphics, TemporalResourceFrame frame, uint64_t geometryBytes,
                                      uint64_t skinBytes, TemporalGeometryUploadSlice& outSlice) {
    outSlice = {};
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr || geometryBytes == 0u || geometryBytes > kTemporalMaximumPassUploadBytes ||
        skinBytes > kTemporalMaximumPassUploadBytes || !MatchesPhysicalFrame(graphics, frame) ||
        !ContextMatchesOwner(*state, graphics)) {
        return false;
    }

    const uint32_t frameSlot = graphics.frameIndex % GFX::kFrameResourceCount;
    TemporalGeometryUploadSlot& slot = state->passScratch.geometryUploads[frameSlot];
    if (!CanClaimSlot(graphics, slot.claimedFrame, slot.retireFenceValue, frame)) {
        return false;
    }

    const bool replaceGeometry =
        slot.geometryBuffer == nullptr || slot.geometryMapped == nullptr || slot.geometryCapacityBytes < geometryBytes;
    const bool replaceSkin = skinBytes != 0u && (slot.skinBuffer == nullptr || slot.skinMapped == nullptr ||
                                                 slot.skinCapacityBytes < skinBytes);

    Microsoft::WRL::ComPtr<ID3D12Resource> geometryReplacement{};
    Microsoft::WRL::ComPtr<ID3D12Resource> skinReplacement{};
    uint8_t* geometryReplacementMapped = nullptr;
    uint8_t* skinReplacementMapped = nullptr;
    const uint64_t geometryReplacementCapacity =
        replaceGeometry ? GrowUploadCapacity(geometryBytes) : slot.geometryCapacityBytes;
    const uint64_t skinReplacementCapacity = replaceSkin ? GrowUploadCapacity(skinBytes) : slot.skinCapacityBytes;
    if (replaceGeometry &&
        !CreateMappedBuffer(state->ownerDevice, geometryReplacementCapacity, L"HIKARI.Temporal.GeometryConstants",
                            geometryReplacement, geometryReplacementMapped)) {
        return false;
    }
    if (replaceSkin && !CreateMappedBuffer(state->ownerDevice, skinReplacementCapacity,
                                           L"HIKARI.Temporal.SkinConstants", skinReplacement, skinReplacementMapped)) {
        ReleaseTemporaryBuffer(geometryReplacement, geometryReplacementMapped);
        return false;
    }

    if (replaceGeometry) {
        ReleaseMappedBuffer(*state, slot.geometryBuffer, slot.geometryMapped,
                            TemporalResourceReleaseMode::ImmediateAfterGpuIdle, "Temporal.GeometryConstants.Replace");
        slot.geometryBuffer = std::move(geometryReplacement);
        slot.geometryMapped = geometryReplacementMapped;
        slot.geometryCapacityBytes = geometryReplacementCapacity;
    }
    if (replaceSkin) {
        ReleaseMappedBuffer(*state, slot.skinBuffer, slot.skinMapped,
                            TemporalResourceReleaseMode::ImmediateAfterGpuIdle, "Temporal.SkinConstants.Replace");
        slot.skinBuffer = std::move(skinReplacement);
        slot.skinMapped = skinReplacementMapped;
        slot.skinCapacityBytes = skinReplacementCapacity;
    }

    slot.claimedFrame = frame;
    slot.retireFenceValue = graphics.currentFrameRetireFenceValue;
    state->retirementContext = graphics;
    outSlice.geometryMapped = slot.geometryMapped;
    outSlice.skinMapped = skinBytes != 0u ? slot.skinMapped : nullptr;
    outSlice.geometryGpuAddress = slot.geometryBuffer != nullptr ? slot.geometryBuffer->GetGPUVirtualAddress() : 0u;
    outSlice.skinGpuAddress =
        skinBytes != 0u && slot.skinBuffer != nullptr ? slot.skinBuffer->GetGPUVirtualAddress() : 0u;
    outSlice.geometryCapacityBytes = slot.geometryCapacityBytes;
    outSlice.skinCapacityBytes = slot.skinCapacityBytes;
    return outSlice.geometryMapped != nullptr && outSlice.geometryGpuAddress != 0u &&
           outSlice.geometryCapacityBytes >= geometryBytes &&
           (skinBytes == 0u || (outSlice.skinMapped != nullptr && outSlice.skinGpuAddress != 0u &&
                                outSlice.skinCapacityBytes >= skinBytes));
}

void ClearTemporalGeometryHistory(TemporalResourceState& state) noexcept { state.passScratch.geometryHistory.clear(); }

void ReleaseTemporalPassScratch(TemporalResourceState& state, TemporalResourceReleaseMode mode) {
    for (TemporalMappedUploadSlot& slot : state.passScratch.motionConstants) {
        ReleaseMappedSlot(state, slot, mode, "Temporal.MotionVectorConstants");
    }
    for (TemporalMappedUploadSlot& slot : state.passScratch.taaConstants) {
        ReleaseMappedSlot(state, slot, mode, "Temporal.TaaResolveConstants");
    }
    for (TemporalGeometryUploadSlot& slot : state.passScratch.geometryUploads) {
        ReleaseGeometrySlot(state, slot, mode);
    }
    ClearTemporalGeometryHistory(state);
}

} // namespace HIKARI::RENDER3D::TEMPORAL::DETAIL
