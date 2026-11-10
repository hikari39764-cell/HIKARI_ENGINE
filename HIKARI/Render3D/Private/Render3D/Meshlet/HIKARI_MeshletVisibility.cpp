#include "Render3D/Meshlet/HIKARI_MeshletVisibility.h"

#include <cstring>
#include <d3dx12.h>
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::MESHLET {
namespace {
constexpr uint32_t kSampleBit = 0x80000000u;
constexpr uint32_t kSecondPhaseBit = 0x40000000u;
bool AllocateViews(std::array<RenderResourceView, 4>& views) {
    if (views[0].IsValid())
        return true;
    auto allocated = AllocateRenderResourceDescriptorRange(4);
    if (allocated.size() != 4)
        return false;
    views = {allocated[0], allocated[1], allocated[2], allocated[3]};
    return true;
}
void WriteDepthView(ID3D12Device* device, const RenderResourceView& view, ID3D12Resource* depth, uint32_t mips) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = mips ? mips : 1u;
    device->CreateShaderResourceView(depth, &srv, view.cpu);
}
void WriteViews(ID3D12Device* device, const std::array<RenderResourceView, 4>& views, ID3D12Resource* depth,
                uint32_t mips, ID3D12Resource* counters, uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS queryAddress,
                ID3D12Resource* history = nullptr, uint32_t historyMips = 0) {
    WriteDepthView(device, views[0], depth, mips);
    WriteDepthView(device, views[3], history, historyMips);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = index * sizeof(MeshletVisibilityCounters) / 4;
    uav.Buffer.NumElements = sizeof(MeshletVisibilityCounters) / 4;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(counters, nullptr, &uav, views[1].cpu);
    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{queryAddress, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT};
    device->CreateConstantBufferView(&cbv, views[2].cpu);
}
} // namespace

bool IsMeshletOcclusionViewUsable(const DEPTH::DepthPyramidView& depth, const MATH::Mat4& renderViewProj,
                                  const MATH::Mat4& cullingViewProj, bool frozen) {
    if (!depth.valid || !depth.viewProjValid || !depth.resource || !depth.pyramidSrv.ptr || !depth.width ||
        !depth.height || !depth.mipCount || depth.mipCount > 255u ||
        depth.depthConvention != DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax)
        return false;
    const bool frozenView = depth.viewKind == DEPTH::DepthPyramidViewKind::Frozen;
    if (frozenView ? !frozen : (frozen || depth.viewKind != DEPTH::DepthPyramidViewKind::CurrentFrame))
        return false;
    const auto& expected = frozenView ? cullingViewProj : renderViewProj;
    return std::memcmp(&depth.viewProj, &expected, sizeof(expected)) == 0;
}

bool MeshletVisibility::EnsureFrame(const GFX::Context& graphics, Frame& frame) {
    if (frame.initialized)
        return true;
    constexpr UINT64 bytes = kMaxViewsPerFrame * sizeof(MeshletVisibilityCounters);
    const CD3DX12_HEAP_PROPERTIES gpuHeap(D3D12_HEAP_TYPE_DEFAULT), readHeap(D3D12_HEAP_TYPE_READBACK);
    const auto gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
            graphics.device, &gpuHeap, D3D12_HEAP_FLAG_NONE, &gpuDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            GFX::VIDEO_MEMORY::Owner::GpuDriven, IID_PPV_ARGS(&frame.counters))) ||
        FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
            graphics.device, &readHeap, D3D12_HEAP_FLAG_NONE, &readDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            GFX::VIDEO_MEMORY::Owner::GpuDriven, IID_PPV_ARGS(&frame.readback)))) {
        // 途中失敗した未送信リソースを残さず、次回の生成を安全にする。
        frame.counters.Reset();
        frame.readback.Reset();
        return false;
    }
    frame.counters->SetName(L"Meshlet.Visibility.Counters");
    frame.readback->SetName(L"Meshlet.Visibility.Readback");
    frame.initialized = true;
    return true;
}

void MeshletVisibility::Collect(Frame& frame) {
    if (!frame.resolved)
        return;
    const D3D12_RANGE range{0, frame.count * sizeof(MeshletVisibilityCounters)};
    void* mapped = nullptr;
    if (FAILED(frame.readback->Map(0, &range, &mapped)))
        return;
    const auto* values = static_cast<const MeshletVisibilityCounters*>(mapped);
    for (uint32_t i = 0; i < frame.count; ++i) {
        const auto& entry = frame.entries[i];
        const auto phase = static_cast<size_t>(entry.phase);
        if (!entry.sampled || phase >= stats_.samples.size())
            continue;
        auto& sample = stats_.samples[phase];
        if (sample.valid && sample.retireFence >= frame.fence)
            continue;
        sample = {values[i], frame.fence, true};
    }
    const D3D12_RANGE noWrite{0, 0};
    frame.readback->Unmap(0, &noWrite);
    frame.resolved = false;
}

MeshletVisibilityBinding MeshletVisibility::Begin(const GFX::Context& graphics, const DEPTH::DepthPyramidView* depth,
                                                  MeshletVisibilityPhase phase,
                                                  const DEPTH::DepthPyramidView* history) {
    active_ = nullptr;
    if (!graphics.device || !graphics.cmdList)
        return {};
    if (!zeroUpload_) {
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(kMaxViewsPerFrame * sizeof(MeshletVisibilityCounters));
        if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
                graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                GFX::VIDEO_MEMORY::Owner::GpuDriven, IID_PPV_ARGS(&zeroUpload_))))
            return {};
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(zeroUpload_->Map(0, &noRead, &mapped))) {
            zeroUpload_.Reset();
            return {};
        }
        std::memset(mapped, 0, kMaxViewsPerFrame * sizeof(MeshletVisibilityCounters));
        zeroUpload_->Unmap(0, nullptr);
    }
    if (!nullViews_[0].IsValid()) {
        if (!AllocateViews(nullViews_))
            return {};
        WriteViews(graphics.device, nullViews_, nullptr, 0, nullptr, 0, zeroUpload_->GetGPUVirtualAddress());
    }
    const bool hasDepth = depth && depth->valid && depth->resource && depth->mipCount;
    const bool hasHistory = history && history->valid && history->resource && history->mipCount;
    const bool secondPhase = phase == MeshletVisibilityPhase::GeometryAuxSecond;
    if (phase == MeshletVisibilityPhase::Forward) {
        stats_.forwardHzbBound = false;
        stats_.width = stats_.height = stats_.mipCount = 0;
    }
    const MeshletVisibilityBinding disabled{nullViews_[0].gpu, 0};
    // 二段目が使えない時は、遮蔽の判定なしで全部を描き直す（一段目が落とした簇を欠かさない）。
    if ((phase == MeshletVisibilityPhase::Other && !hasDepth) || (secondPhase && (!hasDepth || !hasHistory)))
        return disabled;
    auto& frame = frames_[graphics.frameIndex % frames_.size()];
    const uint64_t retire = graphics.currentFrameRetireFenceValue;
    const uint64_t completed = graphics.frameFence ? graphics.frameFence->GetCompletedValue() : 0;
    if (frame.fence != retire) {
        if (frame.fence != 0 && completed < frame.fence)
            return disabled;
        Collect(frame);
        frame.queries.Reset();
        frame.count = 0;
        frame.fence = retire;
    }
    if (frame.count >= frame.entries.size())
        return disabled;
    const bool sampled = GFX::GPU_PROFILE::IsEnabled() && retire != 0 && graphics.frameFence && (retire % 16u == 0u);
    if (!hasDepth && !sampled)
        return disabled;
    auto& entry = frame.entries[frame.count];
    if (!AllocateViews(entry.views) || (sampled && !EnsureFrame(graphics, frame)))
        return disabled;
    if (!frame.queries.IsInitialized() &&
        !frame.queries.Init(graphics.device, kMaxViewsPerFrame * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                            GFX::UPLOAD_TRAFFIC::Owner::PassConstants))
        return disabled;
    const auto allocation = frame.queries.Allocate(sizeof(MeshletOcclusionGpuData));
    if (!allocation)
        return disabled;
    MeshletOcclusionGpuData query{};
    if (hasDepth)
        query.viewProj = depth->viewProj;
    if (secondPhase)
        query.historyViewProj = history->viewProj;
    query.control = (hasDepth ? depth->mipCount : 0u) |
                    (secondPhase ? (history->mipCount << 8u) | kSecondPhaseBit : 0u) | (sampled ? kSampleBit : 0u);
    std::memcpy(allocation.cpuAddress, &query, sizeof(query));
    entry.phase = phase;
    entry.sampled = sampled;
    WriteViews(graphics.device, entry.views, hasDepth ? depth->resource : nullptr, hasDepth ? depth->mipCount : 0,
               sampled ? frame.counters.Get() : nullptr, sampled ? frame.count : 0, allocation.gpuAddress,
               secondPhase ? history->resource : nullptr, secondPhase ? history->mipCount : 0);
    if (sampled) {
        const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
            frame.counters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        graphics.cmdList->ResourceBarrier(1, &toCopy);
        graphics.cmdList->CopyBufferRegion(frame.counters.Get(), frame.count * sizeof(MeshletVisibilityCounters),
                                           zeroUpload_.Get(), 0, sizeof(MeshletVisibilityCounters));
        const auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(frame.counters.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        graphics.cmdList->ResourceBarrier(1, &toUav);
        active_ = &frame;
        activeIndex_ = frame.count;
    }
    ++frame.count;
    if (phase == MeshletVisibilityPhase::Forward && hasDepth) {
        stats_.forwardHzbBound = true;
        stats_.width = depth->width;
        stats_.height = depth->height;
        stats_.mipCount = depth->mipCount;
    }
    return {entry.views[0].gpu, query.control};
}

void MeshletVisibility::End(const GFX::Context& graphics) {
    if (!active_)
        return;
    const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        active_->counters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    graphics.cmdList->ResourceBarrier(1, &toCopy);
    const uint64_t offset = activeIndex_ * sizeof(MeshletVisibilityCounters);
    graphics.cmdList->CopyBufferRegion(active_->readback.Get(), offset, active_->counters.Get(), offset,
                                       sizeof(MeshletVisibilityCounters));
    const auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(active_->counters.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    graphics.cmdList->ResourceBarrier(1, &toUav);
    active_->resolved = true;
    active_ = nullptr;
}

void MeshletVisibility::Reset() {
    const auto& graphics = GFX::GetCurrentContext();
    for (auto& frame : frames_) {
        GFX::RetireD3D12ObjectForFrame(frame.counters, graphics, "Meshlet.Visibility.Counters");
        GFX::RetireD3D12ObjectForFrame(frame.readback, graphics, "Meshlet.Visibility.Readback");
        for (auto& entry : frame.entries)
            for (auto& view : entry.views)
                RetireRenderResourceDescriptor(view, graphics, "Meshlet.Visibility.Descriptor");
        frame.queries.Finalize(&graphics);
        frame = {};
    }
    for (auto& view : nullViews_)
        RetireRenderResourceDescriptor(view, graphics, "Meshlet.Visibility.Null");
    GFX::RetireD3D12ObjectForFrame(zeroUpload_, graphics, "Meshlet.Visibility.ClearStaging");
    active_ = nullptr;
    stats_ = {};
}
} // namespace HIKARI::RENDER3D::MESHLET
