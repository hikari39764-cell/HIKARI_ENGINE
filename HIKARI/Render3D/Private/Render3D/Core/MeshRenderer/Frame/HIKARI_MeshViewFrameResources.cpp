#include "Render3D/Core/MeshRenderer/Frame/HIKARI_MeshViewFrameResources.h"

#include <array>
#include <cstring>
#include <utility>
#include "Gfx/D3D12/HIKARI_D3D12BufferAlignment.h"

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
    return uploads_.Init(device, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void MeshViewFrameResources::Finalize() {
    uploads_.Finalize(&GFX::GetCurrentContext());
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
    struct Block {
        const void* data;
        size_t bytes;
    };
    // 一回の割当で全 CBV/SRV を公開する。失敗時に異なる view の値を混在させない。
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
    size_t bytes = 0u;
    for (const Block& block : blocks)
        bytes += GFX::AlignD3D12ConstantBufferByteSize(static_cast<UINT>(block.bytes));
    uploads_.ResetForFrame(graphics.currentFrameRetireFenceValue);
    const DX::UploadAllocation allocation = uploads_.Allocate(bytes);
    if (!allocation)
        return false;
    std::array<D3D12_GPU_VIRTUAL_ADDRESS, blocks.size()> addresses{};
    size_t offset = 0u;
    for (size_t index = 0u; index < blocks.size(); ++index) {
        std::memcpy(static_cast<std::byte*>(allocation.cpuAddress) + offset, blocks[index].data, blocks[index].bytes);
        addresses[index] = allocation.gpuAddress + offset;
        offset += GFX::AlignD3D12ConstantBufferByteSize(static_cast<UINT>(blocks[index].bytes));
    }
    bindings_ = {addresses[0], addresses[1], addresses[2],
                 addresses[3], addresses[4], {addresses[5], addresses[6], addresses[7], localLightList}};
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
