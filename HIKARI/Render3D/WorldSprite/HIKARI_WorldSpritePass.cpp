#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePipelines.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteMaterials.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_SceneTextureAssetRequest.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::WORLD_SPRITES {
namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kGpuAlphaOpaque = 0u;
constexpr uint32_t kGpuAlphaMask = 1u;
constexpr uint32_t kGpuAlphaBlend = 2u;
constexpr uint32_t kGpuAlphaHybridCore = 3u;
constexpr uint32_t kGpuAlphaHybridFringe = 4u;

enum class ResolvedAlphaMode : uint8_t {
    Opaque,
    Mask,
    Blend,
    Hybrid,
};

// CPU 側の整列は自然境界のままにする。PreparedItem を
// std::stable_sort するため、拡張境界にすると MSVC の内部
// aligned_storage 契約に抵触する。GPU 側は 144-byte stride で一致する。
struct GpuWorldSpriteInstance {
    MATH::Vec4 position{};
    MATH::Vec4 orientation{};
    MATH::Vec4 sizePivot{};
    MATH::Vec4 tintOpacity{};
    MATH::Vec4 uvRect{0, 0, 1, 1};
    MATH::Vec4 surface{};
    uint32_t textureDescriptorIndex = 0u;
    uint32_t facingMode = 0u;
    uint32_t alphaMode = kGpuAlphaBlend;
    float alphaCutoff = 0.5f;
    uint32_t normalTextureDescriptorIndex = (std::numeric_limits<uint32_t>::max)();
    uint32_t reserved[3]{};
    MATH::Vec4 sourceCanvasRect{};
};

static_assert(sizeof(GpuWorldSpriteInstance) == 144u);

struct DrawConstants {
    uint32_t baseIndex = 0u;
    uint32_t count = 0u;
    uint32_t reserved0 = 0u;
    uint32_t reserved1 = 0u;
    float logicalTimeSeconds = 0.0f;
};

struct PreparedItem {
    GpuWorldSpriteInstance gpu{};
    std::shared_ptr<const WorldSpriteMaterialPipelines> material;
    int32_t sortPriority = 0;
    float viewDepth = 0.0f;
    uint32_t stableOrder = 0u;
};

ResolvedAlphaMode ResolveAlphaMode(WorldSpriteAlphaMode authored, const TextureAssetDescriptor* descriptor,
                                   float opacity) noexcept {
    if (authored == WorldSpriteAlphaMode::Opaque) {
        return opacity < 0.999f ? ResolvedAlphaMode::Blend : ResolvedAlphaMode::Opaque;
    }
    if (authored == WorldSpriteAlphaMode::Mask) {
        return ResolvedAlphaMode::Mask;
    }
    if (authored == WorldSpriteAlphaMode::Blend) {
        return ResolvedAlphaMode::Blend;
    }
    if (opacity < 0.999f || descriptor == nullptr) {
        return ResolvedAlphaMode::Blend;
    }
    switch (descriptor->alphaClass) {
    case TextureAssetAlphaClass::Opaque: return ResolvedAlphaMode::Opaque;
    case TextureAssetAlphaClass::Cutout: return ResolvedAlphaMode::Mask;
    case TextureAssetAlphaClass::Translucent:
        // Auto の半透明画像は、不透明に近い中心だけを先に
        // Depth へ書き、柔らかい縁を透明 Pass で合成する。
        // アンチエイリアス付き PNG 全体が Depth 非書き込みに
        // 落ちて背面モデルが透けることを防ぐ。
        return ResolvedAlphaMode::Hybrid;
    case TextureAssetAlphaClass::Unknown:
    default: return ResolvedAlphaMode::Blend;
    }
}

} // namespace

struct WorldSpritePass::Impl {
    GFX::Context context{};
    ComPtr<ID3D12RootSignature> rootSignature{};
    WorldSpritePipelineSet pipelines{};
    ASSETS::SHADER::ShaderAssetEntryLease geometryShader;
    WorldSpriteMaterials materials;
    struct FrameSlot {
        ComPtr<ID3D12Resource> instances{};
        DX::DynamicUploadBuffer staging{};
        std::vector<GpuWorldSpriteInstance> published{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };
    std::array<FrameSlot, GFX::kFrameResourceCount> frames{};
    std::vector<PreparedItem> opaque, masked, hybridCore, transparent;
    std::vector<GpuWorldSpriteInstance> packed;
    std::vector<std::shared_ptr<const WorldSpriteMaterialPipelines>> packedMaterials;
    std::vector<MATH::Vec4> parameterValues;
    std::vector<MATH::Vec4> shadowBounds;
    std::vector<TextureResourceLoadRequest> requests;
    std::vector<std::string> requestAssetIds;
    std::unordered_map<std::string, TextureResourceHandle> textureByAssetId;
    uint64_t textureWorld = 0, textureRegistry = 0, textureRevision = 0;
    TextureResourceHandle missingTexture{};
    WorldSpritePassStats stats{};
    uint32_t capacity = 0u;
    uint32_t opaqueOffset = 0u;
    uint32_t maskOffset = 0u;
    uint32_t hybridOffset = 0u;
    uint32_t transparentOffset = 0u;
    uint32_t activeFrameSlot = 0u;
    bool initialized = false, meshPipeline = false, preferRaster = false;
    SurfaceLightingBindings lighting{};
    bool initializationAttempted = false;
    const WorldSpriteFrameSource* preparedSource = nullptr;
    uint64_t preparedFrame = ~uint64_t(0), preparedSourceFrame = 0, preparedWorld = 0;
    uint64_t preparedFence = 0;
    MATH::Mat4 preparedView{};

    bool Initialize(const GFX::Context& newContext, uint32_t newCapacity) {
        Shutdown();
        context = newContext;
        capacity = (std::max)((std::min)(newCapacity, (std::numeric_limits<uint32_t>::max)() / 2u), 1u);
        if (context.device == nullptr || !CreateWorldSpritePipelines(context.device, preferRaster, rootSignature,
                                                                     pipelines, meshPipeline, geometryShader)) {
            Shutdown();
            return false;
        }
        for (auto& frame : frames) {
            if (!GFX::D3D12_BUFFER::CreateDefaultBuffer(
                    context.device, UINT64(capacity) * 2u * sizeof(GpuWorldSpriteInstance), frame.instances) ||
                !frame.staging.Init(context.device, 64u * 1024u)) {
                Shutdown();
                return false;
            }
        }
        stats.residentBytes = UINT64(capacity) * 2u * sizeof(GpuWorldSpriteInstance) * frames.size();
        missingTexture = CreateCheckerTextureResource("world_sprite/missing_checker/" +
                                                          std::to_string(reinterpret_cast<uintptr_t>(this)),
                                                      0xff00ffffu, 0x120018ffu, TextureResourceColorSpace::Srgb);
        if (!missingTexture || GetTextureResourceSrvDescriptorIndex(missingTexture) >= GFX::DESCRIPTOR::kUserSrvCount) {
            Shutdown();
            return false;
        }
        initialized = true;
        stats.ready = true;
        stats.depthOfFieldCaptureReady =
            pipelines[static_cast<size_t>(WorldSpritePipeline::DepthOfFieldCapture)] != nullptr;
        return true;
    }

    void Shutdown() noexcept {
        for (auto& frame : frames) {
            frame.staging.Finalize(&context);
            GFX::RetireD3D12ObjectForFrame(frame.instances, context, "WorldSpriteInstances");
            frame.published.clear();
            frame.state = D3D12_RESOURCE_STATE_COMMON;
        }
        opaque.clear();
        masked.clear();
        hybridCore.clear();
        transparent.clear();
        packed.clear();
        packedMaterials.clear();
        parameterValues.clear();
        materials.Shutdown();
        geometryShader = {};
        shadowBounds.clear();
        requests.clear();
        requestAssetIds.clear();
        textureByAssetId.clear();
        textureWorld = textureRegistry = textureRevision = 0;
        for (auto& pipeline : pipelines)
            GFX::RetireD3D12ObjectForFrame(pipeline, context, "WorldSpritePipeline");
        GFX::RetireD3D12ObjectForFrame(rootSignature, context, "WorldSpriteRoot");
        if (missingTexture) {
            (void)ReleaseTextureResource(missingTexture);
            missingTexture = {};
        }
        stats = {};
        capacity = 0u;
        opaqueOffset = 0u;
        maskOffset = 0u;
        hybridOffset = 0u;
        transparentOffset = 0u;
        activeFrameSlot = 0u;
        initialized = false;
        initializationAttempted = false;
        preparedFrame = ~uint64_t(0);
        preparedSource = nullptr;
    }

    bool PrepareFrame(const GFX::Context& newContext, const WorldSpriteFrameSource* source, const Camera3D& camera) {
        const auto view = camera.GetViewProj();
        if (source && preparedSource == source && preparedFrame == newContext.frameIndex &&
            preparedFence == newContext.currentFrameRetireFenceValue && preparedSourceFrame == source->frameIndex &&
            preparedWorld == source->worldIdentity && std::memcmp(&preparedView, &view, sizeof(view)) == 0)
            return true;
        preparedFrame = ~uint64_t(0);
        context = newContext;
        stats.submittedCount = 0u;
        stats.opaqueCount = 0u;
        stats.maskCount = 0u;
        stats.hybridCount = 0u;
        stats.transparentCount = 0u;
        stats.missingTextureCount = 0u;
        stats.droppedCount = 0u;
        stats.uploadedBytes = 0u;
        stats.textureLoadRequests = 0u;
        stats.shadowCasterCount = 0u;
        shadowBounds.clear();
        opaque.clear();
        masked.clear();
        hybridCore.clear();
        transparent.clear();
        packed.clear();
        packedMaterials.clear();
        parameterValues.clear();
        const uint64_t registryId = source && source->assetRegistry ? source->assetRegistry->GetInstanceId() : 0;
        const uint64_t revision = source && source->assetRegistry ? source->assetRegistry->GetRevision() : 0;
        materials.BeginFrame(context, registryId, revision);
        if (source == nullptr || source->instances.empty()) {
            (void)materials.UploadParameters({}, stats.uploadedBytes);
            stats.residentBytes =
                UINT64(capacity) * 2u * sizeof(GpuWorldSpriteInstance) * frames.size() + materials.GetResidentBytes();
            return true;
        }
        if (!initialized || context.device == nullptr) {
            stats.droppedCount = static_cast<uint32_t>(
                (std::min)(source->instances.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
            return true;
        }

        if (context.cmdList == nullptr)
            return false;
        if (source->worldIdentity != textureWorld || registryId != textureRegistry || revision != textureRevision) {
            textureByAssetId.clear();
            textureWorld = source->worldIdentity;
            textureRegistry = registryId;
            textureRevision = revision;
        }
        requests.clear();
        requestAssetIds.clear();
        const uint32_t acceptedCount = static_cast<uint32_t>((std::min)(source->instances.size(), size_t(capacity)));
        std::unordered_set<std::string_view> requestedThisFrame{};
        requestedThisFrame.reserve(static_cast<size_t>(acceptedCount) * 2u);
        const auto queueTexture = [&](const std::string& assetId) {
            if (assetId.empty()) {
                return;
            }
            if (!requestedThisFrame.insert(assetId).second) {
                return;
            }
            if (const auto found = textureByAssetId.find(assetId); found != textureByAssetId.end()) {
                if (found->second && IsTextureResourceValid(found->second)) {
                    return;
                }
                textureByAssetId.erase(found);
            }
            textureByAssetId.try_emplace(assetId);
            std::optional<TextureResourceLoadRequest> request =
                source->assetRegistry != nullptr ? FindSceneTextureAssetRequest(*source->assetRegistry, assetId)
                                                 : std::nullopt;
            if (!request) {
                textureByAssetId.erase(assetId);
                return;
            }
            requests.push_back(std::move(*request));
            requestAssetIds.push_back(assetId);
        };
        for (uint32_t index = 0; index < acceptedCount; ++index) {
            const auto& instance = source->instances[index];
            queueTexture(instance.textureAssetId.value);
            queueTexture(instance.normalTextureAssetId.value);
        }
        if (!requests.empty()) {
            TextureResourceBatchLoadStats loadStats{};
            const auto handles = PreloadTextureResourcesWithColorSpace(requests, &loadStats);
            const size_t count = (std::min)(handles.size(), requestAssetIds.size());
            for (size_t index = 0u; index < count; ++index) {
                if (handles[index]) {
                    textureByAssetId[requestAssetIds[index]] = handles[index];
                }
            }
        }

        stats.textureLoadRequests = static_cast<uint32_t>(requests.size());
        stats.droppedCount = static_cast<uint32_t>(source->instances.size()) - acceptedCount;
        opaque.reserve(acceptedCount);
        masked.reserve(acceptedCount);
        hybridCore.reserve(acceptedCount);
        transparent.reserve(acceptedCount);
        const uint32_t missingDescriptorIndex = GetTextureResourceSrvDescriptorIndex(missingTexture);

        const auto cameraPosition = camera.GetPosition();
        const auto cameraForward = MATH::Normalize(camera.GetTarget() - cameraPosition);
        for (uint32_t index = 0u; index < acceptedCount; ++index) {
            const WorldSpriteInstance& instance = source->instances[index];
            const TextureAssetDescriptor* descriptor =
                source->assetRegistry != nullptr && !instance.textureAssetId.value.empty()
                    ? source->assetRegistry->FindAs<TextureAssetDescriptor>(instance.textureAssetId)
                    : nullptr;
            TextureResourceHandle texture = missingTexture;
            if (const auto found = textureByAssetId.find(instance.textureAssetId.value);
                found != textureByAssetId.end() && IsTextureResourceValid(found->second)) {
                texture = found->second;
            } else {
                ++stats.missingTextureCount;
            }
            uint32_t descriptorIndex = GetTextureResourceSrvDescriptorIndex(texture);
            if (descriptorIndex >= GFX::DESCRIPTOR::kUserSrvCount) {
                descriptorIndex = missingDescriptorIndex;
                ++stats.missingTextureCount;
            }
            uint32_t normalDescriptorIndex = (std::numeric_limits<uint32_t>::max)();
            if (!instance.normalTextureAssetId.value.empty()) {
                if (const auto found = textureByAssetId.find(instance.normalTextureAssetId.value);
                    found != textureByAssetId.end() && IsTextureResourceValid(found->second)) {
                    const uint32_t candidate = GetTextureResourceSrvDescriptorIndex(found->second);
                    if (candidate < GFX::DESCRIPTOR::kUserSrvCount) {
                        normalDescriptorIndex = candidate;
                    }
                }
            }

            float width = instance.size.x * (std::max)(std::abs(instance.worldScale.x), 0.001f);
            float height = instance.size.y * (std::max)(std::abs(instance.worldScale.y), 0.001f);
            if (instance.preserveAspect) {
                uint64_t textureWidth = descriptor != nullptr ? descriptor->width : 0u;
                uint32_t textureHeight = descriptor != nullptr ? descriptor->height : 0u;
                if (const RenderResourceRecord* record = GetTextureResourceRecord(texture)) {
                    if (textureWidth == 0u) {
                        textureWidth = record->desc.width;
                    }
                    if (textureHeight == 0u) {
                        textureHeight = record->desc.height;
                    }
                }
                if (textureWidth != 0u && textureHeight != 0u) {
                    width = height * static_cast<float>(textureWidth) / static_cast<float>(textureHeight) *
                            instance.uvRect.z / instance.uvRect.w;
                }
            }

            PreparedItem item{};
            item.gpu.position = {instance.worldPosition.x, instance.worldPosition.y, instance.worldPosition.z,
                                 float((instance.castShadow ? 1u : 0u) | (instance.receiveShadow ? 2u : 0u) |
                                       (instance.rotated ? 4u : 0u) | (instance.flipX ? 8u : 0u))};
            item.gpu.orientation = {instance.worldRotation.x, instance.worldRotation.y, instance.worldRotation.z,
                                    instance.worldRotation.w};
            item.gpu.sizePivot = {width, height, instance.pivot.x, instance.pivot.y};
            item.gpu.tintOpacity = {instance.tint.x, instance.tint.y, instance.tint.z, instance.opacity};
            item.gpu.uvRect = instance.uvRect;
            item.gpu.sourceCanvasRect = instance.sourceCanvasRect;
            if (instance.spriteShaderArtifact) {
                item.material = materials.Resolve(instance.spriteShaderArtifact, rootSignature.Get(), meshPipeline,
                                                  geometryShader.GetDxil());
                if (item.material) {
                    item.gpu.reserved[0] =
                        static_cast<uint32_t>(parameterValues.size() / ASSETS::SHADER::kShaderParameterSlotCount);
                    parameterValues.insert(parameterValues.end(), instance.spriteMaterialParameters.begin(),
                                           instance.spriteMaterialParameters.end());
                }
            }
            item.gpu.surface = {instance.lit ? 1.0f : 0.0f, instance.roughness, instance.specular,
                                instance.pointSampling ? 1.0f : 0.0f};
            item.gpu.textureDescriptorIndex = descriptorIndex;
            item.gpu.normalTextureDescriptorIndex = normalDescriptorIndex;
            item.gpu.facingMode = static_cast<uint32_t>(instance.facing);
            const ResolvedAlphaMode resolvedAlpha = ResolveAlphaMode(instance.alphaMode, descriptor, instance.opacity);
            if (instance.castShadow && resolvedAlpha != ResolvedAlphaMode::Blend) {
                // Billboard 回転と pivot を含む保守的な球。影視点へも同じ形状を提出する。
                const float dx = width * (std::max)(instance.pivot.x, 1.0f - instance.pivot.x);
                const float dy = height * (std::max)(instance.pivot.y, 1.0f - instance.pivot.y);
                shadowBounds.push_back({instance.worldPosition.x, instance.worldPosition.y, instance.worldPosition.z,
                                        std::sqrt(dx * dx + dy * dy)});
                ++stats.shadowCasterCount;
            }
            item.gpu.alphaCutoff = instance.alphaCutoff;
            item.sortPriority = instance.transparentSortPriority;
            item.stableOrder = instance.stableOrder;
            const MATH::Vec3 delta = instance.worldPosition - cameraPosition;
            item.viewDepth = MATH::Dot(delta, cameraForward);
            if (!std::isfinite(item.viewDepth)) {
                item.viewDepth = 0.0f;
            }
            if (resolvedAlpha == ResolvedAlphaMode::Opaque) {
                item.gpu.alphaMode = kGpuAlphaOpaque;
                opaque.push_back(item);
            } else if (resolvedAlpha == ResolvedAlphaMode::Mask) {
                item.gpu.alphaMode = kGpuAlphaMask;
                masked.push_back(item);
            } else if (resolvedAlpha == ResolvedAlphaMode::Hybrid) {
                item.gpu.alphaMode = kGpuAlphaHybridCore;
                hybridCore.push_back(item);
                item.gpu.alphaMode = kGpuAlphaHybridFringe;
                transparent.push_back(item);
            } else {
                item.gpu.alphaMode = kGpuAlphaBlend;
                transparent.push_back(item);
            }
        }
        std::stable_sort(transparent.begin(), transparent.end(), [](const PreparedItem& lhs, const PreparedItem& rhs) {
            if (lhs.sortPriority != rhs.sortPriority) {
                return lhs.sortPriority < rhs.sortPriority;
            }
            if (lhs.viewDepth != rhs.viewDepth) {
                return lhs.viewDepth > rhs.viewDepth;
            }
            return lhs.stableOrder < rhs.stableOrder;
        });

        activeFrameSlot = context.frameIndex % GFX::kFrameResourceCount;
        auto& frame = frames[activeFrameSlot];
        packed.clear();
        opaqueOffset = 0u;
        maskOffset = static_cast<uint32_t>(opaque.size());
        hybridOffset = maskOffset + static_cast<uint32_t>(masked.size());
        transparentOffset = hybridOffset + static_cast<uint32_t>(hybridCore.size());
        const auto copyItems = [this](const std::vector<PreparedItem>& items) {
            for (const PreparedItem& item : items) {
                packed.push_back(item.gpu);
                packedMaterials.push_back(item.material);
            }
        };
        copyItems(opaque);
        copyItems(masked);
        copyItems(hybridCore);
        copyItems(transparent);
        const size_t bytes = packed.size() * sizeof(GpuWorldSpriteInstance);
        // 同一スロットの内容が変わらなければ転送しない。UPLOAD は一時的なコピー元だけに使う。
        if (frame.published.size() != packed.size() ||
            (bytes && std::memcmp(frame.published.data(), packed.data(), bytes) != 0)) {
            frame.staging.ResetForFrame(context.currentFrameRetireFenceValue);
            const auto upload = frame.staging.Allocate(bytes);
            if (!upload)
                return false;
            std::memcpy(upload.cpuAddress, packed.data(), bytes);
            const auto before = CD3DX12_RESOURCE_BARRIER::Transition(frame.instances.Get(), frame.state,
                                                                     D3D12_RESOURCE_STATE_COPY_DEST);
            context.cmdList->ResourceBarrier(1, &before);
            context.cmdList->CopyBufferRegion(frame.instances.Get(), 0, upload.resource, upload.offset, bytes);
            const auto after = CD3DX12_RESOURCE_BARRIER::Transition(
                frame.instances.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.cmdList->ResourceBarrier(1, &after);
            frame.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            frame.published = packed;
            stats.uploadedBytes = bytes;
        }
        if (!materials.UploadParameters(parameterValues, stats.uploadedBytes))
            return false;
        stats.residentBytes =
            UINT64(capacity) * 2u * sizeof(GpuWorldSpriteInstance) * frames.size() + materials.GetResidentBytes();
        stats.opaqueCount = static_cast<uint32_t>(opaque.size());
        stats.maskCount = static_cast<uint32_t>(masked.size());
        stats.hybridCount = static_cast<uint32_t>(hybridCore.size());
        stats.transparentCount = static_cast<uint32_t>(transparent.size());
        stats.meshShader = meshPipeline;
        stats.submittedCount = stats.opaqueCount + stats.maskCount + stats.transparentCount;
        preparedSource = source;
        preparedFrame = newContext.frameIndex;
        preparedFence = newContext.currentFrameRetireFenceValue;
        preparedSourceFrame = source->frameIndex;
        preparedWorld = source->worldIdentity;
        preparedView = view;
        return true;
    }

    bool ExecuteBatch(const GFX::Context& newContext, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants, uint32_t alphaMode,
                      uint32_t offset, uint32_t count, bool preserveDrawOrder = false, uint32_t passKind = 0u,
                      const MATH::Mat4* lightViewProjection = nullptr) {
        context = newContext;
        if (count == 0u)
            return true;
        if (!initialized || context.cmdList == nullptr || cameraConstants == 0u || activeFrameSlot >= frames.size() ||
            !frames[activeFrameSlot].instances || alphaMode >= pipelines.size() || !pipelines[alphaMode]) {
            return false;
        }
        ID3D12DescriptorHeap* heap = GetTextureResourceSrvHeap();
        const D3D12_GPU_DESCRIPTOR_HANDLE texturePool = GetMaterialTexturePoolSrvGpuHandle(context);
        if (heap == nullptr || texturePool.ptr == 0u)
            return false;
        ComPtr<ID3D12GraphicsCommandList6> commandList6{};
        if (meshPipeline && !HIKARI_DX_CHECK(context.cmdList->QueryInterface(IID_PPV_ARGS(commandList6.GetAddressOf())),
                                             "WorldSpritePass::QueryCommandList6")) {
            return false;
        }
        context.cmdList->SetDescriptorHeaps(1u, &heap);
        context.cmdList->SetGraphicsRootSignature(rootSignature.Get());
        context.cmdList->SetPipelineState(pipelines[alphaMode].Get());
        context.cmdList->SetGraphicsRootConstantBufferView(0u, cameraConstants);
        context.cmdList->SetGraphicsRootShaderResourceView(1u,
                                                           frames[activeFrameSlot].instances->GetGPUVirtualAddress());
        context.cmdList->SetGraphicsRootDescriptorTable(2u, texturePool);
        context.cmdList->SetGraphicsRootShaderResourceView(
            14u, materials.GetParameterAddress(frames[activeFrameSlot].instances->GetGPUVirtualAddress()));
        const MATH::Mat4 identity = MATH::Mat4::Identity();
        context.cmdList->SetGraphicsRoot32BitConstants(13u, 16u, lightViewProjection ? lightViewProjection : &identity,
                                                       0u);
        const bool usesSurfaceLighting = passKind == 0u || passKind == 4u;
        const bool lightingAvailable = usesSurfaceLighting && lighting.IsValid();
        if (lightingAvailable) {
            context.cmdList->SetGraphicsRootConstantBufferView(4, lighting.light);
            context.cmdList->SetGraphicsRootConstantBufferView(5, lighting.shadow);
            context.cmdList->SetGraphicsRootConstantBufferView(6, lighting.skyEnvironment);
            context.cmdList->SetGraphicsRootShaderResourceView(7, lighting.localLights);
            context.cmdList->SetGraphicsRootShaderResourceView(8, lighting.localLightHeaders);
            context.cmdList->SetGraphicsRootShaderResourceView(9, lighting.localLightIndices);
            context.cmdList->SetGraphicsRootDescriptorTable(10,
                                                            lighting.shadowMap.ptr ? lighting.shadowMap : texturePool);
            context.cmdList->SetGraphicsRootDescriptorTable(11, lighting.irradiance.ptr ? lighting.irradiance
                                                                                        : texturePool);
            context.cmdList->SetGraphicsRootDescriptorTable(12, lighting.prefiltered.ptr ? lighting.prefiltered
                                                                                         : texturePool);
        }
        const UINT lightingFlag = lightingAvailable ? 1u : 0u;
        if (!meshPipeline)
            context.cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const auto draw = [&](UINT instances) {
            if (meshPipeline)
                commandList6->DispatchMesh(instances, 1, 1);
            else
                context.cmdList->DrawInstanced(6, instances, 0, 0);
        };
        const float logicalTime = TIME::GetInterpolatedSimulationTimeSeconds();
        const auto pipelineAt = [&](uint32_t index) -> ID3D12PipelineState* {
            if (usesSurfaceLighting && packedMaterials[index])
                return packedMaterials[index]->Get(static_cast<WorldSpritePipeline>(alphaMode));
            return pipelines[alphaMode].Get();
        };
        // 透明順序を保ったまま、同じ PSO の連続する不透明 instance だけをまとめる。
        for (uint32_t index = 0u; index < count;) {
            ID3D12PipelineState* pipeline = pipelineAt(offset + index);
            uint32_t run = 1u;
            if (!preserveDrawOrder) {
                while (index + run < count && pipelineAt(offset + index + run) == pipeline)
                    ++run;
            }
            context.cmdList->SetPipelineState(pipeline);
            const DrawConstants constants{offset + index, run, lightingFlag, passKind, logicalTime};
            context.cmdList->SetGraphicsRoot32BitConstants(3u, 5u, &constants, 0u);
            draw(run);
            index += run;
        }
        return true;
    }
};

WorldSpritePass::WorldSpritePass() : impl_(std::make_unique<Impl>()) {}

WorldSpritePass::~WorldSpritePass() { Shutdown(); }

bool WorldSpritePass::Initialize(const GFX::Context& context, uint32_t instanceCapacity, bool preferRaster) {
    impl_->preferRaster = preferRaster;
    return impl_->Initialize(context, instanceCapacity);
}

void WorldSpritePass::Shutdown() noexcept {
    if (impl_)
        impl_->Shutdown();
}

bool WorldSpritePass::PrepareFrame(const GFX::Context& context, const WorldSpriteFrameSource* source,
                                   const Camera3D& camera) {
    if (impl_->context.device && impl_->context.device != context.device)
        impl_->Shutdown();
    // 入力のない view に PSO/Upload/SRV を確保しない。失敗の再試行は
    // Shutdown または明示 Initialize を所有者が行う時だけにする。
    if (!impl_->initialized && !impl_->initializationAttempted && context.device && context.srvHeap && source &&
        !source->instances.empty()) {
        (void)Initialize(context);
        impl_->initializationAttempted = true;
    }
    return impl_->PrepareFrame(context, source, camera);
}

bool WorldSpritePass::ExecuteForwardOpaque(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                                           const SurfaceLightingBindings& lighting) {
    impl_->lighting = lighting;
    return impl_->ExecuteBatch(context, cameraConstants, kGpuAlphaOpaque, impl_->opaqueOffset,
                               impl_->stats.opaqueCount) &&
           impl_->ExecuteBatch(context, cameraConstants, kGpuAlphaMask, impl_->maskOffset, impl_->stats.maskCount) &&
           impl_->ExecuteBatch(context, cameraConstants, kGpuAlphaHybridCore, impl_->hybridOffset,
                               impl_->stats.hybridCount);
}

bool WorldSpritePass::ExecuteForwardTransparent(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                                                const SurfaceLightingBindings& lighting) {
    impl_->lighting = lighting;
    return impl_->ExecuteBatch(context, cameraConstants, kGpuAlphaBlend, impl_->transparentOffset,
                               impl_->stats.transparentCount, true);
}

bool WorldSpritePass::ExecuteForwardTransparentDepthOfField(const GFX::Context& context,
                                                            D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
                                                            const SurfaceLightingBindings& lighting,
                                                            const DOF::WorldSpriteLayerCapture& capture) {
    if (capture.frameSlot != context.frameIndex || capture.width == 0u || capture.height == 0u ||
        !impl_->pipelines[static_cast<size_t>(WorldSpritePipeline::DepthOfFieldCapture)]) {
        return false;
    }
    impl_->lighting = lighting;
    MATH::Mat4 passData = MATH::Mat4::Identity();
    const CAMERA::DepthOfFieldSettings& settings = capture.settings;
    passData.m[0][0] = settings.focusDistance;
    passData.m[1][0] = settings.focusRange;
    passData.m[2][0] = settings.nearTransition;
    passData.m[3][0] = settings.farTransition;
    const float scale = static_cast<float>(capture.height) / 1080.0f;
    passData.m[0][1] = settings.nearBlurRadius1080pPixels * scale;
    passData.m[1][1] = settings.farBlurRadius1080pPixels * scale;
    passData.m[2][1] = static_cast<float>(capture.width);
    passData.m[3][1] = static_cast<float>(capture.height);
    return impl_->ExecuteBatch(context, cameraConstants,
                               static_cast<uint32_t>(WorldSpritePipeline::DepthOfFieldCapture),
                               impl_->transparentOffset, impl_->stats.transparentCount, true, 4u, &passData);
}

bool WorldSpritePass::ExecuteDepth(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS camera) {
    return !HasOpaqueWork() || impl_->ExecuteBatch(context, camera, static_cast<uint32_t>(WorldSpritePipeline::Depth),
                                                   0u, impl_->transparentOffset, false, 2u);
}

bool WorldSpritePass::ExecuteSurface(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS camera, bool featureMask) {
    return !HasOpaqueWork() ||
           impl_->ExecuteBatch(
               context, camera,
               static_cast<uint32_t>(featureMask ? WorldSpritePipeline::SurfaceFeature : WorldSpritePipeline::Surface),
               0u, impl_->transparentOffset, false, 1u);
}

bool WorldSpritePass::ExecuteShadow(const GFX::Context& context, D3D12_GPU_VIRTUAL_ADDRESS camera,
                                    const MATH::Mat4& lightViewProjection) {
    return !HasShadowWork() || impl_->ExecuteBatch(context, camera, static_cast<uint32_t>(WorldSpritePipeline::Shadow),
                                                   0u, impl_->transparentOffset, false, 3u, &lightViewProjection);
}

bool WorldSpritePass::HasShadowWork() const noexcept { return impl_->stats.shadowCasterCount != 0; }

std::span<const MATH::Vec4> WorldSpritePass::GetShadowCasterBounds() const noexcept { return impl_->shadowBounds; }

bool WorldSpritePass::HasOpaqueWork() const noexcept {
    return impl_->stats.opaqueCount != 0u || impl_->stats.maskCount != 0u || impl_->stats.hybridCount != 0u;
}

bool WorldSpritePass::HasTransparentWork() const noexcept { return impl_->stats.transparentCount != 0u; }

bool WorldSpritePass::HasFrameWork() const noexcept { return impl_->stats.submittedCount != 0u; }

const WorldSpritePassStats& WorldSpritePass::GetStats() const noexcept { return impl_->stats; }

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
