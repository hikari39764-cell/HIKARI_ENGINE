#include "Render3D/WorldSprite/HIKARI_WorldSpriteMaterials.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <d3dx12.h>

#include "Assets/Shader/HIKARI_ProjectSpriteShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {
namespace {
constexpr std::array<WorldSpritePipeline, 3u> kMaterialKinds{WorldSpritePipeline::Opaque, WorldSpritePipeline::Blend,
                                                             WorldSpritePipeline::DepthOfFieldCapture};
constexpr size_t kPipelineCacheCapacity = 64u;
} // namespace

ID3D12PipelineState* WorldSpriteMaterialPipelines::Get(WorldSpritePipeline kind) const noexcept {
    const size_t index = kind == WorldSpritePipeline::DepthOfFieldCapture ? 2u
                         : kind == WorldSpritePipeline::Blend             ? 1u
                                                                          : 0u;
    return states[index].Get();
}

struct WorldSpriteMaterials::Impl {
    struct Entry {
        std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact> artifact;
        std::shared_ptr<const WorldSpriteMaterialPipelines> pipelines;
        uint64_t lastUse = 0u;
    };
    struct ParameterFrame {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        DX::DynamicUploadBuffer staging;
        std::vector<MATH::Vec4> published;
        uint64_t capacityBytes = 0u;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };
    GFX::Context context{};
    std::unordered_map<const ASSETS::SHADER::ShaderCookedArtifact*, Entry> cache;
    std::array<ParameterFrame, GFX::kFrameResourceCount> frames;
    uint64_t registryId = 0u, registryRevision = 0u, useSequence = 0u;
    uint32_t diagnosticsRemaining = 8u;

    void Retire(const Entry& entry) {
        if (!entry.pipelines)
            return;
        for (auto state : entry.pipelines->states)
            GFX::RetireD3D12ObjectForFrame(state, context, "WorldSpriteProjectPipeline");
    }
    void ClearCache() {
        for (const auto& [key, entry] : cache) {
            (void)key;
            Retire(entry);
        }
        cache.clear();
    }
    void Release(ParameterFrame& frame) {
        frame.staging.Finalize(&context);
        GFX::RetireD3D12ObjectForFrame(frame.buffer, context, "WorldSpriteParameters");
        frame.published.clear();
        frame.capacityBytes = 0u;
        frame.state = D3D12_RESOURCE_STATE_COMMON;
    }
};

WorldSpriteMaterials::WorldSpriteMaterials() : impl_(std::make_unique<Impl>()) {}
WorldSpriteMaterials::~WorldSpriteMaterials() { Shutdown(); }

void WorldSpriteMaterials::BeginFrame(const GFX::Context& context, uint64_t registryId, uint64_t registryRevision) {
    auto& data = *impl_;
    if (data.context.device && data.context.device != context.device)
        Shutdown();
    data.context = context;
    if (data.registryId != registryId || data.registryRevision != registryRevision) {
        data.ClearCache();
        data.registryId = registryId;
        data.registryRevision = registryRevision;
        data.diagnosticsRemaining = 8u;
    }
}

void WorldSpriteMaterials::Shutdown() noexcept {
    auto& data = *impl_;
    data.ClearCache();
    for (auto& frame : data.frames)
        data.Release(frame);
    data.context = {};
    data.registryId = data.registryRevision = data.useSequence = 0u;
}

std::shared_ptr<const WorldSpriteMaterialPipelines> WorldSpriteMaterials::Resolve(
    const std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact>& artifact, ID3D12RootSignature* root,
    bool meshPipeline, std::span<const uint8_t> geometry) {
    auto& data = *impl_;
    if (!artifact || !data.context.device || !root)
        return {};
    if (auto found = data.cache.find(artifact.get()); found != data.cache.end()) {
        found->second.lastUse = ++data.useSequence;
        return found->second.pipelines;
    }
    if (data.cache.size() >= kPipelineCacheCapacity) {
        const auto oldest =
            std::min_element(data.cache.begin(), data.cache.end(),
                             [](const auto& lhs, const auto& rhs) { return lhs.second.lastUse < rhs.second.lastUse; });
        data.Retire(oldest->second);
        data.cache.erase(oldest);
    }
    Impl::Entry entry{artifact, {}, ++data.useSequence};
    std::string diagnostic;
    if (ASSETS::SHADER::ValidateProjectSpriteShaderArtifact(*artifact, diagnostic)) {
        auto pipelines = std::make_shared<WorldSpriteMaterialPipelines>();
        bool ready = true;
        for (size_t index = 0u; index < kMaterialKinds.size(); ++index) {
            const auto& shader = artifact->entries[index == 2u ? 1u : 0u];
            if (!CreateWorldSpriteProjectPipeline(data.context.device, meshPipeline, root, kMaterialKinds[index],
                                                  geometry, shader.dxil, pipelines->states[index])) {
                ready = false;
                diagnostic = "unable to create the Sprite color/DOF pipeline pair";
                break;
            }
        }
        if (ready)
            entry.pipelines = std::move(pipelines);
    }
    if (!entry.pipelines && data.diagnosticsRemaining > 0u) {
        --data.diagnosticsRemaining;
        HIKARI_LOG_ERROR("[SpriteMaterial] " + artifact->sourceGuid.value + ": " + diagnostic +
                         "; using the built-in Sprite shader.");
    }
    const auto result = entry.pipelines;
    data.cache.emplace(artifact.get(), std::move(entry));
    return result;
}

bool WorldSpriteMaterials::UploadParameters(std::span<const MATH::Vec4> values, uint64_t& uploadedBytes) {
    auto& data = *impl_;
    auto& frame = data.frames[data.context.frameIndex % data.frames.size()];
    if (values.empty()) {
        data.Release(frame);
        return true;
    }
    const uint64_t bytes = values.size_bytes();
    if (frame.capacityBytes < bytes) {
        data.Release(frame);
        uint64_t capacity = 256u;
        while (capacity < bytes)
            capacity *= 2u;
        if (!GFX::D3D12_BUFFER::CreateDefaultBuffer(data.context.device, capacity, frame.buffer) ||
            !frame.staging.Init(data.context.device, 64u * 1024u)) {
            data.Release(frame);
            return false;
        }
        frame.capacityBytes = capacity;
    }
    if (frame.published.size() == values.size() && std::memcmp(frame.published.data(), values.data(), bytes) == 0)
        return true;
    frame.staging.ResetForFrame(data.context.currentFrameRetireFenceValue);
    const auto upload = frame.staging.Allocate(bytes);
    if (!upload || !data.context.cmdList)
        return false;
    std::memcpy(upload.cpuAddress, values.data(), bytes);
    const auto before =
        CD3DX12_RESOURCE_BARRIER::Transition(frame.buffer.Get(), frame.state, D3D12_RESOURCE_STATE_COPY_DEST);
    data.context.cmdList->ResourceBarrier(1u, &before);
    data.context.cmdList->CopyBufferRegion(frame.buffer.Get(), 0u, upload.resource, upload.offset, bytes);
    const auto after = CD3DX12_RESOURCE_BARRIER::Transition(frame.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    data.context.cmdList->ResourceBarrier(1u, &after);
    frame.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    frame.published.assign(values.begin(), values.end());
    uploadedBytes += bytes;
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS WorldSpriteMaterials::GetParameterAddress(D3D12_GPU_VIRTUAL_ADDRESS fallback) const noexcept {
    const auto& frame = impl_->frames[impl_->context.frameIndex % impl_->frames.size()];
    return frame.buffer ? frame.buffer->GetGPUVirtualAddress() : fallback;
}

uint64_t WorldSpriteMaterials::GetResidentBytes() const noexcept {
    uint64_t bytes = 0u;
    for (const auto& frame : impl_->frames)
        bytes += frame.capacityBytes;
    return bytes;
}

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
