#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3dx12.h>
#include <wrl/client.h>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_RenderGeometryShaderContract.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
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
        // aligned_storage 契約に抵触する。GPU 側は 80-byte stride で一致する。
        struct GpuWorldSpriteInstance {
            MATH::Vec4 position{};
            MATH::Vec4 orientation{};
            MATH::Vec4 sizePivot{};
            MATH::Vec4 tintOpacity{};
            uint32_t textureDescriptorIndex = 0u;
            uint32_t facingMode = 0u;
            uint32_t alphaMode = kGpuAlphaBlend;
            float alphaCutoff = 0.5f;
        };

        static_assert(sizeof(GpuWorldSpriteInstance) == 80u);

        struct DrawConstants {
            uint32_t baseIndex = 0u;
            uint32_t count = 0u;
            uint32_t reserved0 = 0u;
            uint32_t reserved1 = 0u;
        };

        struct PreparedItem {
            GpuWorldSpriteInstance gpu{};
            int32_t sortPriority = 0;
            float distanceSquared = 0.0f;
            uint32_t stableOrder = 0u;
        };

        struct WorldSpritePipelineStateStream {
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

        TextureResourceColorSpace ResolveTextureColorSpace(
            TextureAssetColorSpace colorSpace) noexcept {
            return colorSpace == TextureAssetColorSpace::Linear
                ? TextureResourceColorSpace::Linear
                : TextureResourceColorSpace::Srgb;
        }

        ResolvedAlphaMode ResolveAlphaMode(
            WorldSpriteAlphaMode authored,
            const TextureAssetDescriptor* descriptor,
            float opacity) noexcept {
            if (authored == WorldSpriteAlphaMode::Opaque) {
                return opacity < 0.999f
                    ? ResolvedAlphaMode::Blend
                    : ResolvedAlphaMode::Opaque;
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
            case TextureAssetAlphaClass::Opaque:
                return ResolvedAlphaMode::Opaque;
            case TextureAssetAlphaClass::Cutout:
                return ResolvedAlphaMode::Mask;
            case TextureAssetAlphaClass::Translucent:
                // Auto の半透明画像は、不透明に近い中心だけを先に
                // Depth へ書き、柔らかい縁を透明 Pass で合成する。
                // アンチエイリアス付き PNG 全体が Depth 非書き込みに
                // 落ちて背面モデルが透けることを防ぐ。
                return ResolvedAlphaMode::Hybrid;
            case TextureAssetAlphaClass::Unknown:
            default:
                return ResolvedAlphaMode::Blend;
            }
        }

        bool CreateRootSignature(
            ID3D12Device* device,
            ID3D12RootSignature** output) {
            if (device == nullptr || output == nullptr) return false;

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
            parameters[2].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[2].DescriptorTable.NumDescriptorRanges = 1u;
            parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
            parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[3].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
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
            ComPtr<ID3DBlob> signature{};
            ComPtr<ID3DBlob> errors{};
            if (!HIKARI_DX_CHECK(
                    D3D12SerializeRootSignature(
                        &description,
                        D3D_ROOT_SIGNATURE_VERSION_1,
                        signature.GetAddressOf(),
                        errors.GetAddressOf()),
                    "WorldSpritePass::SerializeRootSignature")) {
                return false;
            }
            return HIKARI_DX_CHECK(
                device->CreateRootSignature(
                    0u,
                    signature->GetBufferPointer(),
                    signature->GetBufferSize(),
                    IID_PPV_ARGS(output)),
                "WorldSpritePass::CreateRootSignature");
        }

        bool CreateUploadBuffer(
            ID3D12Device* device,
            uint32_t capacity,
            ComPtr<ID3D12Resource>& outResource,
            GpuWorldSpriteInstance*& outMapped) {
            outMapped = nullptr;
            const UINT64 size = static_cast<UINT64>(capacity) *
                sizeof(GpuWorldSpriteInstance);
            const auto heap =
                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto description = CD3DX12_RESOURCE_DESC::Buffer(size);
            if (!HIKARI_DX_CHECK(
                    device->CreateCommittedResource(
                        &heap,
                        D3D12_HEAP_FLAG_NONE,
                        &description,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(outResource.GetAddressOf())),
                    "WorldSpritePass::CreateUploadBuffer")) {
                return false;
            }
            void* mapped = nullptr;
            const D3D12_RANGE noRead{ 0u, 0u };
            if (!HIKARI_DX_CHECK(
                    outResource->Map(0u, &noRead, &mapped),
                    "WorldSpritePass::MapUploadBuffer")) {
                outResource.Reset();
                return false;
            }
            outMapped = static_cast<GpuWorldSpriteInstance*>(mapped);
            return true;
        }

        bool ResolveShaders(
            ID3D12Device* device,
            ASSETS::SHADER::ShaderAssetEntryLease& outMesh,
            ASSETS::SHADER::ShaderAssetEntryLease& outPixel) {
            const GFX::ShaderDeviceCapabilities capabilities =
                GFX::QueryShaderDeviceCapabilities(device);
            const auto contract =
                ASSETS::SHADER::MakeRenderGeometryShaderContract(
                    ASSETS::SHADER::RenderGeometryRootLayout::
                        WorldSpriteDraw,
                    { capabilities.shaderModel,
                      capabilities.rootSignatureVersion,
                      capabilities.featureMask });
            const auto resolve = [&contract](
                const char* entry,
                ASSETS::SHADER::ShaderStage stage,
                const char* profile,
                ASSETS::SHADER::ShaderAssetEntryLease& outLease) {
                auto resolved = ASSETS::SHADER::ResolveBuiltinShaderEntry(
                    "HIKARI.EngineShader.Render3D_WorldSpriteDraw",
                    { entry, stage, profile },
                    contract);
                std::string diagnostic{};
                const auto* artifact = resolved
                    ? resolved.lease.GetArtifact()
                    : nullptr;
                if (!resolved || artifact == nullptr ||
                    !ASSETS::SHADER::ValidateRenderGeometryShaderArtifact(
                        *artifact, diagnostic) ||
                    resolved.lease.GetDxil().empty()) {
                    if (diagnostic.empty()) diagnostic = resolved.diagnostic;
                    HIKARI_LOG_ERROR(
                        std::string("[WorldSpritePass][Shader] ") +
                        entry + ": " + diagnostic);
                    return false;
                }
                outLease = std::move(resolved.lease);
                return true;
            };
            return resolve(
                       "WorldSpriteMS",
                       ASSETS::SHADER::ShaderStage::Mesh,
                       "ms_6_5",
                       outMesh) &&
                resolve(
                    "WorldSpritePS",
                    ASSETS::SHADER::ShaderStage::Pixel,
                    "ps_6_0",
                    outPixel);
        }
    } // namespace

    struct WorldSpritePass::Impl {
        GFX::Context context{};
        ComPtr<ID3D12RootSignature> rootSignature{};
        std::array<ComPtr<ID3D12PipelineState>, 4u> pipelines{};
        std::array<ComPtr<ID3D12Resource>, GFX::kFrameResourceCount>
            uploadBuffers{};
        std::array<GpuWorldSpriteInstance*, GFX::kFrameResourceCount>
            mappedInstances{};
        TextureResourceHandle missingTexture{};
        WorldSpritePassStats stats{};
        uint32_t capacity = 0u;
        uint32_t opaqueOffset = 0u;
        uint32_t maskOffset = 0u;
        uint32_t hybridOffset = 0u;
        uint32_t transparentOffset = 0u;
        uint32_t activeFrameSlot = 0u;
        bool initialized = false;
        bool initializationAttempted = false;

        bool CreatePipelines(ID3D12Device* device) {
            if (!CreateRootSignature(
                    device, rootSignature.GetAddressOf())) {
                return false;
            }
            ASSETS::SHADER::ShaderAssetEntryLease meshShader{};
            ASSETS::SHADER::ShaderAssetEntryLease pixelShader{};
            if (!ResolveShaders(device, meshShader, pixelShader)) {
                return false;
            }
            ComPtr<ID3D12Device2> device2{};
            if (!HIKARI_DX_CHECK(
                    device->QueryInterface(
                        IID_PPV_ARGS(device2.GetAddressOf())),
                    "WorldSpritePass::QueryDevice2")) {
                return false;
            }
            const std::span<const uint8_t> meshDxil =
                meshShader.GetDxil();
            const std::span<const uint8_t> pixelDxil =
                pixelShader.GetDxil();
            for (uint32_t alphaMode = 0u;
                 alphaMode < pipelines.size();
                 ++alphaMode) {
                CD3DX12_BLEND_DESC blend(D3D12_DEFAULT);
                if (alphaMode == kGpuAlphaBlend) {
                    auto& target = blend.RenderTarget[0];
                    target.BlendEnable = TRUE;
                    target.SrcBlend = D3D12_BLEND_ONE;
                    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    target.BlendOp = D3D12_BLEND_OP_ADD;
                    target.SrcBlendAlpha = D3D12_BLEND_ONE;
                    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                }
                CD3DX12_DEPTH_STENCIL_DESC depth(D3D12_DEFAULT);
                depth.DepthEnable = TRUE;
                depth.DepthWriteMask = alphaMode == kGpuAlphaBlend
                    ? D3D12_DEPTH_WRITE_MASK_ZERO
                    : D3D12_DEPTH_WRITE_MASK_ALL;
                depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
                rasterizer.CullMode = D3D12_CULL_MODE_NONE;
                D3D12_RT_FORMAT_ARRAY formats{};
                formats.NumRenderTargets = 1u;
                formats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

                WorldSpritePipelineStateStream stream{};
                stream.rootSignature = rootSignature.Get();
                stream.meshShader = CD3DX12_SHADER_BYTECODE(
                    meshDxil.data(), meshDxil.size());
                stream.pixelShader = CD3DX12_SHADER_BYTECODE(
                    pixelDxil.data(), pixelDxil.size());
                stream.blend = blend;
                stream.depthStencil = depth;
                stream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
                stream.rasterizer = rasterizer;
                stream.rtvFormats = formats;
                stream.sampleDescription = DXGI_SAMPLE_DESC{ 1u, 0u };
                stream.sampleMask = (std::numeric_limits<UINT>::max)();
                D3D12_PIPELINE_STATE_STREAM_DESC description{};
                description.SizeInBytes = sizeof(stream);
                description.pPipelineStateSubobjectStream = &stream;
                if (!HIKARI_DX_CHECK(
                        device2->CreatePipelineState(
                            &description,
                            IID_PPV_ARGS(
                                pipelines[alphaMode].GetAddressOf())),
                        "WorldSpritePass::CreatePipeline")) {
                    return false;
                }
            }
            return true;
        }

        bool Initialize(
            const GFX::Context& newContext,
            uint32_t newCapacity) {
            Shutdown();
            context = newContext;
            capacity = (std::max)(
                (std::min)(
                    newCapacity,
                    (std::numeric_limits<uint32_t>::max)() / 2u),
                1u);
            if (context.device == nullptr ||
                !CreatePipelines(context.device)) {
                Shutdown();
                return false;
            }
            for (uint32_t index = 0u;
                 index < GFX::kFrameResourceCount;
                 ++index) {
                if (!CreateUploadBuffer(
                        context.device,
                        capacity * 2u,
                        uploadBuffers[index],
                        mappedInstances[index])) {
                    Shutdown();
                    return false;
                }
            }
            missingTexture = CreateCheckerTextureResource(
                "world_sprite/missing_checker/" + std::to_string(
                    reinterpret_cast<uintptr_t>(this)),
                0xff00ffffu,
                0x120018ffu,
                TextureResourceColorSpace::Srgb);
            if (!missingTexture ||
                GetTextureResourceSrvDescriptorIndex(missingTexture) >=
                    GFX::DESCRIPTOR::kUserSrvCount) {
                Shutdown();
                return false;
            }
            initialized = true;
            stats.ready = true;
            return true;
        }

        void Shutdown() noexcept {
            for (uint32_t index = 0u;
                 index < GFX::kFrameResourceCount;
                 ++index) {
                if (uploadBuffers[index] && mappedInstances[index]) {
                    uploadBuffers[index]->Unmap(0u, nullptr);
                }
                mappedInstances[index] = nullptr;
                uploadBuffers[index].Reset();
            }
            for (auto& pipeline : pipelines) pipeline.Reset();
            rootSignature.Reset();
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
        }

        bool PrepareFrame(
            const GFX::Context& newContext,
            const WorldSpriteFrameSource* source,
            const MATH::Vec3& cameraPosition) {
            context = newContext;
            stats.submittedCount = 0u;
            stats.opaqueCount = 0u;
            stats.maskCount = 0u;
            stats.hybridCount = 0u;
            stats.transparentCount = 0u;
            stats.missingTextureCount = 0u;
            stats.droppedCount = 0u;
            if (source == nullptr || source->instances.empty()) return true;
            if (!initialized || context.device == nullptr) {
                stats.droppedCount = static_cast<uint32_t>((std::min)(
                    source->instances.size(),
                    static_cast<size_t>(
                        (std::numeric_limits<uint32_t>::max)())));
                return true;
            }

            std::vector<TextureResourceLoadRequest> requests{};
            std::vector<std::string> requestGuids{};
            std::unordered_map<std::string, TextureResourceHandle>
                textureByGuid{};
            for (const WorldSpriteInstance& instance : source->instances) {
                const std::string& guid = instance.textureAssetGuid.value;
                if (guid.empty() || textureByGuid.contains(guid) ||
                    std::find(requestGuids.begin(), requestGuids.end(), guid) !=
                        requestGuids.end()) {
                    continue;
                }
                const auto* descriptor = source->assetRegistry != nullptr
                    ? source->assetRegistry->FindAs<TextureAssetDescriptor>(
                        AssetId{ guid })
                    : nullptr;
                if (descriptor == nullptr ||
                    descriptor->dimension !=
                        TextureAssetDimension::Texture2D ||
                    descriptor->sourcePath.empty()) {
                    continue;
                }
                requests.push_back({
                    "runtime_world_sprite/" + guid,
                    descriptor->sourcePath,
                    ResolveTextureColorSpace(descriptor->colorSpace)
                });
                requestGuids.push_back(guid);
            }
            if (!requests.empty()) {
                TextureResourceBatchLoadStats loadStats{};
                const auto handles = PreloadTextureResourcesWithColorSpace(
                    requests, &loadStats);
                const size_t count = (std::min)(
                    handles.size(), requestGuids.size());
                for (size_t index = 0u; index < count; ++index) {
                    if (handles[index]) {
                        textureByGuid.emplace(
                            requestGuids[index], handles[index]);
                    }
                }
            }

            std::vector<PreparedItem> opaque{};
            std::vector<PreparedItem> masked{};
            std::vector<PreparedItem> hybridCore{};
            std::vector<PreparedItem> transparent{};
            const uint32_t acceptedCount = (std::min)(
                static_cast<uint32_t>(source->instances.size()),
                capacity);
            stats.droppedCount = static_cast<uint32_t>(
                source->instances.size()) - acceptedCount;
            opaque.reserve(acceptedCount);
            masked.reserve(acceptedCount);
            hybridCore.reserve(acceptedCount);
            transparent.reserve(acceptedCount);
            const uint32_t missingDescriptorIndex =
                GetTextureResourceSrvDescriptorIndex(missingTexture);

            for (uint32_t index = 0u; index < acceptedCount; ++index) {
                const WorldSpriteInstance& instance =
                    source->instances[index];
                const TextureAssetDescriptor* descriptor =
                    source->assetRegistry != nullptr &&
                        instance.textureAssetGuid.IsValid()
                    ? source->assetRegistry->FindAs<TextureAssetDescriptor>(
                        AssetId{ instance.textureAssetGuid.value })
                    : nullptr;
                TextureResourceHandle texture = missingTexture;
                if (const auto found = textureByGuid.find(
                        instance.textureAssetGuid.value);
                    found != textureByGuid.end()) {
                    texture = found->second;
                } else {
                    ++stats.missingTextureCount;
                }
                uint32_t descriptorIndex =
                    GetTextureResourceSrvDescriptorIndex(texture);
                if (descriptorIndex >= GFX::DESCRIPTOR::kUserSrvCount) {
                    descriptorIndex = missingDescriptorIndex;
                    ++stats.missingTextureCount;
                }

                float width = instance.size.x *
                    (std::max)(std::abs(instance.worldScale.x), 0.001f);
                float height = instance.size.y *
                    (std::max)(std::abs(instance.worldScale.y), 0.001f);
                if (instance.preserveAspect) {
                    uint64_t textureWidth = descriptor != nullptr
                        ? descriptor->width
                        : 0u;
                    uint32_t textureHeight = descriptor != nullptr
                        ? descriptor->height
                        : 0u;
                    if (const RenderResourceRecord* record =
                            GetTextureResourceRecord(texture)) {
                        if (textureWidth == 0u) {
                            textureWidth = record->desc.width;
                        }
                        if (textureHeight == 0u) {
                            textureHeight = record->desc.height;
                        }
                    }
                    if (textureWidth != 0u && textureHeight != 0u) {
                        width = height * static_cast<float>(textureWidth) /
                            static_cast<float>(textureHeight);
                    }
                }

                PreparedItem item{};
                item.gpu.position = {
                    instance.worldPosition.x,
                    instance.worldPosition.y,
                    instance.worldPosition.z,
                    1.0f
                };
                item.gpu.orientation = {
                    instance.worldRotation.x,
                    instance.worldRotation.y,
                    instance.worldRotation.z,
                    instance.worldRotation.w
                };
                item.gpu.sizePivot = {
                    width,
                    height,
                    instance.pivot.x,
                    instance.pivot.y
                };
                item.gpu.tintOpacity = {
                    instance.tint.x,
                    instance.tint.y,
                    instance.tint.z,
                    instance.opacity
                };
                item.gpu.textureDescriptorIndex = descriptorIndex;
                item.gpu.facingMode = static_cast<uint32_t>(
                    instance.facing);
                const ResolvedAlphaMode resolvedAlpha = ResolveAlphaMode(
                    instance.alphaMode, descriptor, instance.opacity);
                item.gpu.alphaCutoff = instance.alphaCutoff;
                item.sortPriority = instance.transparentSortPriority;
                item.stableOrder = instance.stableOrder;
                const MATH::Vec3 delta =
                    instance.worldPosition - cameraPosition;
                item.distanceSquared = delta.x * delta.x +
                    delta.y * delta.y + delta.z * delta.z;
                if (!std::isfinite(item.distanceSquared)) {
                    item.distanceSquared = 0.0f;
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
            std::stable_sort(
                transparent.begin(),
                transparent.end(),
                [](const PreparedItem& lhs, const PreparedItem& rhs) {
                    if (lhs.sortPriority != rhs.sortPriority) {
                        return lhs.sortPriority < rhs.sortPriority;
                    }
                    if (lhs.distanceSquared != rhs.distanceSquared) {
                        return lhs.distanceSquared > rhs.distanceSquared;
                    }
                    return lhs.stableOrder < rhs.stableOrder;
                });

            activeFrameSlot = context.frameIndex % GFX::kFrameResourceCount;
            GpuWorldSpriteInstance* destination =
                mappedInstances[activeFrameSlot];
            if (destination == nullptr) return false;
            opaqueOffset = 0u;
            maskOffset = static_cast<uint32_t>(opaque.size());
            hybridOffset = maskOffset +
                static_cast<uint32_t>(masked.size());
            transparentOffset = hybridOffset +
                static_cast<uint32_t>(hybridCore.size());
            const auto copyItems = [&destination](
                const std::vector<PreparedItem>& items) {
                for (const PreparedItem& item : items) {
                    *destination++ = item.gpu;
                }
            };
            copyItems(opaque);
            copyItems(masked);
            copyItems(hybridCore);
            copyItems(transparent);
            stats.opaqueCount = static_cast<uint32_t>(opaque.size());
            stats.maskCount = static_cast<uint32_t>(masked.size());
            stats.hybridCount = static_cast<uint32_t>(hybridCore.size());
            stats.transparentCount = static_cast<uint32_t>(
                transparent.size());
            stats.submittedCount = stats.opaqueCount + stats.maskCount +
                stats.transparentCount;
            return true;
        }

        bool ExecuteBatch(
            const GFX::Context& newContext,
            D3D12_GPU_VIRTUAL_ADDRESS cameraConstants,
            uint32_t alphaMode,
            uint32_t offset,
            uint32_t count,
            bool preserveDrawOrder = false) {
            context = newContext;
            if (count == 0u) return true;
            if (!initialized || context.cmdList == nullptr ||
                cameraConstants == 0u ||
                activeFrameSlot >= uploadBuffers.size() ||
                !uploadBuffers[activeFrameSlot] ||
                alphaMode >= pipelines.size() ||
                !pipelines[alphaMode]) {
                return false;
            }
            ID3D12DescriptorHeap* heap = GetTextureResourceSrvHeap();
            const D3D12_GPU_DESCRIPTOR_HANDLE texturePool =
                GetMaterialTexturePoolSrvGpuHandle(context);
            if (heap == nullptr || texturePool.ptr == 0u) return false;
            ComPtr<ID3D12GraphicsCommandList6> commandList6{};
            if (!HIKARI_DX_CHECK(
                    context.cmdList->QueryInterface(
                        IID_PPV_ARGS(commandList6.GetAddressOf())),
                    "WorldSpritePass::QueryCommandList6")) {
                return false;
            }
            context.cmdList->SetDescriptorHeaps(1u, &heap);
            context.cmdList->SetGraphicsRootSignature(rootSignature.Get());
            context.cmdList->SetPipelineState(pipelines[alphaMode].Get());
            context.cmdList->SetGraphicsRootConstantBufferView(
                0u, cameraConstants);
            context.cmdList->SetGraphicsRootShaderResourceView(
                1u,
                uploadBuffers[activeFrameSlot]->GetGPUVirtualAddress());
            context.cmdList->SetGraphicsRootDescriptorTable(
                2u, texturePool);
            if (!preserveDrawOrder) {
                const DrawConstants constants{ offset, count, 0u, 0u };
                context.cmdList->SetGraphicsRoot32BitConstants(
                    3u,
                    4u,
                    &constants,
                    0u);
                commandList6->DispatchMesh(count, 1u, 1u);
            } else {
                // Separate dispatches preserve the CPU-authored stable
                // back-to-front order across transparent primitives.
                for (uint32_t index = 0u; index < count; ++index) {
                    const DrawConstants constants{
                        offset + index, 1u, 0u, 0u };
                    context.cmdList->SetGraphicsRoot32BitConstants(
                        3u,
                        4u,
                        &constants,
                        0u);
                    commandList6->DispatchMesh(1u, 1u, 1u);
                }
            }
            return true;
        }
    };

    WorldSpritePass::WorldSpritePass()
        : impl_(std::make_unique<Impl>()) {}

    WorldSpritePass::~WorldSpritePass() {
        Shutdown();
    }

    bool WorldSpritePass::Initialize(
        const GFX::Context& context,
        uint32_t instanceCapacity) {
        return impl_->Initialize(context, instanceCapacity);
    }

    void WorldSpritePass::Shutdown() noexcept {
        if (impl_) impl_->Shutdown();
    }

    bool WorldSpritePass::PrepareFrame(
        const GFX::Context& context,
        const WorldSpriteFrameSource* source,
        const MATH::Vec3& cameraPosition) {
        // 入力のない view に PSO/Upload/SRV を確保しない。失敗の再試行は
        // Shutdown または明示 Initialize を所有者が行う時だけにする。
        if (!impl_->initialized && !impl_->initializationAttempted &&
            context.device && context.srvHeap && source && !source->instances.empty()) {
            (void)Initialize(context);
            impl_->initializationAttempted = true;
        }
        return impl_->PrepareFrame(context, source, cameraPosition);
    }

    bool WorldSpritePass::ExecuteForwardOpaque(
        const GFX::Context& context,
        D3D12_GPU_VIRTUAL_ADDRESS cameraConstants) {
            return impl_->ExecuteBatch(
                   context,
                   cameraConstants,
                   kGpuAlphaOpaque,
                   impl_->opaqueOffset,
                   impl_->stats.opaqueCount) &&
            impl_->ExecuteBatch(
                context,
                cameraConstants,
                kGpuAlphaMask,
                impl_->maskOffset,
                impl_->stats.maskCount) &&
            impl_->ExecuteBatch(
                context,
                cameraConstants,
                kGpuAlphaHybridCore,
                impl_->hybridOffset,
                impl_->stats.hybridCount);
    }

    bool WorldSpritePass::ExecuteForwardTransparent(
        const GFX::Context& context,
        D3D12_GPU_VIRTUAL_ADDRESS cameraConstants) {
        return impl_->ExecuteBatch(
            context,
            cameraConstants,
            kGpuAlphaBlend,
            impl_->transparentOffset,
            impl_->stats.transparentCount,
            true);
    }

    bool WorldSpritePass::HasOpaqueWork() const noexcept {
        return impl_->stats.opaqueCount != 0u ||
            impl_->stats.maskCount != 0u ||
            impl_->stats.hybridCount != 0u;
    }

    bool WorldSpritePass::HasTransparentWork() const noexcept {
        return impl_->stats.transparentCount != 0u;
    }

    bool WorldSpritePass::HasFrameWork() const noexcept {
        return impl_->stats.submittedCount != 0u;
    }

    const WorldSpritePassStats& WorldSpritePass::GetStats() const noexcept {
        return impl_->stats;
    }

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
