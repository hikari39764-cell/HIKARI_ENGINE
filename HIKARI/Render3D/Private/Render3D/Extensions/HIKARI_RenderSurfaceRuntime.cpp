#include "Render3D/Extensions/HIKARI_RenderSurfaceRuntime.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshRootParameters.h"
#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "RenderContracts/Shader/HIKARI_ShaderCookedArtifact.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::SURFACE_EXTENSIONS {
namespace {

using Microsoft::WRL::ComPtr;
namespace SHADER = ASSETS::SHADER;

constexpr size_t kUploadPageBytes = 4u * 1024u * 1024u;
constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// 着色器の RenderSurfaceConstants（b3）と同じ並び。
struct DrawConstants {
    MATH::Mat4 viewProjection{};
    MATH::Vec3 cameraPosition{};
    uint32_t pass = 0u;
    std::array<uint8_t, kRenderSurfaceMaximumConstantBytes> module{};
};

struct SurfaceResource {
    ComPtr<ID3D12Resource> resource{};
    RenderSurfaceTextureArrayDesc texture{};
    bool isTexture = false;
    D3D12_INDEX_BUFFER_VIEW indexView{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool copyPending = false;
    uint64_t revision = 0u;
};

struct DescriptorTable {
    std::vector<RenderResourceView> views{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
};

DXGI_FORMAT ToDxgi(RenderSurfaceTextureFormat format) noexcept {
    switch (format) {
    case RenderSurfaceTextureFormat::R16Unorm: return DXGI_FORMAT_R16_UNORM;
    case RenderSurfaceTextureFormat::R16G16Snorm: return DXGI_FORMAT_R16G16_SNORM;
    case RenderSurfaceTextureFormat::R32Uint: return DXGI_FORMAT_R32_UINT;
    case RenderSurfaceTextureFormat::R8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

uint32_t BytesPerTexel(RenderSurfaceTextureFormat format) noexcept {
    return format == RenderSurfaceTextureFormat::R16Unorm ? 2u : 4u;
}

struct RuntimeState {
    GFX::Context graphics{};
    uint64_t deviceGeneration = 0u;
    std::vector<IRenderSurfaceExtension*> extensions{};
    std::map<RenderSurfaceResourceId, SurfaceResource> resources{};
    RenderSurfaceResourceId nextResource = 1u;
    std::map<std::string, ComPtr<ID3D12PipelineState>> pipelines{};
    std::map<std::string, bool> failedPrograms{};
    std::map<std::array<RenderSurfaceResourceId, kRenderSurfaceTextureSlotCount>, DescriptorTable> tables{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
    uint64_t lastFrameIndex = UINT64_MAX;
    uint32_t slot = 0u;
    MATH::Vec3 cameraPosition{};
};

RuntimeState& State() noexcept {
    static RuntimeState state{};
    return state;
}

void Retire(const GFX::Context& graphics, ComPtr<ID3D12Resource>& resource) {
    GFX::RetireD3D12ObjectForFrame(resource, graphics, "RenderSurface.Resource");
}

void ReleaseTable(DescriptorTable& table) noexcept {
    for (const RenderResourceView& view : table.views) {
        (void)ReleaseRenderResourceDescriptor(view);
    }
    table.views.clear();
}

void ReleaseAll(RuntimeState& state, const GFX::Context* graphics) noexcept {
    try {
        for (auto& [id, resource] : state.resources) {
            if (graphics != nullptr) {
                Retire(*graphics, resource.resource);
            }
        }
        state.resources.clear();
        for (auto& [key, table] : state.tables) {
            ReleaseTable(table);
        }
        state.tables.clear();
        state.pipelines.clear();
        state.failedPrograms.clear();
    } catch (...) {
    }
}

template <class Call>
void CallModule(const char* what, Call&& call) noexcept {
    try {
        call();
    } catch (const std::exception& exception) {
        HIKARI_LOG_ERROR(std::string("[RenderSurface] ") + what + " threw: " + exception.what());
    } catch (...) {
        HIKARI_LOG_ERROR(std::string("[RenderSurface] ") + what + " threw an unknown exception.");
    }
}

class Resources final : public IRenderSurfaceResources {
public:
    explicit Resources(RuntimeState& state) : state_(state) {}

    RenderSurfaceResourceId CreateTextureArray(const RenderSurfaceTextureArrayDesc& desc) override {
        const DXGI_FORMAT format = ToDxgi(desc.format);
        if (format == DXGI_FORMAT_UNKNOWN || desc.width == 0u || desc.height == 0u || desc.layerCount == 0u ||
            desc.layerCount > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION || desc.mipCount == 0u) {
            return 0u;
        }
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            format, desc.width, desc.height, static_cast<UINT16>(desc.layerCount), static_cast<UINT16>(desc.mipCount));
        SurfaceResource resource{};
        resource.isTexture = true;
        resource.texture = desc;
        resource.state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 state_.graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &resourceDesc, resource.state,
                                 nullptr, GFX::VIDEO_MEMORY::Owner::Geometry,
                                 IID_PPV_ARGS(resource.resource.GetAddressOf())),
                             "RenderSurface::CreateTextureArray")) {
            return 0u;
        }
        GFX::SetD3D12Name(resource.resource.Get(), L"HIKARI RenderSurface Texture Array");
        resource.copyPending = true;
        const RenderSurfaceResourceId id = state_.nextResource++;
        state_.resources.emplace(id, std::move(resource));
        return id;
    }

    bool UploadTexture(RenderSurfaceResourceId texture, const RenderSurfaceTextureUpload& upload) override {
        const auto found = state_.resources.find(texture);
        if (found == state_.resources.end() || !found->second.isTexture || upload.pixels == nullptr) {
            return false;
        }
        SurfaceResource& resource = found->second;
        const RenderSurfaceTextureArrayDesc& desc = resource.texture;
        const uint32_t mipWidth = (std::max)(desc.width >> upload.mip, 1u);
        const uint32_t mipHeight = (std::max)(desc.height >> upload.mip, 1u);
        const uint32_t texelBytes = BytesPerTexel(desc.format);
        if (upload.mip >= desc.mipCount || upload.layer >= desc.layerCount || upload.width == 0u ||
            upload.height == 0u || upload.x + upload.width > mipWidth || upload.y + upload.height > mipHeight ||
            upload.rowPitchBytes < upload.width * texelBytes) {
            return false;
        }
        TransitionTo(resource, D3D12_RESOURCE_STATE_COPY_DEST);
        const uint32_t rowBytes = upload.width * texelBytes;
        const uint32_t pitch =
            (rowBytes + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        const size_t bytes = static_cast<size_t>(pitch) * upload.height + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        const DX::UploadAllocation allocation = state_.uploads[state_.slot].Allocate(bytes);
        if (!allocation) {
            return false;
        }
        const size_t aligned = (allocation.offset + (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u)) &
                               ~static_cast<size_t>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
        uint8_t* target = static_cast<uint8_t*>(allocation.cpuAddress) + (aligned - allocation.offset);
        const uint8_t* source = static_cast<const uint8_t*>(upload.pixels);
        for (uint32_t row = 0u; row < upload.height; ++row) {
            std::memcpy(target + static_cast<size_t>(row) * pitch,
                        source + static_cast<size_t>(row) * upload.rowPitchBytes, rowBytes);
        }
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = resource.resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex =
            D3D12CalcSubresource(upload.mip, upload.layer, 0u, desc.mipCount, desc.layerCount);
        D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
        sourceLocation.pResource = allocation.resource;
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sourceLocation.PlacedFootprint.Offset = aligned;
        sourceLocation.PlacedFootprint.Footprint = {ToDxgi(desc.format), upload.width, upload.height, 1u, pitch};
        state_.graphics.cmdList->CopyTextureRegion(&destination, upload.x, upload.y, 0u, &sourceLocation, nullptr);
        resource.copyPending = true;
        return true;
    }

    RenderSurfaceResourceId CreateIndexBuffer(std::span<const uint16_t> indices) override {
        if (indices.empty()) {
            return 0u;
        }
        const UINT64 bytes = indices.size_bytes();
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
        SurfaceResource resource{};
        resource.state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 state_.graphics.device, &heap, D3D12_HEAP_FLAG_NONE, &desc, resource.state, nullptr,
                                 GFX::VIDEO_MEMORY::Owner::Geometry, IID_PPV_ARGS(resource.resource.GetAddressOf())),
                             "RenderSurface::CreateIndexBuffer")) {
            return 0u;
        }
        const DX::UploadAllocation allocation = state_.uploads[state_.slot].Allocate(static_cast<size_t>(bytes));
        if (!allocation) {
            return 0u;
        }
        std::memcpy(allocation.cpuAddress, indices.data(), static_cast<size_t>(bytes));
        state_.graphics.cmdList->CopyBufferRegion(resource.resource.Get(), 0u, allocation.resource, allocation.offset,
                                                  bytes);
        resource.indexView = {resource.resource->GetGPUVirtualAddress(), static_cast<UINT>(bytes),
                              DXGI_FORMAT_R16_UINT};
        TransitionTo(resource, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        const RenderSurfaceResourceId id = state_.nextResource++;
        state_.resources.emplace(id, std::move(resource));
        return id;
    }

    void Release(RenderSurfaceResourceId id) override {
        const auto found = state_.resources.find(id);
        if (found == state_.resources.end()) {
            return;
        }
        Retire(state_.graphics, found->second.resource);
        state_.resources.erase(found);
        for (auto table = state_.tables.begin(); table != state_.tables.end();) {
            if (std::find(table->first.begin(), table->first.end(), id) != table->first.end()) {
                ReleaseTable(table->second);
                table = state_.tables.erase(table);
            } else {
                ++table;
            }
        }
    }

    void FinishCopies() {
        for (auto& [id, resource] : state_.resources) {
            if (resource.isTexture && resource.copyPending) {
                TransitionTo(resource, kShaderReadState);
                resource.copyPending = false;
            }
        }
    }

private:
    void TransitionTo(SurfaceResource& resource, D3D12_RESOURCE_STATES next) {
        if (resource.state == next) {
            return;
        }
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.resource.Get(), resource.state, next);
        state_.graphics.cmdList->ResourceBarrier(1u, &barrier);
        resource.state = next;
    }

    RuntimeState& state_;
};

// RenderGeometryShaderContract の合同で内蔵着色器を解決する。
bool ResolveEntry(RuntimeState& state, const char* programId, const char* entry, SHADER::ShaderStage stage,
                  const char* profile, SHADER::ShaderAssetEntryLease& outLease) {
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(state.graphics.device);
    const auto contract = SHADER::MakeRenderGeometryShaderContract(
        SHADER::RenderGeometryRootLayout::ModuleSurfaceDraw,
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    auto resolved = SHADER::ResolveBuiltinShaderEntry(programId, {entry, stage, profile}, contract);
    if (!resolved || resolved.lease.GetDxil().empty()) {
        HIKARI_LOG_ERROR(std::string("[RenderSurface] ") + programId + " " + entry + ": " + resolved.diagnostic);
        return false;
    }
    outLease = std::move(resolved.lease);
    return true;
}

ID3D12PipelineState* ResolvePipeline(RuntimeState& state, ID3D12RootSignature* rootSignature, const char* programId,
                                     RenderSurfacePass pass, bool featureMask) {
    const std::string program(programId);
    const std::string key = program + "#" + std::to_string(static_cast<uint32_t>(pass)) + (featureMask ? "f" : "");
    if (const auto found = state.pipelines.find(key); found != state.pipelines.end()) {
        return found->second.Get();
    }
    if (state.failedPrograms.contains(key)) {
        return nullptr;
    }
    state.failedPrograms.emplace(key, true);

    SHADER::ShaderAssetEntryLease vertex{};
    SHADER::ShaderAssetEntryLease pixel{};
    if (!ResolveEntry(state, programId, "SurfaceVS", SHADER::ShaderStage::Vertex, "vs_6_0", vertex)) {
        return nullptr;
    }
    const char* pixelEntry = pass == RenderSurfacePass::Forward           ? "SurfaceForwardPS"
                             : pass != RenderSurfacePass::GeometrySurface ? nullptr
                             : featureMask                                ? "SurfaceGeometryFeaturePS"
                                                                          : "SurfaceGeometryPS";
    if (pixelEntry != nullptr &&
        !ResolveEntry(state, programId, pixelEntry, SHADER::ShaderStage::Pixel, "ps_6_0", pixel)) {
        return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature;
    const auto vertexDxil = vertex.GetDxil();
    description.VS = {vertexDxil.data(), vertexDxil.size()};
    if (pixelEntry != nullptr) {
        const auto pixelDxil = pixel.GetDxil();
        description.PS = {pixelDxil.data(), pixelDxil.size()};
    }
    description.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    description.SampleMask = UINT_MAX;
    description.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1u;
    if (pass == RenderSurfacePass::Forward) {
        description.NumRenderTargets = 1u;
        description.RTVFormats[0] = kSceneColorFormat;
    } else if (pass == RenderSurfacePass::GeometrySurface) {
        description.NumRenderTargets = featureMask ? 3u : 1u;
        description.RTVFormats[0] = kSceneColorFormat;
        if (featureMask) {
            description.RTVFormats[1] = DXGI_FORMAT_R32G32_UINT;
            description.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }
    ComPtr<ID3D12PipelineState> pipeline{};
    if (!HIKARI_DX_CHECK(
            state.graphics.device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf())),
            "RenderSurface::CreatePipeline")) {
        return nullptr;
    }
    GFX::SetD3D12Name(pipeline.Get(), L"HIKARI RenderSurface PSO");
    state.failedPrograms.erase(key);
    return state.pipelines.emplace(key, std::move(pipeline)).first->second.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE ResolveTable(RuntimeState& state, const RenderSurfaceDraw& draw) {
    std::array<RenderSurfaceResourceId, kRenderSurfaceTextureSlotCount> key{};
    std::copy(std::begin(draw.textures), std::end(draw.textures), key.begin());
    if (const auto found = state.tables.find(key); found != state.tables.end()) {
        return found->second.gpu;
    }
    DescriptorTable table{};
    table.views = AllocateRenderResourceDescriptorRange(kRenderSurfaceTextureSlotCount);
    if (table.views.size() != kRenderSurfaceTextureSlotCount) {
        return {};
    }
    for (uint32_t slot = 0u; slot < kRenderSurfaceTextureSlotCount; ++slot) {
        const auto found = state.resources.find(key[slot]);
        const bool valid = found != state.resources.end() && found->second.isTexture;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Format = valid ? ToDxgi(found->second.texture.format) : DXGI_FORMAT_R16_UNORM;
        view.Texture2DArray.MipLevels = valid ? found->second.texture.mipCount : 1u;
        view.Texture2DArray.ArraySize = valid ? found->second.texture.layerCount : 1u;
        state.graphics.device->CreateShaderResourceView(valid ? found->second.resource.Get() : nullptr, &view,
                                                        table.views[slot].cpu);
    }
    table.gpu = table.views.front().gpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu = table.gpu;
    state.tables.emplace(key, std::move(table));
    return gpu;
}

class Commands final : public IRenderSurfaceCommands {
public:
    Commands(RuntimeState& state, const GFX::Context& graphics, ID3D12RootSignature* rootSignature,
             const RecordInput& input)
        : state_(state), graphics_(graphics), rootSignature_(rootSignature), input_(input) {}

    bool Draw(const RenderSurfaceDraw& draw) override {
        if (draw.programId == nullptr || draw.instanceCount == 0u || draw.indexCount == 0u ||
            draw.instanceStride == 0u || draw.instances == nullptr ||
            draw.constantBytes > kRenderSurfaceMaximumConstantBytes ||
            (draw.constantBytes != 0u && draw.constants == nullptr)) {
            return false;
        }
        const auto index = state_.resources.find(draw.indexBuffer);
        if (index == state_.resources.end() || index->second.isTexture) {
            return false;
        }
        ID3D12PipelineState* pipeline =
            ResolvePipeline(state_, rootSignature_, draw.programId, input_.pass, input_.geometryFeatureMask);
        const D3D12_GPU_DESCRIPTOR_HANDLE table = ResolveTable(state_, draw);
        if (pipeline == nullptr || table.ptr == 0u) {
            return false;
        }
        DX::DynamicUploadBuffer& upload = state_.uploads[state_.slot];
        DrawConstants constants{};
        constants.viewProjection = input_.viewProjection;
        constants.cameraPosition = input_.cameraPosition;
        constants.pass = static_cast<uint32_t>(input_.pass);
        if (draw.constantBytes != 0u) {
            std::memcpy(constants.module.data(), draw.constants, draw.constantBytes);
        }
        const DX::UploadAllocation constantAllocation = upload.Allocate(sizeof(constants));
        const size_t instanceBytes = static_cast<size_t>(draw.instanceStride) * draw.instanceCount;
        const DX::UploadAllocation instanceAllocation = upload.Allocate(instanceBytes);
        if (!constantAllocation || !instanceAllocation) {
            return false;
        }
        std::memcpy(constantAllocation.cpuAddress, &constants, sizeof(constants));
        std::memcpy(instanceAllocation.cpuAddress, draw.instances, instanceBytes);

        ID3D12GraphicsCommandList* cmd = graphics_.cmdList;
        cmd->SetPipelineState(pipeline);
        cmd->SetGraphicsRootConstantBufferView(MESHRENDERER::ROOT_PARAM::MeshletVisibleRanges,
                                               constantAllocation.gpuAddress);
        cmd->SetGraphicsRootShaderResourceView(MESHRENDERER::ROOT_PARAM::JointPalette, instanceAllocation.gpuAddress);
        cmd->SetGraphicsRootDescriptorTable(MESHRENDERER::ROOT_PARAM::ClusterGeometryPool, table);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetIndexBuffer(&index->second.indexView);
        cmd->DrawIndexedInstanced(draw.indexCount, draw.instanceCount, 0u, 0, 0u);
        return true;
    }

private:
    RuntimeState& state_;
    const GFX::Context& graphics_;
    ID3D12RootSignature* rootSignature_ = nullptr;
    const RecordInput& input_;
};

} // namespace

void BeginFrame(const GFX::Context& graphics, std::span<IRenderSurfaceExtension* const> extensions) {
    RuntimeState& state = State();
    state.extensions.assign(extensions.begin(), extensions.end());
    if (state.extensions.empty() || graphics.device == nullptr || graphics.cmdList == nullptr) {
        state.extensions.clear();
        return;
    }
    if (state.deviceGeneration != graphics.deviceGeneration) {
        if (state.deviceGeneration != 0u) {
            ReleaseAll(state, nullptr);
            for (IRenderSurfaceExtension* extension : state.extensions) {
                CallModule("OnRenderResourcesLost", [&] { extension->OnRenderResourcesLost(); });
            }
            for (DX::DynamicUploadBuffer& upload : state.uploads) {
                upload = DX::DynamicUploadBuffer{};
            }
        }
        state.deviceGeneration = graphics.deviceGeneration;
    }
    state.graphics = graphics;
    state.slot = static_cast<uint32_t>(graphics.frameIndex % GFX::kFrameResourceCount);
    DX::DynamicUploadBuffer& upload = state.uploads[state.slot];
    if (!upload.IsInitialized() &&
        !upload.Init(graphics.device, kUploadPageBytes, GFX::UPLOAD_TRAFFIC::Owner::SurfaceExtensions)) {
        state.extensions.clear();
        return;
    }
    // graphics.frameIndex は枠の番号なので、巻き戻しの判定には通しのフレーム番号を使う。
    const uint64_t frameSerial = TIME::GetFrameContext().frameIndex;
    if (state.lastFrameIndex != frameSerial) {
        upload.ResetForFrame(frameSerial);
        state.lastFrameIndex = frameSerial;
    }
    Resources resources(state);
    for (IRenderSurfaceExtension* extension : state.extensions) {
        CallModule("PrepareFrame", [&] { extension->PrepareFrame(resources); });
    }
    resources.FinishCopies();
}

bool HasWork() noexcept { return !State().extensions.empty(); }

void SetFrameCameraPosition(const MATH::Vec3& position) noexcept { State().cameraPosition = position; }

const MATH::Vec3& GetFrameCameraPosition() noexcept { return State().cameraPosition; }

bool Record(const GFX::Context& graphics, ID3D12RootSignature* rootSignature, const RecordInput& input) {
    RuntimeState& state = State();
    if (state.extensions.empty()) {
        return true;
    }
    if (rootSignature == nullptr || graphics.cmdList == nullptr) {
        return false;
    }
    ID3D12DescriptorHeap* heap = GetTextureResourceSrvHeap();
    if (heap == nullptr) {
        return false;
    }
    graphics.cmdList->SetDescriptorHeaps(1u, &heap);
    RenderSurfaceView view{};
    view.pass = input.pass;
    view.shadowCascade = input.shadowCascade;
    std::memcpy(view.viewProjection, &input.viewProjection, sizeof(view.viewProjection));
    view.cameraPosition[0] = input.cameraPosition.x;
    view.cameraPosition[1] = input.cameraPosition.y;
    view.cameraPosition[2] = input.cameraPosition.z;
    Commands commands(state, graphics, rootSignature, input);
    for (IRenderSurfaceExtension* extension : state.extensions) {
        CallModule("Record", [&] { extension->Record(view, commands); });
    }
    return true;
}

void Shutdown(const GFX::Context* graphics) noexcept {
    RuntimeState& state = State();
    ReleaseAll(state, graphics);
    state.extensions.clear();
    state.deviceGeneration = 0u;
}

} // namespace HIKARI::RENDER3D::SURFACE_EXTENSIONS
