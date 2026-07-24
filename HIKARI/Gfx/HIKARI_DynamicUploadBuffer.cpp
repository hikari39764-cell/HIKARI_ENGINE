#include "HIKARI_DynamicUploadBuffer.h"
#include <cassert>
#include <d3dx12.h>

using namespace HIKARI::DX;
// 256 バイト境界にサイズを切り上げる
static inline size_t Align256(size_t size) {
    return (size + 255) & ~255;
}
// DynamicUploadBuffer クラスの実装
DynamicUploadBuffer::DynamicUploadBuffer() {}
DynamicUploadBuffer::~DynamicUploadBuffer() {}

// バッファを初期化する。device には ID3D12Device のポインタを、bufferSize にはバッファのサイズを指定する
void DynamicUploadBuffer::Init(ID3D12Device* device, size_t bufferSize)
{
    bufferSize_ = Align256(bufferSize);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize_);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer_)
    );
    assert(SUCCEEDED(hr));

    buffer_->Map(0, nullptr, reinterpret_cast<void**>(&cpuBase_));
    gpuBase_ = buffer_->GetGPUVirtualAddress();

    offset_ = 0;
}
// バッファから size バイト分の領域を確保し、その CPU アドレスを返す。gpuAddress には対応する GPU アドレスが格納される
void* DynamicUploadBuffer::Allocate(size_t size, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress)
{
    size_t aligned = Align256(size);

    if (offset_ + aligned > bufferSize_) {
        offset_ = 0; // ｻﾘｻｷ
    }

    void* cpuAddr = cpuBase_ + offset_;
    gpuAddress = gpuBase_ + offset_;

    offset_ += aligned;

    return cpuAddr;
}
// バッファを解放する。バッファが存在する場合は Unmap し、リセットする
void DynamicUploadBuffer::Finalize()
{
    if (buffer_) {
        buffer_->Unmap(0, nullptr);
    }
    buffer_.Reset();
    cpuBase_ = nullptr;
    gpuBase_ = 0;
    offset_ = 0;
    bufferSize_ = 0;
}
// バッファのオフセットをリセットする
void DynamicUploadBuffer::Reset()
{
    offset_ = 0;
}
