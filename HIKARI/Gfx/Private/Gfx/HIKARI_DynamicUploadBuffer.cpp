#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"

#include <algorithm>
#include <utility>
#include <d3dx12.h>

using namespace HIKARI::DX;

namespace {

constexpr size_t kUploadAlignment = 256u;

} // namespace

DynamicUploadBuffer::DynamicUploadBuffer() {}
DynamicUploadBuffer::~DynamicUploadBuffer() { Finalize(); }

DynamicUploadBuffer& DynamicUploadBuffer::operator=(DynamicUploadBuffer&& other) noexcept {
    if (this != &other) {
        Finalize();
        device_ = std::move(other.device_);
        pages_ = std::move(other.pages_);
        pageRanges_ = std::move(other.pageRanges_);
        defaultPageSize_ = std::exchange(other.defaultPageSize_, 0u);
        activePageIndex_ = std::exchange(other.activePageIndex_, 0u);
        frameSerial_ = other.frameSerial_;
        hasFrameSerial_ = std::exchange(other.hasFrameSerial_, false);
    }
    return *this;
}

bool DynamicUploadBuffer::Init(ID3D12Device* device, size_t bufferSize) {
    Finalize();
    if (device == nullptr || !HIKARI::GFX::TryAlignUploadSize(bufferSize, kUploadAlignment, defaultPageSize_)) {
        return false;
    }
    device_ = device;
    if (!CreatePage(defaultPageSize_)) {
        Finalize();
        return false;
    }
    return true;
}

bool DynamicUploadBuffer::CreatePage(size_t capacity) {
    if (device_ == nullptr || capacity == 0u) {
        return false;
    }
    try {
        pages_.reserve(pages_.size() + 1u);
        pageRanges_.reserve(pageRanges_.size() + 1u);
    } catch (...) {
        return false;
    }
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(capacity);

    Page page{};
    HRESULT hr =
        device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                         nullptr, IID_PPV_ARGS(page.resource.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "DynamicUploadBuffer::CreateCommittedResource")) {
        return false;
    }
    HIKARI::GFX::VIDEO_MEMORY::Track(page.resource.Get(), HIKARI::GFX::VIDEO_MEMORY::Category::Upload);

    const D3D12_RANGE readRange{0u, 0u};
    hr = page.resource->Map(0u, &readRange, reinterpret_cast<void**>(&page.cpuBase));
    if (!HIKARI_DX_CHECK(hr, "DynamicUploadBuffer::Map")) {
        return false;
    }
    if (page.cpuBase == nullptr) {
        return false;
    }
    page.gpuBase = page.resource->GetGPUVirtualAddress();
    pages_.push_back(std::move(page));
    pageRanges_.push_back({capacity, 0u});
    return true;
}

void* DynamicUploadBuffer::Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress) {
    const UploadAllocation allocation = Allocate(size);
    gpuAddress = allocation.gpuAddress;
    return allocation.cpuAddress;
}

UploadAllocation DynamicUploadBuffer::Allocate(size_t size) {
    HIKARI::GFX::UploadPageReservation reservation{};
    if (!HIKARI::GFX::TryReserveUploadPageRange(pageRanges_, activePageIndex_, size, kUploadAlignment, reservation)) {
        size_t alignedSize = 0u;
        if (!HIKARI::GFX::TryAlignUploadSize(size, kUploadAlignment, alignedSize)) {
            return {};
        }
        const size_t newPageSize = std::max(defaultPageSize_, alignedSize);
        if (!CreatePage(newPageSize) || !HIKARI::GFX::TryReserveUploadPageRange(pageRanges_, activePageIndex_, size,
                                                                                kUploadAlignment, reservation)) {
            return {};
        }
    }

    if (reservation.pageIndex >= pages_.size()) {
        return {};
    }
    Page& page = pages_[reservation.pageIndex];
    return {page.cpuBase + reservation.offset, page.resource.Get(), page.gpuBase + reservation.offset,
            reservation.offset};
}

void DynamicUploadBuffer::Finalize(const HIKARI::GFX::Context* retireContext) {
    for (Page& page : pages_) {
        if (page.resource != nullptr && page.cpuBase != nullptr) {
            page.resource->Unmap(0u, nullptr);
        }
        if (retireContext != nullptr) {
            HIKARI::GFX::RetireD3D12ObjectForFrame(page.resource, *retireContext, "DynamicUploadBuffer.Page");
        } else {
            page.resource.Reset();
        }
        page.cpuBase = nullptr;
        page.gpuBase = 0u;
    }
    pages_.clear();
    pageRanges_.clear();
    device_.Reset();
    defaultPageSize_ = 0u;
    activePageIndex_ = 0u;
    hasFrameSerial_ = false;
}

void DynamicUploadBuffer::Reset() {
    HIKARI::GFX::ResetUploadPageRanges(pageRanges_, activePageIndex_);
    hasFrameSerial_ = false;
}

void DynamicUploadBuffer::ResetForFrame(uint64_t frameSerial) {
    if (!hasFrameSerial_ || frameSerial_ != frameSerial) {
        Reset();
        frameSerial_ = frameSerial;
        hasFrameSerial_ = true;
    }
}
