#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include "Render3D/WorldText/HIKARI_WorldTextPass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "RenderCore/Text/HIKARI_SdfTextLayout.h"

namespace HIKARI::RENDER3D::WORLD_TEXT {
namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kMaximumTextCount = 4096u;

// CPU/GPU とも 112-byte stride を共有する。float4 境界を維持して、
// shader 側で個別 field の packing rule を推測しない。
struct GpuWorldTextGlyphInstance {
    MATH::Vec4 position{};
    MATH::Vec4 orientation{};
    MATH::Vec4 localRect{};
    MATH::Vec4 uvRect{};
    MATH::Vec4 fillColorOpacity{};
    MATH::Vec4 outlineColorWidth{};
    uint32_t textureDescriptorIndex = 0u;
    uint32_t facingMode = 0u;
    uint32_t reserved0 = 0u;
    uint32_t reserved1 = 0u;
};

static_assert(sizeof(GpuWorldTextGlyphInstance) == 112u);

struct DrawConstants {
    uint32_t baseIndex = 0u;
    uint32_t count = 0u;
    // 文字色は表示参照。ビューの P/E を掛けて HDR 目標へ書く。
    float displayToScene = 1.0f;
    uint32_t reserved = 0u;
};

struct PreparedText {
    std::vector<GpuWorldTextGlyphInstance> glyphs{};
    int32_t sortPriority = 0;
    float distanceSquared = 0.0f;
    uint32_t stableOrder = 0u;
};

struct DrawBatch {
    uint32_t baseIndex = 0u;
    uint32_t count = 0u;
};

struct WorldTextPipelineStateStream {
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_MS meshShader;
    CD3DX12_PIPELINE_STATE_STREAM_PS pixelShader;
    CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
    CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
    CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDescription;
    CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK sampleMask;
};

bool CreateRootSignature(ID3D12Device* device, ID3D12RootSignature** output) {
    if (device == nullptr || output == nullptr)
        return false;

    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
    textureRange.BaseShaderRegister = 20u;

    std::array<D3D12_ROOT_PARAMETER, 4u> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0u;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 1u;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 1u;
    parameters[3].Constants.Num32BitValues = 4u;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0u;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1u;
    description.pStaticSamplers = &sampler;
    return HIKARI_DX_CHECK(GFX::CreateRootSignature(device, description, output), "WorldTextPass::CreateRootSignature");
}

bool CreateUploadBuffer(ID3D12Device* device, uint32_t capacity, ComPtr<ID3D12Resource>& outResource,
                        GpuWorldTextGlyphInstance*& outMapped) {
    outMapped = nullptr;
    const UINT64 size = static_cast<UINT64>(capacity) * sizeof(GpuWorldTextGlyphInstance);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = CD3DX12_RESOURCE_DESC::Buffer(size);
    if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                             device, &heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
                             nullptr, GFX::VIDEO_MEMORY::Owner::Vfx, IID_PPV_ARGS(outResource.GetAddressOf())),
                         "WorldTextPass::CreateUploadBuffer")) {
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE noRead{0u, 0u};
    if (!HIKARI_DX_CHECK(outResource->Map(0u, &noRead, &mapped), "WorldTextPass::MapUploadBuffer")) {
        outResource.Reset();
        return false;
    }
    outMapped = static_cast<GpuWorldTextGlyphInstance*>(mapped);
    return true;
}

bool ResolveShaders(ID3D12Device* device, ASSETS::SHADER::ShaderAssetEntryLease& outMesh,
                    ASSETS::SHADER::ShaderAssetEntryLease& outPixel) {
    const GFX::ShaderDeviceCapabilities capabilities = GFX::QueryShaderDeviceCapabilities(device);
    const auto contract = ASSETS::SHADER::MakeRenderGeometryShaderContract(
        ASSETS::SHADER::RenderGeometryRootLayout::WorldTextDraw,
        {capabilities.shaderModel, capabilities.rootSignatureVersion, capabilities.featureMask});
    const auto resolve = [&contract](const char* entry, ASSETS::SHADER::ShaderStage stage, const char* profile,
                                     ASSETS::SHADER::ShaderAssetEntryLease& outLease) {
        auto resolved = ASSETS::SHADER::ResolveBuiltinShaderEntry("HIKARI.EngineShader.Render3D_WorldTextDraw",
                                                                  {entry, stage, profile}, contract);
        std::string diagnostic{};
        const auto* artifact = resolved ? resolved.lease.GetArtifact() : nullptr;
        if (!resolved || artifact == nullptr ||
            !ASSETS::SHADER::ValidateRenderGeometryShaderArtifact(*artifact, diagnostic) ||
            resolved.lease.GetDxil().empty()) {
            if (diagnostic.empty())
                diagnostic = resolved.diagnostic;
            HIKARI_LOG_ERROR(std::string("[WorldTextPass][Shader] ") + entry + ": " + diagnostic);
            return false;
        }
        outLease = std::move(resolved.lease);
        return true;
    };
    return resolve("WorldTextMS", ASSETS::SHADER::ShaderStage::Mesh, "ms_6_5", outMesh) &&
           resolve("WorldTextPS", ASSETS::SHADER::ShaderStage::Pixel, "ps_6_0", outPixel);
}

float ResolveOutlineFieldWidth(float outlineWidth) noexcept {
    const float fieldWidth =
        outlineWidth * TEXT_RENDERING::kSdfTextAtlasPixelHeight * TEXT_RENDERING::kSdfTextPixelDistanceScale / 255.0f;
    return std::clamp(fieldWidth, 0.0f, 0.49f);
}
} // namespace

struct WorldTextPass::Impl {
    GFX::Context context{};
    ComPtr<ID3D12RootSignature> rootSignature{};
    ComPtr<ID3D12PipelineState> pipeline{};
    std::array<ComPtr<ID3D12Resource>, GFX::kFrameResourceCount> uploadBuffers{};
    std::array<GpuWorldTextGlyphInstance*, GFX::kFrameResourceCount> mappedGlyphs{};
    std::vector<DrawBatch> batches{};
    TEXT_RENDERING::SdfTextLayoutLease atlasLease{};
    std::vector<TEXT_RENDERING::SdfTextLayoutRequest> cachedLayoutRequests{};
    std::vector<TEXT_RENDERING::SdfTextGlyphDraw> layoutGlyphs{};
    TEXT_RENDERING::SdfTextLayoutStats layoutStats{};
    uint64_t layoutWorldIdentity{};
    bool layoutCacheValid{};
    WorldTextPassStats stats{};
    uint32_t capacity = 0u;
    uint32_t activeFrameSlot = 0u;
    bool initialized = false;
    bool initializationAttempted = false;

    bool CreatePipeline(ID3D12Device* device) {
        if (!CreateRootSignature(device, rootSignature.GetAddressOf())) {
            return false;
        }
        ASSETS::SHADER::ShaderAssetEntryLease meshShader{};
        ASSETS::SHADER::ShaderAssetEntryLease pixelShader{};
        if (!ResolveShaders(device, meshShader, pixelShader)) {
            return false;
        }
        ComPtr<ID3D12Device2> device2{};
        if (!HIKARI_DX_CHECK(device->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf())),
                             "WorldTextPass::QueryDevice2")) {
            return false;
        }

        CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
        auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        CD3DX12_DEPTH_STENCIL_DESC depth(D3D12_DEFAULT);
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
        rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        D3D12_RT_FORMAT_ARRAY formats{};
        formats.NumRenderTargets = 1u;
        formats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

        const std::span<const uint8_t> meshDxil = meshShader.GetDxil();
        const std::span<const uint8_t> pixelDxil = pixelShader.GetDxil();
        WorldTextPipelineStateStream stream{};
        stream.rootSignature = rootSignature.Get();
        stream.meshShader = CD3DX12_SHADER_BYTECODE(meshDxil.data(), meshDxil.size());
        stream.pixelShader = CD3DX12_SHADER_BYTECODE(pixelDxil.data(), pixelDxil.size());
        stream.blend = blend;
        stream.depthStencil = depth;
        stream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        stream.rasterizer = rasterizer;
        stream.rtvFormats = formats;
        stream.sampleDescription = DXGI_SAMPLE_DESC{1u, 0u};
        stream.sampleMask = (std::numeric_limits<UINT>::max)();
        D3D12_PIPELINE_STATE_STREAM_DESC description{};
        description.SizeInBytes = sizeof(stream);
        description.pPipelineStateSubobjectStream = &stream;
        return HIKARI_DX_CHECK(device2->CreatePipelineState(&description, IID_PPV_ARGS(pipeline.GetAddressOf())),
                               "WorldTextPass::CreatePipeline");
    }

    bool Initialize(const GFX::Context& newContext, uint32_t newCapacity) {
        Shutdown();
        context = newContext;
        capacity = std::clamp(newCapacity, 1u, 65535u);
        if (context.device == nullptr || !CreatePipeline(context.device)) {
            Shutdown();
            return false;
        }
        for (uint32_t index = 0u; index < GFX::kFrameResourceCount; ++index) {
            if (!CreateUploadBuffer(context.device, capacity, uploadBuffers[index], mappedGlyphs[index])) {
                Shutdown();
                return false;
            }
        }
        initialized = true;
        stats.ready = true;
        return true;
    }

    void Shutdown() noexcept {
        for (uint32_t index = 0u; index < GFX::kFrameResourceCount; ++index) {
            if (uploadBuffers[index] && mappedGlyphs[index]) {
                uploadBuffers[index]->Unmap(0u, nullptr);
            }
            mappedGlyphs[index] = nullptr;
            uploadBuffers[index].Reset();
        }
        batches.clear();
        atlasLease.Reset();
        cachedLayoutRequests.clear();
        layoutGlyphs.clear();
        layoutStats = {};
        layoutCacheValid = false;
        layoutWorldIdentity = 0;
        pipeline.Reset();
        rootSignature.Reset();
        stats = {};
        capacity = 0u;
        activeFrameSlot = 0u;
        initialized = false;
        initializationAttempted = false;
    }

    bool PrepareFrame(const GFX::Context& newContext, const WorldTextFrameSource* source,
                      const MATH::Vec3& cameraPosition) {
        context = newContext;
        const bool ready = stats.ready;
        stats = {};
        stats.ready = ready;
        batches.clear();
        if (source == nullptr || source->instances.empty()) {
            atlasLease.Reset();
            cachedLayoutRequests.clear();
            layoutGlyphs.clear();
            layoutCacheValid = false;
            return true;
        }
        stats.requestedTextCount = static_cast<uint32_t>(
            (std::min)(source->instances.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        if (!initialized || context.device == nullptr) {
            stats.droppedTextCount = stats.requestedTextCount;
            return true;
        }

        const uint32_t acceptedTextCount = (std::min)(
            static_cast<uint32_t>((std::min)(source->instances.size(), static_cast<size_t>(kMaximumTextCount))),
            kMaximumTextCount);
        stats.droppedTextCount = stats.requestedTextCount - acceptedTextCount;
        std::vector<TEXT_RENDERING::SdfTextLayoutRequest> requests{};
        std::vector<const WorldTextInstance*> requestInstances{};
        requests.reserve(acceptedTextCount);
        requestInstances.reserve(acceptedTextCount);
        for (uint32_t index = 0u; index < acceptedTextCount; ++index) {
            const WorldTextInstance& instance = source->instances[index];
            if (instance.utf8Text.empty() || instance.absoluteFontAssetPath.empty() || instance.opacity <= 0.0f ||
                instance.revealAmount <= 0.0f) {
                ++stats.droppedTextCount;
                continue;
            }
            TEXT_RENDERING::SdfTextLayoutRequest request{};
            request.absoluteFontAssetPath = instance.absoluteFontAssetPath;
            request.fontRevision = instance.fontRevision;
            request.utf8Text = instance.utf8Text;
            request.fontHeight = instance.fontHeight;
            request.maxWidth = instance.layoutSize.x;
            request.maxHeight = instance.layoutSize.y;
            request.lineHeightMultiplier = instance.lineHeightMultiplier;
            request.letterSpacing = instance.letterSpacing;
            request.revealAmount = instance.revealAmount;
            request.horizontalAlign = instance.horizontalAlign;
            request.verticalAlign = instance.verticalAlign;
            request.wrap = instance.wrap;
            request.submissionSequence = instance.stableOrder;
            requests.push_back(std::move(request));
            requestInstances.push_back(&instance);
        }

        if (!layoutCacheValid || layoutWorldIdentity != source->worldIdentity || requests != cachedLayoutRequests) {
            layoutCacheValid = false;
            if (!TEXT_RENDERING::BuildSdfTextGlyphDraws(requests, layoutGlyphs, &layoutStats, capacity, &atlasLease)) {
                stats.failedTextRequestCount = static_cast<uint32_t>(requests.size());
                stats.droppedTextCount += static_cast<uint32_t>(requests.size());
                return true;
            }
            cachedLayoutRequests = requests;
            layoutWorldIdentity = source->worldIdentity;
            layoutCacheValid = layoutStats.failedRequestCount == 0 && layoutStats.readyRequestCount == requests.size();
        }
        stats.failedTextRequestCount = layoutStats.failedRequestCount;
        stats.droppedGlyphCount = layoutStats.droppedGlyphCount;

        std::vector<PreparedText> prepared(requests.size());
        for (size_t index = 0u; index < prepared.size(); ++index) {
            const WorldTextInstance& instance = *requestInstances[index];
            PreparedText& text = prepared[index];
            text.sortPriority = instance.transparentSortPriority;
            text.stableOrder = instance.stableOrder;
            const MATH::Vec3 delta = instance.worldPosition - cameraPosition;
            text.distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            if (!std::isfinite(text.distanceSquared)) {
                text.distanceSquared = 0.0f;
            }
        }

        for (const TEXT_RENDERING::SdfTextGlyphDraw& glyph : layoutGlyphs) {
            if (glyph.requestIndex >= requestInstances.size()) {
                ++stats.droppedGlyphCount;
                continue;
            }
            const WorldTextInstance& instance = *requestInstances[glyph.requestIndex];
            uint32_t descriptorIndex = GetTextureResourceSrvDescriptorIndex(glyph.atlasTexture);
            if (descriptorIndex >= GFX::DESCRIPTOR::kUserSrvCount) {
                ++stats.missingAtlasTextureCount;
                ++stats.droppedGlyphCount;
                continue;
            }

            const float scaleX = (std::max)(std::abs(instance.worldScale.x), 0.001f);
            const float scaleY = (std::max)(std::abs(instance.worldScale.y), 0.001f);
            const float centerX = glyph.x + glyph.width * 0.5f - instance.layoutSize.x * instance.pivot.x;
            const float centerY =
                instance.layoutSize.y - (glyph.y + glyph.height * 0.5f) - instance.layoutSize.y * instance.pivot.y;

            GpuWorldTextGlyphInstance gpu{};
            gpu.position = {instance.worldPosition.x, instance.worldPosition.y, instance.worldPosition.z, 1.0f};
            gpu.orientation = {instance.worldRotation.x, instance.worldRotation.y, instance.worldRotation.z,
                               instance.worldRotation.w};
            gpu.localRect = {centerX * scaleX, centerY * scaleY, glyph.width * scaleX, glyph.height * scaleY};
            gpu.uvRect = {glyph.u0, glyph.v0, glyph.u1, glyph.v1};
            gpu.fillColorOpacity = {instance.color.x, instance.color.y, instance.color.z,
                                    std::clamp(instance.opacity * glyph.revealAlpha, 0.0f, 1.0f)};
            gpu.outlineColorWidth = {instance.outlineColor.x, instance.outlineColor.y, instance.outlineColor.z,
                                     ResolveOutlineFieldWidth(instance.outlineWidth)};
            gpu.textureDescriptorIndex = descriptorIndex;
            gpu.facingMode = static_cast<uint32_t>(instance.facing);
            prepared[glyph.requestIndex].glyphs.push_back(gpu);
        }

        prepared.erase(std::remove_if(prepared.begin(), prepared.end(),
                                      [](const PreparedText& text) { return text.glyphs.empty(); }),
                       prepared.end());
        std::stable_sort(prepared.begin(), prepared.end(), [](const PreparedText& lhs, const PreparedText& rhs) {
            if (lhs.sortPriority != rhs.sortPriority) {
                return lhs.sortPriority < rhs.sortPriority;
            }
            if (lhs.distanceSquared != rhs.distanceSquared) {
                return lhs.distanceSquared > rhs.distanceSquared;
            }
            return lhs.stableOrder < rhs.stableOrder;
        });

        activeFrameSlot = context.frameIndex % GFX::kFrameResourceCount;
        GpuWorldTextGlyphInstance* destination = mappedGlyphs[activeFrameSlot];
        if (destination == nullptr)
            return false;
        uint32_t baseIndex = 0u;
        batches.reserve(prepared.size());
        for (const PreparedText& text : prepared) {
            const uint32_t count = static_cast<uint32_t>(text.glyphs.size());
            std::memcpy(destination + baseIndex, text.glyphs.data(),
                        static_cast<size_t>(count) * sizeof(GpuWorldTextGlyphInstance));
            batches.push_back({baseIndex, count});
            baseIndex += count;
        }
        stats.submittedTextCount = static_cast<uint32_t>(batches.size());
        stats.submittedGlyphCount = baseIndex;
        stats.droppedTextCount = stats.requestedTextCount - stats.submittedTextCount;
        return true;
    }

    bool ExecuteForwardTransparent(const GFX::Context& newContext, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants) {
        context = newContext;
        stats.dispatchCount = 0u;
        if (batches.empty())
            return true;
        if (!initialized || context.cmdList == nullptr || cameraConstants == 0u ||
            activeFrameSlot >= uploadBuffers.size() || !uploadBuffers[activeFrameSlot] || !pipeline) {
            return false;
        }
        ID3D12DescriptorHeap* heap = GetTextureResourceSrvHeap();
        const D3D12_GPU_DESCRIPTOR_HANDLE texturePool = GetMaterialTexturePoolSrvGpuHandle(context);
        if (heap == nullptr || texturePool.ptr == 0u)
            return false;
        ComPtr<ID3D12GraphicsCommandList6> commandList6{};
        if (!HIKARI_DX_CHECK(context.cmdList->QueryInterface(IID_PPV_ARGS(commandList6.GetAddressOf())),
                             "WorldTextPass::QueryCommandList6")) {
            return false;
        }

        context.cmdList->SetDescriptorHeaps(1u, &heap);
        context.cmdList->SetGraphicsRootSignature(rootSignature.Get());
        context.cmdList->SetPipelineState(pipeline.Get());
        context.cmdList->SetGraphicsRootConstantBufferView(0u, cameraConstants);
        context.cmdList->SetGraphicsRootShaderResourceView(1u, uploadBuffers[activeFrameSlot]->GetGPUVirtualAddress());
        context.cmdList->SetGraphicsRootDescriptorTable(2u, texturePool);
        for (const DrawBatch& batch : batches) {
            const DrawConstants constants{batch.baseIndex, batch.count, RENDER3D::COLOR::GetActiveViewExposure().z, 0u};
            context.cmdList->SetGraphicsRoot32BitConstants(3u, 4u, &constants, 0u);
            commandList6->DispatchMesh(batch.count, 1u, 1u);
            ++stats.dispatchCount;
        }
        return true;
    }
};

WorldTextPass::WorldTextPass() : impl_(std::make_unique<Impl>()) {}

WorldTextPass::~WorldTextPass() { Shutdown(); }

bool WorldTextPass::Initialize(const GFX::Context& context, uint32_t glyphCapacity) {
    return impl_->Initialize(context, glyphCapacity);
}

void WorldTextPass::Shutdown() noexcept {
    if (impl_)
        impl_->Shutdown();
}

bool WorldTextPass::PrepareFrame(const GFX::Context& context, const WorldTextFrameSource* source,
                                 const MATH::Vec3& cameraPosition) {
    if (!impl_->initialized && !impl_->initializationAttempted && context.device && context.srvHeap && source &&
        !source->instances.empty()) {
        (void)Initialize(context);
        impl_->initializationAttempted = true;
    }
    return impl_->PrepareFrame(context, source, cameraPosition);
}

bool WorldTextPass::ExecuteForwardTransparent(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants) {
    return impl_->ExecuteForwardTransparent(context, cameraConstants);
}

bool WorldTextPass::HasTransparentWork() const noexcept { return impl_->stats.submittedGlyphCount != 0u; }

bool WorldTextPass::HasFrameWork() const noexcept { return HasTransparentWork(); }

const WorldTextPassStats& WorldTextPass::GetStats() const noexcept { return impl_->stats; }

} // namespace HIKARI::RENDER3D::WORLD_TEXT
