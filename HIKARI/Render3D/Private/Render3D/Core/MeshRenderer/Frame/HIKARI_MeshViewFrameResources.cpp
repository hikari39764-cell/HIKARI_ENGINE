#include "Render3D/Core/MeshRenderer/Frame/HIKARI_MeshViewFrameResources.h"

#include <array>
#include <cstring>
#include <utility>
#include <d3dx12.h>
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::MESHRENDERER {

MeshViewFrameResources::~MeshViewFrameResources() { Finalize(); }

MeshViewFrameResources::MeshViewFrameResources(MeshViewFrameResources&& other) noexcept { *this = std::move(other); }

MeshViewFrameResources& MeshViewFrameResources::operator=(MeshViewFrameResources&& other) noexcept {
    if (this != &other) {
        Finalize();
        camera = other.camera;
        cullingCamera = other.cullingCamera;
        light = other.light;
        shadow = other.shadow;
        skyEnvironment = other.skyEnvironment;
        localLightList = other.localLightList;
        localLightBuildSignature_ = other.localLightBuildSignature_;
        localLightBuildCached_ = std::exchange(other.localLightBuildCached_, false);
        uploads_ = std::move(other.uploads_);
        localLightBuffer_ = std::move(other.localLightBuffer_);
        localLightCapacity_ = std::exchange(other.localLightCapacity_, 0u);
        localLightState_ = other.localLightState_;
        localLightVersion_ = other.localLightVersion_;
        residentLocalLightVersion_ = std::exchange(other.residentLocalLightVersion_, 0u);
        records_ = std::move(other.records_);
        headers_ = std::move(other.headers_);
        indices_ = std::move(other.indices_);
        bindings_ = std::exchange(other.bindings_, {});
    }
    return *this;
}

bool MeshViewFrameResources::Initialize(ID3D12Device* device) {
    Finalize();
    if (device == nullptr)
        return false;
    using namespace RENDER3D::LIGHTING;
    try {
        records_.resize(kMaximumLocalLightCount + kMaximumLocalDecalCount);
        headers_.resize(kMaximumLocalLightClusterCount);
        indices_.resize(kMaximumLocalLightClusterIndexCount);
    } catch (...) {
        Finalize();
        return false;
    }
    return uploads_.Init(device, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, GFX::UPLOAD_TRAFFIC::Owner::ViewConstants);
}

void MeshViewFrameResources::Finalize() {
    uploads_.Finalize(&GFX::GetCurrentContext());
    if (localLightBuffer_)
        GFX::RetireD3D12ObjectForFrame(localLightBuffer_, GFX::GetCurrentContext(), "MeshView.LocalLights");
    localLightCapacity_ = 0;
    localLightState_ = D3D12_RESOURCE_STATE_COMMON;
    residentLocalLightVersion_ = 0;
    records_.clear();
    headers_.clear();
    indices_.clear();
    bindings_ = {};
    localLightList = {};
    localLightBuildCached_ = false;
}

void MeshViewFrameResources::BuildLocalLights(const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                              const Camera3D& viewCamera, uint32_t width, uint32_t height,
                                              std::span<const RENDER3D::LIGHTING::LocalLightShadowAssignment> shadows) {
    const auto signature =
        RENDER3D::LIGHTING::LocalLightListInputSignature(lighting, viewCamera, width, height, shadows);
    if (localLightBuildCached_ && signature == localLightBuildSignature_)
        return;
    localLightList = RENDER3D::LIGHTING::BuildLocalLightList(lighting, viewCamera, width, height,
                                                             {records_.data(), static_cast<uint32_t>(records_.size()),
                                                              headers_.data(), static_cast<uint32_t>(headers_.size()),
                                                              indices_.data(), static_cast<uint32_t>(indices_.size())},
                                                             shadows);
    localLightBuildSignature_ = signature;
    localLightBuildCached_ = true;
    ++localLightVersion_;
}

bool MeshViewFrameResources::Publish(const GFX::Context& graphics, const CameraCB* cullingOverride) {
    bindings_ = {};
    const uint32_t recordCount = localLightList.lightCount + localLightList.decalCount;
    if (!IsInitialized() || recordCount > records_.size() || localLightList.clusterCount > headers_.size() ||
        localLightList.clusterIndexCount > indices_.size())
        return false;

    using namespace RENDER3D::LIGHTING;
    const LocalLightGpuData emptyLight{};
    const LocalLightClusterHeader emptyHeader{};
    const uint32_t emptyIndex = 0u;
    // 全 CBV/SRV を揃えてから公開する。失敗時に異なる view の値を混在させない。定数は先頭 5 つ、局所光の記録・
    // 分簇は後ろ 3 つ（常駐 buffer）。
    constexpr size_t kConstantBlocks = 5u;
    const std::array<Block, 8> blocks{
        {{&camera, sizeof(camera)},
         {cullingOverride != nullptr ? cullingOverride : &camera, sizeof(camera)},
         {&light, sizeof(light)},
         {&shadow, sizeof(shadow)},
         {&skyEnvironment, sizeof(skyEnvironment)},
         {recordCount ? records_.data() : &emptyLight, sizeof(emptyLight) * (recordCount ? recordCount : 1u)},
         {localLightList.clusterCount ? headers_.data() : &emptyHeader,
          sizeof(emptyHeader) * (localLightList.clusterCount ? localLightList.clusterCount : 1u)},
         {localLightList.clusterIndexCount ? indices_.data() : &emptyIndex,
          sizeof(emptyIndex) * (localLightList.clusterIndexCount ? localLightList.clusterIndexCount : 1u)}}};
    std::array<size_t, blocks.size()> offsets{};
    size_t constantBytes = 0u, lightBytes = 0u;
    for (size_t index = 0u; index < blocks.size(); ++index) {
        size_t& bytes = index < kConstantBlocks ? constantBytes : lightBytes;
        offsets[index] = bytes;
        bytes += GFX::AlignD3D12ConstantBufferByteSize(static_cast<UINT>(blocks[index].bytes));
    }
    uploads_.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const DX::UploadAllocation constants = uploads_.Allocate(constantBytes, GFX::UPLOAD_TRAFFIC::Owner::ViewConstants);
    if (!constants)
        return false;
    std::array<D3D12_GPU_VIRTUAL_ADDRESS, blocks.size()> addresses{};
    for (size_t index = 0u; index < kConstantBlocks; ++index) {
        std::memcpy(static_cast<std::byte*>(constants.cpuAddress) + offsets[index], blocks[index].data,
                    blocks[index].bytes);
        addresses[index] = constants.gpuAddress + offsets[index];
    }
    const std::span<const Block> lightBlocks(blocks.data() + kConstantBlocks, blocks.size() - kConstantBlocks);
    const std::span<const size_t> lightOffsets(offsets.data() + kConstantBlocks, lightBlocks.size());
    if ((residentLocalLightVersion_ != localLightVersion_ || localLightCapacity_ < lightBytes) &&
        !WriteLocalLights(graphics, lightBlocks, lightOffsets, lightBytes))
        return false;
    for (size_t index = kConstantBlocks; index < blocks.size(); ++index)
        addresses[index] = localLightBuffer_->GetGPUVirtualAddress() + offsets[index];
    bindings_ = {addresses[0], addresses[1], addresses[2],
                 addresses[3], addresses[4], {addresses[5], addresses[6], addresses[7], localLightList}};
    return true;
}

bool MeshViewFrameResources::WriteLocalLights(const GFX::Context& graphics, std::span<const Block> blocks,
                                              std::span<const size_t> offsets, size_t bytes) {
    if (graphics.cmdList == nullptr)
        return false;
    if (localLightCapacity_ < bytes) {
        if (localLightBuffer_)
            GFX::RetireD3D12ObjectForFrame(localLightBuffer_, graphics, "MeshView.LocalLights");
        constexpr size_t kGranularity = 64u * 1024u;
        const size_t capacity = (bytes + kGranularity - 1u) / kGranularity * kGranularity;
        localLightCapacity_ = 0;
        localLightState_ = D3D12_RESOURCE_STATE_COMMON;
        if (!GFX::D3D12_BUFFER::CreateDefaultBuffer(graphics.device, capacity, GFX::VIDEO_MEMORY::Owner::Lighting,
                                                    localLightBuffer_))
            return false;
        localLightCapacity_ = capacity;
    }
    const DX::UploadAllocation staging = uploads_.Allocate(bytes, GFX::UPLOAD_TRAFFIC::Owner::LocalLights);
    if (!staging)
        return false;
    for (size_t index = 0u; index < blocks.size(); ++index)
        std::memcpy(static_cast<std::byte*>(staging.cpuAddress) + offsets[index], blocks[index].data,
                    blocks[index].bytes);
    // 同じフレームの先の描画が読み終えてから書く（遷移の barrier が待つ）。先に積んだ描画は前の中身を読む。
    if (localLightState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
        const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(localLightBuffer_.Get(), localLightState_,
                                                                 D3D12_RESOURCE_STATE_COPY_DEST);
        graphics.cmdList->ResourceBarrier(1, &toCopy);
    }
    graphics.cmdList->CopyBufferRegion(localLightBuffer_.Get(), 0, staging.resource, staging.offset, bytes);
    const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(localLightBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                             D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    graphics.cmdList->ResourceBarrier(1, &toRead);
    localLightState_ = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    residentLocalLightVersion_ = localLightVersion_;
    return true;
}

bool MeshViewFrameResources::PublishSkyEnvironment(const GFX::Context& graphics) {
    return PublishConstant(graphics, &skyEnvironment, sizeof(skyEnvironment), bindings_.skyEnvironment);
}

bool MeshViewFrameResources::PublishLight(const GFX::Context& graphics) {
    return PublishConstant(graphics, &light, sizeof(light), bindings_.light);
}

bool MeshViewFrameResources::PublishConstant(const GFX::Context& graphics, const void* data, size_t bytes,
                                             D3D12_GPU_VIRTUAL_ADDRESS& binding) {
    if (!IsInitialized() || bindings_.camera == 0u)
        return false;
    uploads_.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const DX::UploadAllocation allocation = uploads_.Allocate(bytes);
    if (!allocation)
        return false;
    // 段階切替では変更した CB だけを提出し、以前の GPU 入力を上書きしない。
    std::memcpy(allocation.cpuAddress, data, bytes);
    binding = allocation.gpuAddress;
    return true;
}

} // namespace HIKARI::MESHRENDERER
