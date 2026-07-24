#include "Render2D/HIKARI_DxTexture.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <DirectXTex.h>
#include <d3dx12.h>
#include "../External/WICTextureLoader.h"
#include "Assets/Formats/HIKARI_HtexFormat.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace DXTEX {

        namespace {

            const char* ColorSpaceSuffix(TextureColorSpace colorSpace)
            {
                switch (colorSpace) {
                case TextureColorSpace::Linear:
                    return "|linear";
                case TextureColorSpace::Srgb:
                    return "|srgb";
                case TextureColorSpace::Auto:
                default:
                    return "|auto";
                }
            }

            const char* ColorSpaceName(TextureColorSpace colorSpace)
            {
                switch (colorSpace) {
                case TextureColorSpace::Linear:
                    return "Linear";
                case TextureColorSpace::Srgb:
                    return "SRGB";
                case TextureColorSpace::Auto:
                default:
                    return "Auto";
                }
            }

            void LogTextureLoad(
                const char* kind,
                const std::string& path,
                TextureColorSpace colorSpace,
                DXGI_FORMAT srvFormat,
                int handle)
            {
                std::ostringstream oss;
                oss << "[DxTextureManager][TextureLoad] kind=" << (kind ? kind : "Texture2D")
                    << " path=" << path
                    << " colorSpace=" << ColorSpaceName(colorSpace)
                    << " srvFormat=" << GFX::FormatToString(srvFormat)
                    << " handle=" << handle;
                HIKARI_LOG_INFO(oss.str());
            }

            std::string NormalizeTextureCachePath(const std::string& path)
            {
                std::string normalized = path;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                return TEXT::ToLowerAsciiCopy(normalized);
            }

            std::string MakeTextureCacheKey(
                const std::string& name,
                const std::string& path,
                TextureColorSpace colorSpace)
            {
                // 蜷後§隲也炊蜷阪〒繧ゅ∝ｮ滉ｽ薙ヵ繧｡繧､繝ｫ縺碁＆縺・ｴ蜷医・蛻･ GPU resource 縺ｨ縺励※謇ｱ縺・・
                const bool isCubemap = name.rfind("cube:", 0) == 0;
                const char* dimensionPrefix = isCubemap ? "cube" : "tex";
                const std::string normalizedPath = NormalizeTextureCachePath(path);
                if (!normalizedPath.empty()) {
                    return std::string(dimensionPrefix) +
                        "|path:" +
                        normalizedPath +
                        ColorSpaceSuffix(colorSpace);
                }

                return std::string(dimensionPrefix) +
                    "|name:" +
                    TEXT::ToLowerAsciiCopy(name) +
                    ColorSpaceSuffix(colorSpace);
            }

            bool ContainsAny(const std::string& text, std::initializer_list<const char*> needles)
            {
                for (const char* needle : needles) {
                    if (needle != nullptr && text.find(needle) != std::string::npos) {
                        return true;
                    }
                }
                return false;
            }

            TextureColorSpace ResolveAutoColorSpace(const std::string& name, const std::string& path)
            {
                const std::string key = TEXT::ToLowerAsciiCopy(name + " " + path);

                // Data textures must stay linear. Sampling these through an SRGB SRV would corrupt values.
                if (ContainsAny(key, {
                    "normal", "_n.", "_n_", "nrm",
                    "roughness", "metallic", "metalness", "metal_rough", "metallicroughness",
                    "occlusion", "ambientocclusion", "ao.", "_ao", "orm", "arm",
                    "height", "displacement", "mask", "opacity", "alpha", "linear"
                })) {
                    return TextureColorSpace::Linear;
                }

                // Most artist-authored color textures, UI textures, albedo/baseColor and emissive maps are SRGB.
                return TextureColorSpace::Srgb;
            }

            bool IsTgaPath(const std::string& path)
            {
                const std::string lower = TEXT::ToLowerAsciiCopy(path);
                return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tga";
            }

            TextureColorSpace ResolveTextureLoadColorSpace(
                const std::string& name,
                const std::string& path,
                TextureColorSpace colorSpace)
            {
                if (colorSpace != TextureColorSpace::Auto) {
                    return colorSpace;
                }

                return ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) ==
                    CookedAssetFormat::HTEX
                    ? TextureColorSpace::Auto
                    : ResolveAutoColorSpace(name, path);
            }

            TextureColorSpace ToRuntimeColorSpace(TextureAssetColorSpace colorSpace)
            {
                switch (colorSpace) {
                case TextureAssetColorSpace::Linear:
                    return TextureColorSpace::Linear;
                case TextureAssetColorSpace::Srgb:
                    return TextureColorSpace::Srgb;
                case TextureAssetColorSpace::Auto:
                default:
                    return TextureColorSpace::Auto;
                }
            }

            DXGI_FORMAT ToSrgbFormat(DXGI_FORMAT format)
            {
                switch (format) {
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case DXGI_FORMAT_B8G8R8X8_UNORM:
                    return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
                case DXGI_FORMAT_BC1_UNORM:
                    return DXGI_FORMAT_BC1_UNORM_SRGB;
                case DXGI_FORMAT_BC2_UNORM:
                    return DXGI_FORMAT_BC2_UNORM_SRGB;
                case DXGI_FORMAT_BC3_UNORM:
                    return DXGI_FORMAT_BC3_UNORM_SRGB;
                case DXGI_FORMAT_BC7_UNORM:
                    return DXGI_FORMAT_BC7_UNORM_SRGB;
                default:
                    return format;
                }
            }

            DXGI_FORMAT ToLinearFormat(DXGI_FORMAT format)
            {
                switch (format) {
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    return DXGI_FORMAT_R8G8B8A8_UNORM;
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    return DXGI_FORMAT_B8G8R8A8_UNORM;
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                    return DXGI_FORMAT_B8G8R8X8_UNORM;
                case DXGI_FORMAT_BC1_UNORM_SRGB:
                    return DXGI_FORMAT_BC1_UNORM;
                case DXGI_FORMAT_BC2_UNORM_SRGB:
                    return DXGI_FORMAT_BC2_UNORM;
                case DXGI_FORMAT_BC3_UNORM_SRGB:
                    return DXGI_FORMAT_BC3_UNORM;
                case DXGI_FORMAT_BC7_UNORM_SRGB:
                    return DXGI_FORMAT_BC7_UNORM;
                default:
                    return format;
                }
            }

            DXGI_FORMAT ResolveSrvFormat(DXGI_FORMAT resourceFormat, TextureColorSpace colorSpace)
            {
                if (colorSpace == TextureColorSpace::Srgb) {
                    return ToSrgbFormat(resourceFormat);
                }
                if (colorSpace == TextureColorSpace::Linear) {
                    return ToLinearFormat(resourceFormat);
                }
                return resourceFormat;
            }

            void RemoveCacheEntriesForHandle(
                std::unordered_map<std::string, int>& cache,
                int handle) {

                for (auto it = cache.begin(); it != cache.end();) {
                    if (it->second == handle) {
                        it = cache.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }

            void WriteRgba(uint32_t rgba, uint8_t* outPixel)
            {
                outPixel[0] = static_cast<uint8_t>((rgba >> 24) & 0xffu);
                outPixel[1] = static_cast<uint8_t>((rgba >> 16) & 0xffu);
                outPixel[2] = static_cast<uint8_t>((rgba >> 8) & 0xffu);
                outPixel[3] = static_cast<uint8_t>(rgba & 0xffu);
            }

            std::string HexColor(uint32_t rgba)
            {
                std::ostringstream oss;
                oss << std::hex << std::setw(8) << std::setfill('0') << rgba;
                return oss.str();
            }
        }

        bool  DxTextureManager::initialized_ = false;
        GFX::Context DxTextureManager::context_{};
        UINT  DxTextureManager::descriptorSize_ = 0;

        ComPtr<ID3D12DescriptorHeap> DxTextureManager::srvHeap_;
        ComPtr<ID3D12CommandAllocator> DxTextureManager::uploadAllocator_;
        ComPtr<ID3D12GraphicsCommandList> DxTextureManager::uploadCmdList_;
        ComPtr<ID3D12Fence> DxTextureManager::uploadFence_;
        HANDLE DxTextureManager::uploadFenceEvent_ = nullptr;
        uint64_t DxTextureManager::uploadFenceValue_ = 1;

        std::vector<ComPtr<ID3D12Resource>>       DxTextureManager::textures_;
        std::vector<TextureDimension>             DxTextureManager::dimensions_;
        std::vector<UINT>                         DxTextureManager::mipCounts_;
        std::vector<DXGI_FORMAT>                  DxTextureManager::formats_;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvCpu_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvGpu_;
        std::vector<bool>                         DxTextureManager::pendingRelease_;
        std::unordered_map<std::string, int>      DxTextureManager::nameToHandle_;
        GFX::DescriptorAllocator                  DxTextureManager::descriptorAllocator_;

        void DxTextureManager::Init(const GFX::Context& ctx, int maxTextures)
        {
            if (initialized_) return;
            context_ = ctx;
            auto* device = context_.device;
            assert(device);

            maxTextures = std::clamp(
                maxTextures,
                1,
                static_cast<int>(GFX::DESCRIPTOR::kUserSrvCount));

            if (context_.srvHeap) {
                srvHeap_ = context_.srvHeap;
            } else {
                D3D12_DESCRIPTOR_HEAP_DESC desc{};
                desc.NumDescriptors = maxTextures;
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
                if (FAILED(hr)) {
                    OutputDebugStringA("DxTextureManager::Init - CreateDescriptorHeap failed.\n");
                    return;
                }
            }

            descriptorSize_ =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            HRESULT hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&uploadAllocator_));
            assert(SUCCEEDED(hr));

            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                uploadAllocator_.Get(),
                nullptr,
                IID_PPV_ARGS(&uploadCmdList_));
            assert(SUCCEEDED(hr));
            uploadCmdList_->Close();

            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence_));
            assert(SUCCEEDED(hr));

            uploadFenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            assert(uploadFenceEvent_ != nullptr);
            uploadFenceValue_ = 1;

            srvCpu_.resize(maxTextures);
            srvGpu_.resize(maxTextures);
            textures_.resize(maxTextures);
            dimensions_.resize(maxTextures, TextureDimension::Texture2D);
            mipCounts_.resize(maxTextures, 0);
            formats_.resize(maxTextures, DXGI_FORMAT_UNKNOWN);
            pendingRelease_.resize(maxTextures, false);
            descriptorAllocator_.Initialize(
                GFX::DESCRIPTOR::kUserSrvBegin,
                static_cast<UINT>(maxTextures));

            for (int i = 0; i < maxTextures; ++i) {
                const UINT descriptorIndex =
                    GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(i);
                srvCpu_[i] =
                    GFX::DESCRIPTOR::CpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
                srvGpu_[i] =
                    GFX::DESCRIPTOR::GpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
            }

            initialized_ = true;
        }

        void DxTextureManager::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        void DxTextureManager::Finalize()
        {
            if (!initialized_) {
                return;
            }

            textures_.clear();
            dimensions_.clear();
            mipCounts_.clear();
            formats_.clear();
            srvCpu_.clear();
            srvGpu_.clear();
            pendingRelease_.clear();
            if (!context_.srvHeap) {
                srvHeap_.Reset();
            }
            uploadCmdList_.Reset();
            uploadAllocator_.Reset();
            uploadFence_.Reset();
            if (uploadFenceEvent_) {
                CloseHandle(uploadFenceEvent_);
                uploadFenceEvent_ = nullptr;
            }
            uploadFenceValue_ = 1;
            nameToHandle_.clear();
            descriptorAllocator_.Reset();
            DXTEX::CleanupWICResources();
            initialized_ = false;
        }

        void DxTextureManager::EnsureInit()
        {
            if (!initialized_) {
                Init(context_);
            }
        }

        int DxTextureManager::LoadTexture(const std::string& name, const std::string& path)
        {
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Auto);
        }

        int DxTextureManager::LoadTextureWithColorSpace(const std::string& name, const std::string& path, TextureColorSpace colorSpace)
        {
            EnsureInit();
            const TextureColorSpace resolvedColorSpace = ResolveTextureLoadColorSpace(name, path, colorSpace);
            const std::string cacheKey = MakeTextureCacheKey(name, path, resolvedColorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            int handle = CreateTextureFromFile(path, resolvedColorSpace);
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::FindLoadedTextureWithColorSpace(
            const std::string& name,
            const std::string& path,
            TextureColorSpace colorSpace)
        {
            EnsureInit();
            const TextureColorSpace resolvedColorSpace =
                ResolveTextureLoadColorSpace(name, path, colorSpace);
            const std::string cacheKey =
                MakeTextureCacheKey(name, path, resolvedColorSpace);
            const auto found = nameToHandle_.find(cacheKey);
            if (found == nameToHandle_.end() ||
                !IsTextureHandleValid(found->second)) {
                return -1;
            }
            return found->second;
        }

        std::vector<TextureLoadResult> DxTextureManager::LoadTexturesWithColorSpaceBatch(
            const std::vector<TextureLoadRequest>& requests,
            TextureBatchLoadStats* outStats)
        {
            EnsureInit();

            TextureBatchLoadStats stats{};
            stats.requested = static_cast<uint32_t>(requests.size());

            std::vector<TextureLoadResult> results(requests.size());
            struct PendingHtexLoad {
                TextureLoadRequest request{};
                std::string cacheKey{};
                TextureColorSpace requestedColorSpace = TextureColorSpace::Auto;
                TextureColorSpace effectiveColorSpace = TextureColorSpace::Auto;
                HtexTexture htex{};
                std::vector<D3D12_SUBRESOURCE_DATA> subresources{};
                Microsoft::WRL::ComPtr<ID3D12Resource> textureResource{};
                Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource{};
                DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
                UINT64 uploadBytes = 0;
                std::vector<size_t> resultIndices{};
                bool readyForUpload = false;
            };

            std::vector<PendingHtexLoad> pendingHtexLoads{};
            std::unordered_map<std::string, size_t> pendingIndexByCacheKey{};

            auto failResults = [&](const std::vector<size_t>& indices) {
                stats.failed += static_cast<uint32_t>(indices.size());
                for (const size_t index : indices) {
                    if (index < results.size()) {
                        results[index].handle = -1;
                        results[index].loaded = false;
                    }
                }
            };

            for (size_t i = 0; i < requests.size(); ++i) {
                const TextureLoadRequest& request = requests[i];
                TextureLoadResult& result = results[i];
                result.name = request.name;
                result.path = request.path;
                result.colorSpace = ResolveTextureLoadColorSpace(
                    request.name,
                    request.path,
                    request.colorSpace);

                const std::string cacheKey = MakeTextureCacheKey(
                    request.name,
                    request.path,
                    result.colorSpace);

                const auto cached = nameToHandle_.find(cacheKey);
                if (cached != nameToHandle_.end()) {
                    result.handle = cached->second;
                    result.cacheHit = true;
                    result.loaded = true;
                    ++stats.cacheHits;
                    continue;
                }

                if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(
                    request.path) != CookedAssetFormat::HTEX) {
                    result.handle = LoadTextureWithColorSpace(
                        request.name,
                        request.path,
                        request.colorSpace);
                    result.loaded = result.handle >= 0;
                    if (result.loaded) {
                        ++stats.fallbackLoads;
                    } else {
                        ++stats.failed;
                    }
                    continue;
                }

                const auto pendingFound = pendingIndexByCacheKey.find(cacheKey);
                if (pendingFound != pendingIndexByCacheKey.end()) {
                    pendingHtexLoads[pendingFound->second].resultIndices.push_back(i);
                    continue;
                }

                PendingHtexLoad load{};
                load.request = request;
                load.cacheKey = cacheKey;
                load.requestedColorSpace = result.colorSpace;
                load.resultIndices.push_back(i);
                pendingIndexByCacheKey.emplace(cacheKey, pendingHtexLoads.size());
                pendingHtexLoads.push_back(std::move(load));
            }

            if (pendingHtexLoads.empty()) {
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] invalid D3D12 context.");
                for (const PendingHtexLoad& load : pendingHtexLoads) {
                    failResults(load.resultIndices);
                }
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            for (PendingHtexLoad& load : pendingHtexLoads) {
                std::string htexMessage{};
                if (!ReadHtexFile(load.request.path, load.htex, htexMessage)) {
                    HIKARI_LOG_ERROR(htexMessage);
                    failResults(load.resultIndices);
                    continue;
                }

                if (load.htex.width == 0 ||
                    load.htex.height == 0 ||
                    load.htex.arraySize == 0 ||
                    load.htex.subresources.empty() ||
                    load.htex.format == DXGI_FORMAT_UNKNOWN ||
                    load.htex.arraySize > 0xffffu ||
                    load.htex.mipLevels > 0xffffu) {
                    HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] invalid texture metadata: " + load.request.path);
                    failResults(load.resultIndices);
                    continue;
                }

                D3D12_RESOURCE_DESC textureDesc{};
                textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                textureDesc.Alignment = 0;
                textureDesc.Width = load.htex.width;
                textureDesc.Height = load.htex.height;
                textureDesc.DepthOrArraySize = static_cast<UINT16>(load.htex.arraySize);
                textureDesc.MipLevels = static_cast<UINT16>(load.htex.mipLevels);
                textureDesc.Format = load.htex.format;
                textureDesc.SampleDesc.Count = 1;
                textureDesc.SampleDesc.Quality = 0;
                textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                auto textureHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                HRESULT hr = device->CreateCommittedResource(
                    &textureHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &textureDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(load.textureResource.GetAddressOf()));
                if (FAILED(hr) || !load.textureResource) {
                    std::ostringstream oss;
                    oss << "[DxTextureManager][HTEXBatch][ERROR] CreateCommittedResource failed. path="
                        << load.request.path
                        << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    HIKARI_LOG_ERROR(oss.str());
                    failResults(load.resultIndices);
                    continue;
                }

                load.subresources.reserve(load.htex.subresources.size());
                for (const HtexSubresource& source : load.htex.subresources) {
                    D3D12_SUBRESOURCE_DATA subresource{};
                    subresource.pData = source.data.data();
                    subresource.RowPitch = static_cast<LONG_PTR>(source.rowPitch);
                    subresource.SlicePitch = static_cast<LONG_PTR>(source.slicePitch);
                    load.subresources.push_back(subresource);
                }

                load.uploadBytes = GetRequiredIntermediateSize(
                    load.textureResource.Get(),
                    0,
                    static_cast<UINT>(load.subresources.size()));

                auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(load.uploadBytes);
                hr = device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(load.uploadResource.GetAddressOf()));
                if (FAILED(hr) || !load.uploadResource) {
                    std::ostringstream oss;
                    oss << "[DxTextureManager][HTEXBatch][ERROR] upload buffer creation failed. path="
                        << load.request.path
                        << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    HIKARI_LOG_ERROR(oss.str());
                    failResults(load.resultIndices);
                    continue;
                }

                const TextureColorSpace embeddedColorSpace = ToRuntimeColorSpace(load.htex.colorSpace);
                load.effectiveColorSpace = load.requestedColorSpace == TextureColorSpace::Auto
                    ? embeddedColorSpace
                    : load.requestedColorSpace;
                load.srvFormat = ResolveSrvFormat(load.htex.format, load.effectiveColorSpace);
                load.readyForUpload = true;
                stats.uploadedBytes += load.uploadBytes;
            }

            const bool hasReadyUpload = std::any_of(
                pendingHtexLoads.begin(),
                pendingHtexLoads.end(),
                [](const PendingHtexLoad& load) { return load.readyForUpload; });
            if (!hasReadyUpload) {
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            HRESULT hr = uploadAllocator_->Reset();
            if (FAILED(hr)) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] command allocator reset failed.");
                for (const PendingHtexLoad& load : pendingHtexLoads) {
                    if (load.readyForUpload) {
                        failResults(load.resultIndices);
                    }
                }
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            if (FAILED(hr)) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] command list reset failed.");
                for (const PendingHtexLoad& load : pendingHtexLoads) {
                    if (load.readyForUpload) {
                        failResults(load.resultIndices);
                    }
                }
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            for (PendingHtexLoad& load : pendingHtexLoads) {
                if (!load.readyForUpload) {
                    continue;
                }

                UpdateSubresources(
                    uploadCmdList_.Get(),
                    load.textureResource.Get(),
                    load.uploadResource.Get(),
                    0,
                    0,
                    static_cast<UINT>(load.subresources.size()),
                    load.subresources.data());

                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                    load.textureResource.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                uploadCmdList_->ResourceBarrier(1, &barrier);
            }

            hr = uploadCmdList_->Close();
            if (FAILED(hr)) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] command list close failed.");
                for (const PendingHtexLoad& load : pendingHtexLoads) {
                    if (load.readyForUpload) {
                        failResults(load.resultIndices);
                    }
                }
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }

            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            if (FAILED(hr)) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] queue signal failed.");
                for (const PendingHtexLoad& load : pendingHtexLoads) {
                    if (load.readyForUpload) {
                        failResults(load.resultIndices);
                    }
                }
                if (outStats != nullptr) {
                    *outStats = stats;
                }
                return results;
            }
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                if (FAILED(hr)) {
                    HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] fence wait setup failed.");
                    for (const PendingHtexLoad& load : pendingHtexLoads) {
                        if (load.readyForUpload) {
                            failResults(load.resultIndices);
                        }
                    }
                    if (outStats != nullptr) {
                        *outStats = stats;
                    }
                    return results;
                }
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            for (PendingHtexLoad& load : pendingHtexLoads) {
                if (!load.readyForUpload) {
                    continue;
                }

                const int handle = load.htex.dimension == HtexTextureDimension::TextureCube
                    ? RegisterCubeFromResourceAs(load.textureResource.Get(), load.srvFormat)
                    : RegisterFromResourceAs(load.textureResource.Get(), load.srvFormat);
                if (handle < 0) {
                    failResults(load.resultIndices);
                    continue;
                }

                nameToHandle_[load.cacheKey] = handle;
                ++stats.uploaded;
                for (const size_t resultIndex : load.resultIndices) {
                    if (resultIndex < results.size()) {
                        results[resultIndex].handle = handle;
                        results[resultIndex].loaded = true;
                    }
                }

                LogTextureLoad(
                    load.htex.dimension == HtexTextureDimension::TextureCube ? "HTEX Cube Batch" : "HTEX 2D Batch",
                    load.request.path,
                    load.effectiveColorSpace,
                    load.srvFormat,
                    handle);
            }

            {
                std::ostringstream oss;
                oss << "[DxTextureManager][HTEXBatch] requested=" << stats.requested
                    << " cacheHits=" << stats.cacheHits
                    << " uploaded=" << stats.uploaded
                    << " fallbackLoads=" << stats.fallbackLoads
                    << " failed=" << stats.failed
                    << " uploadBytes=" << stats.uploadedBytes;
                HIKARI_LOG_INFO(oss.str());
            }

            if (outStats != nullptr) {
                *outStats = stats;
            }
            return results;
        }

        int DxTextureManager::LoadTextureSrgb(const std::string& name, const std::string& path)
        {
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Srgb);
        }

        int DxTextureManager::LoadTextureLinear(const std::string& name, const std::string& path)
        {
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Linear);
        }

        int DxTextureManager::LoadCubemap(const std::string& name, const std::string& path, TextureColorSpace colorSpace)
        {
            EnsureInit();
            const std::string cacheKey = MakeTextureCacheKey("cube:" + name, path, colorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            int handle = CreateCubemapFromFile(path, colorSpace);
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::CreateSolidColorTexture(
            const std::string& name,
            uint32_t rgba,
            TextureColorSpace colorSpace)
        {
            EnsureInit();

            const std::string syntheticPath =
                "generated://solid/" + HexColor(rgba);
            const std::string cacheKey = MakeTextureCacheKey(name, syntheticPath, colorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            std::array<uint8_t, 4> pixel{};
            WriteRgba(rgba, pixel.data());
            const int handle = CreateTextureFromRgbaPixels(
                syntheticPath,
                1,
                1,
                pixel.data(),
                colorSpace,
                "GeneratedSolid");
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::CreateSolidColorCubemap(
            const std::string& name,
            uint32_t rgba,
            TextureColorSpace colorSpace)
        {
            EnsureInit();

            const std::string syntheticPath =
                "generated://solid-cube/" + HexColor(rgba);
            const std::string cacheKey = MakeTextureCacheKey("cube:" + name, syntheticPath, colorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            const int handle = CreateCubemapFromRgbaPixels(
                syntheticPath,
                rgba,
                colorSpace,
                "GeneratedSolidCube");
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::CreateCheckerTexture(
            const std::string& name,
            uint32_t colorA,
            uint32_t colorB,
            TextureColorSpace colorSpace)
        {
            EnsureInit();

            const std::string syntheticPath =
                "generated://checker/" + HexColor(colorA) + "/" + HexColor(colorB);
            const std::string cacheKey = MakeTextureCacheKey(name, syntheticPath, colorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            constexpr uint32_t kSize = 8;
            std::vector<uint8_t> pixels(kSize * kSize * 4);
            for (uint32_t y = 0; y < kSize; ++y) {
                for (uint32_t x = 0; x < kSize; ++x) {
                    const bool useA = ((x / 4u) + (y / 4u)) % 2u == 0u;
                    WriteRgba(useA ? colorA : colorB, pixels.data() + ((y * kSize + x) * 4u));
                }
            }

            const int handle = CreateTextureFromRgbaPixels(
                syntheticPath,
                kSize,
                kSize,
                pixels.data(),
                colorSpace,
                "GeneratedChecker");
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        void DxTextureManager::InvalidateTextureCacheByName(const std::string& name)
        {
            const std::string normalizedName = TEXT::ToLowerAsciiCopy(name);
            const std::string texturePrefix = "tex|name:" + normalizedName + "|";
            const std::string cubemapPrefix = "cube|name:" + normalizedName + "|";
            const std::string legacyTexturePrefix = name + "|";
            const std::string legacyCubemapPrefix = "cube:" + name + "|";
            for (auto it = nameToHandle_.begin(); it != nameToHandle_.end();) {
                if (it->first.rfind(texturePrefix, 0) == 0 ||
                    it->first.rfind(cubemapPrefix, 0) == 0 ||
                    it->first.rfind(legacyTexturePrefix, 0) == 0 ||
                    it->first.rfind(legacyCubemapPrefix, 0) == 0) {
                    it = nameToHandle_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void DxTextureManager::InvalidateTextureCacheByPath(const std::string& path)
        {
            const std::string normalizedPath = NormalizeTextureCachePath(path);
            if (normalizedPath.empty()) {
                return;
            }

            const std::string pathNeedle = "|path:" + normalizedPath + "|";
            const std::string legacyPathNeedle = "|" + normalizedPath + "|";
            for (auto it = nameToHandle_.begin(); it != nameToHandle_.end();) {
                if (it->first.find(pathNeedle) != std::string::npos ||
                    it->first.find(legacyPathNeedle) != std::string::npos) {
                    it = nameToHandle_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void DxTextureManager::InvalidateAllTextureCache()
        {
            nameToHandle_.clear();
        }

        int DxTextureManager::CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace)
        {
            if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) ==
                CookedAssetFormat::HTEX) {
                return CreateTextureFromHtexFile(path, colorSpace);
            }
            if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) ==
                CookedAssetFormat::DDS) {
                return CreateDdsTextureFromFile(path, colorSpace);
            }
            if (IsTgaPath(path)) {
                return CreateTgaTextureFromFile(path, colorSpace);
            }

            auto* device = context_.device;
            auto* queue = context_.queue;
            assert(device && queue);

            assert(uploadAllocator_ && uploadCmdList_ && uploadFence_ && uploadFenceEvent_);

            HRESULT hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));

            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;

            hr = HIKARI::DXTEX::CreateWICTextureFromFile(
                device, uploadCmdList_.Get(), wpath, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                uploadCmdList_->Close();
                OutputDebugStringA("DxTextureManager::CreateTextureFromFile - WIC load failed.\n");
                return -1;
            }

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));

            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));

            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            if (FAILED(hr) || !texResource) {
                OutputDebugStringA("DxTextureManager::CreateTextureFromFile - WIC load failed.\n");
                return -1;
            }

            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = texResource;
            dimensions_[handle] = TextureDimension::Texture2D;

            const D3D12_RESOURCE_DESC texDesc = texResource->GetDesc();
            const DXGI_FORMAT resourceFormat = texDesc.Format;
            const DXGI_FORMAT srvFormat = ResolveSrvFormat(resourceFormat, colorSpace);
            mipCounts_[handle] = std::max<UINT>(1u, static_cast<UINT>(texDesc.MipLevels));
            formats_[handle] = srvFormat;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(
                texResource.Get(), &srvDesc, srvCpu_[handle]);

            LogTextureLoad("Texture2D", path, colorSpace, srvFormat, handle);

            return handle;
        }

        int DxTextureManager::CreateDdsTextureFromFile(const std::string& path, TextureColorSpace colorSpace)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][DDS][ERROR] invalid D3D12 context: " + path);
                return -1;
            }

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage image{};
            HRESULT hr = DirectX::LoadFromDDSFile(wpath, DirectX::DDS_FLAGS_NONE, &metadata, image);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][DDS][ERROR] LoadFromDDSFile failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            if (metadata.IsCubemap()) {
                return CreateCubemapFromFile(path, colorSpace);
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            hr = DirectX::CreateTexture(device, metadata, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][DDS][ERROR] CreateTexture failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            std::vector<D3D12_SUBRESOURCE_DATA> subresources;
            DirectX::PrepareUpload(device, image.GetImages(), image.GetImageCount(), metadata, subresources);
            if (subresources.empty()) {
                HIKARI_LOG_ERROR("[DxTextureManager][DDS][ERROR] PrepareUpload returned no subresources: " + path);
                return -1;
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(
                texResource.Get(),
                0,
                static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr) || !uploadResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][DDS][ERROR] Create upload resource failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(
                uploadCmdList_.Get(),
                texResource.Get(),
                uploadResource.Get(),
                0,
                0,
                static_cast<UINT>(subresources.size()),
                subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(texResource->GetDesc().Format, colorSpace);
            const int handle = RegisterFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad("DDS 2D", path, colorSpace, srvFormat, handle);
            }
            return handle;
        }

        int DxTextureManager::CreateTgaTextureFromFile(const std::string& path, TextureColorSpace colorSpace)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][TGA][ERROR] invalid D3D12 context: " + path);
                return -1;
            }

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage image{};
            HRESULT hr = DirectX::LoadFromTGAFile(wpath, DirectX::TGA_FLAGS_NONE, &metadata, image);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][TGA][ERROR] LoadFromTGAFile failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            hr = DirectX::CreateTexture(device, metadata, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][TGA][ERROR] CreateTexture failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            std::vector<D3D12_SUBRESOURCE_DATA> subresources;
            DirectX::PrepareUpload(device, image.GetImages(), image.GetImageCount(), metadata, subresources);
            if (subresources.empty()) {
                HIKARI_LOG_ERROR("[DxTextureManager][TGA][ERROR] PrepareUpload returned no subresources: " + path);
                return -1;
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(
                texResource.Get(),
                0,
                static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr) || !uploadResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][TGA][ERROR] Create upload resource failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(
                uploadCmdList_.Get(),
                texResource.Get(),
                uploadResource.Get(),
                0,
                0,
                static_cast<UINT>(subresources.size()),
                subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(texResource->GetDesc().Format, colorSpace);
            const int handle = RegisterFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad("TGA 2D", path, colorSpace, srvFormat, handle);
            }
            return handle;
        }

        int DxTextureManager::CreateTextureFromHtexFile(const std::string& path, TextureColorSpace colorSpace)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEX][ERROR] invalid D3D12 context.");
                return -1;
            }

            HtexTexture htex{};
            std::string htexMessage{};
            if (!ReadHtexFile(path, htex, htexMessage)) {
                HIKARI_LOG_ERROR(htexMessage);
                return -1;
            }

            if (htex.width == 0 || htex.height == 0 || htex.subresources.empty() ||
                htex.format == DXGI_FORMAT_UNKNOWN || htex.arraySize > 0xffffu ||
                htex.mipLevels > 0xffffu) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEX][ERROR] invalid texture metadata: " + path);
                return -1;
            }

            D3D12_RESOURCE_DESC textureDesc{};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Alignment = 0;
            textureDesc.Width = htex.width;
            textureDesc.Height = htex.height;
            textureDesc.DepthOrArraySize = static_cast<UINT16>(htex.arraySize);
            textureDesc.MipLevels = static_cast<UINT16>(htex.mipLevels);
            textureDesc.Format = htex.format;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            auto textureHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            HRESULT hr = device->CreateCommittedResource(
                &textureHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(texResource.GetAddressOf()));
            if (FAILED(hr) || !texResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEX][ERROR] CreateCommittedResource failed: " + path);
                return -1;
            }

            std::vector<D3D12_SUBRESOURCE_DATA> subresources;
            subresources.reserve(htex.subresources.size());
            for (const HtexSubresource& source : htex.subresources) {
                D3D12_SUBRESOURCE_DATA subresource{};
                subresource.pData = source.data.data();
                subresource.RowPitch = static_cast<LONG_PTR>(source.rowPitch);
                subresource.SlicePitch = static_cast<LONG_PTR>(source.slicePitch);
                subresources.push_back(subresource);
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(
                texResource.Get(),
                0,
                static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr) || !uploadResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][HTEX][ERROR] upload buffer creation failed: " + path);
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(
                uploadCmdList_.Get(),
                texResource.Get(),
                uploadResource.Get(),
                0,
                0,
                static_cast<UINT>(subresources.size()),
                subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const TextureColorSpace embeddedColorSpace = ToRuntimeColorSpace(htex.colorSpace);
            const TextureColorSpace effectiveColorSpace = colorSpace == TextureColorSpace::Auto
                ? embeddedColorSpace
                : colorSpace;
            const DXGI_FORMAT srvFormat = ResolveSrvFormat(htex.format, effectiveColorSpace);

            const int handle = htex.dimension == HtexTextureDimension::TextureCube
                ? RegisterCubeFromResourceAs(texResource.Get(), srvFormat)
                : RegisterFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad(
                    htex.dimension == HtexTextureDimension::TextureCube ? "HTEX Cube" : "HTEX 2D",
                    path,
                    effectiveColorSpace,
                    srvFormat,
                    handle);
            }
            return handle;
        }

        int DxTextureManager::CreateCubemapFromFile(const std::string& path, TextureColorSpace colorSpace)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][Cubemap][ERROR] LoadCubemap received invalid D3D12 context.");
                return -1;
            }

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage image{};
            HRESULT hr = DirectX::LoadFromDDSFile(wpath, DirectX::DDS_FLAGS_NONE, &metadata, image);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] LoadCubemap failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            if (!metadata.IsCubemap()) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Resource is not a cubemap texture. path=" << path
                    << " arraySize=" << metadata.arraySize;
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            hr = DirectX::CreateTexture(device, metadata, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] CreateTexture failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            std::vector<D3D12_SUBRESOURCE_DATA> subresources;
            DirectX::PrepareUpload(device, image.GetImages(), image.GetImageCount(), metadata, subresources);
            if (subresources.empty()) {
                HIKARI_LOG_ERROR("[DxTextureManager][Cubemap][ERROR] PrepareUpload returned no subresources.");
                return -1;
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Create upload resource failed. hr=0x"
                    << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(uploadCmdList_.Get(), texResource.Get(), uploadResource.Get(), 0, 0, static_cast<UINT>(subresources.size()), subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(texResource->GetDesc().Format, colorSpace);
            const int handle = RegisterCubeFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad("TextureCube", path, colorSpace, srvFormat, handle);
            }
            return handle;
        }

        int DxTextureManager::CreateTextureFromRgbaPixels(
            const std::string& debugName,
            uint32_t width,
            uint32_t height,
            const uint8_t* rgbaPixels,
            TextureColorSpace colorSpace,
            const char* logKind)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_ ||
                width == 0 || height == 0 || rgbaPixels == nullptr) {
                HIKARI_LOG_ERROR("[DxTextureManager][Generated][ERROR] invalid generated texture request: " + debugName);
                return -1;
            }

            D3D12_RESOURCE_DESC textureDesc{};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Alignment = 0;
            textureDesc.Width = width;
            textureDesc.Height = height;
            textureDesc.DepthOrArraySize = 1;
            textureDesc.MipLevels = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            auto textureHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            HRESULT hr = device->CreateCommittedResource(
                &textureHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(texResource.GetAddressOf()));
            if (FAILED(hr) || !texResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][Generated][ERROR] CreateCommittedResource failed: " + debugName);
                return -1;
            }

            D3D12_SUBRESOURCE_DATA subresource{};
            subresource.pData = rgbaPixels;
            subresource.RowPitch = static_cast<LONG_PTR>(width * 4u);
            subresource.SlicePitch = static_cast<LONG_PTR>(width * height * 4u);

            const UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, 1);
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr) || !uploadResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][Generated][ERROR] upload buffer creation failed: " + debugName);
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(uploadCmdList_.Get(), texResource.Get(), uploadResource.Get(), 0, 0, 1, &subresource);
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(DXGI_FORMAT_R8G8B8A8_UNORM, colorSpace);
            const int handle = RegisterFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad(logKind, debugName, colorSpace, srvFormat, handle);
            }
            return handle;
        }

        int DxTextureManager::CreateCubemapFromRgbaPixels(
            const std::string& debugName,
            uint32_t rgba,
            TextureColorSpace colorSpace,
            const char* logKind)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][GeneratedCube][ERROR] invalid generated cubemap request: " + debugName);
                return -1;
            }

            D3D12_RESOURCE_DESC textureDesc{};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Alignment = 0;
            textureDesc.Width = 1;
            textureDesc.Height = 1;
            textureDesc.DepthOrArraySize = 6;
            textureDesc.MipLevels = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            auto textureHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            HRESULT hr = device->CreateCommittedResource(
                &textureHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(texResource.GetAddressOf()));
            if (FAILED(hr) || !texResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][GeneratedCube][ERROR] CreateCommittedResource failed: " + debugName);
                return -1;
            }

            std::array<uint8_t, 6u * 4u> pixels{};
            for (uint32_t face = 0; face < 6u; ++face) {
                WriteRgba(rgba, pixels.data() + face * 4u);
            }

            std::array<D3D12_SUBRESOURCE_DATA, 6> subresources{};
            for (uint32_t face = 0; face < 6u; ++face) {
                subresources[face].pData = pixels.data() + face * 4u;
                subresources[face].RowPitch = 4;
                subresources[face].SlicePitch = 4;
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr) || !uploadResource) {
                HIKARI_LOG_ERROR("[DxTextureManager][GeneratedCube][ERROR] upload buffer creation failed: " + debugName);
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(
                uploadCmdList_.Get(),
                texResource.Get(),
                uploadResource.Get(),
                0,
                0,
                static_cast<UINT>(subresources.size()),
                subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));
            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));
            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(DXGI_FORMAT_R8G8B8A8_UNORM, colorSpace);
            const int handle = RegisterCubeFromResourceAs(texResource.Get(), srvFormat);
            if (handle >= 0) {
                LogTextureLoad(logKind, debugName, colorSpace, srvFormat, handle);
            }
            return handle;
        }


        int DxTextureManager::RegisterFromResource(ID3D12Resource* resource)
        {
            if (!resource) {
                return -1;
            }
            return RegisterFromResourceAs(resource, resource->GetDesc().Format);
        }

        int DxTextureManager::RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat)
        {
            EnsureInit();
            if (!resource) {
                return -1;
            }

            auto* device = context_.device;

            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = resource;
            dimensions_[handle] = TextureDimension::Texture2D;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            auto desc = resource->GetDesc();
            mipCounts_[handle] = std::max<UINT>(1u, static_cast<UINT>(desc.MipLevels));
            formats_[handle] = srvFormat;
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = desc.MipLevels;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(
                resource,
                &srvDesc,
                srvCpu_[handle]
            );

            return handle;
        }

        int DxTextureManager::RegisterCubeFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat)
        {
            EnsureInit();
            if (!resource) {
                return -1;
            }

            auto desc = resource->GetDesc();
            if (desc.DepthOrArraySize < 6) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Resource is not a cubemap texture. depthOrArraySize="
                    << desc.DepthOrArraySize;
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            auto* device = context_.device;
            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = resource;
            dimensions_[handle] = TextureDimension::TextureCube;
            mipCounts_[handle] = std::max<UINT>(1u, static_cast<UINT>(desc.MipLevels));
            formats_[handle] = srvFormat;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = desc.MipLevels;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(resource, &srvDesc, srvCpu_[handle]);
            return handle;
        }

        void DxTextureManager::ReleaseTexture(int handle)
        {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(textures_.size())) {
                return;
            }

            if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
                return;
            }

            const GFX::DescriptorSlot slot{
                GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)
            };

            if (!descriptorAllocator_.IsAllocated(slot)) {
                return;
            }

            if (textures_[handle]) {
                textures_[handle].Reset();
            }
            dimensions_[handle] = TextureDimension::Texture2D;
            mipCounts_[handle] = 0;
            formats_[handle] = DXGI_FORMAT_UNKNOWN;

            RemoveCacheEntriesForHandle(nameToHandle_, handle);
            descriptorAllocator_.Free(slot);
        }

        void DxTextureManager::ReleaseTextureDeferred(int handle)
        {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(textures_.size())) {
                return;
            }

            if (handle >= static_cast<int>(pendingRelease_.size())) {
                return;
            }

            if (pendingRelease_[handle]) {
                return;
            }

            const GFX::DescriptorSlot slot{
                GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)
            };

            if (!descriptorAllocator_.IsAllocated(slot)) {
                return;
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> resourceToRelease = textures_[handle];
            textures_[handle].Reset();
            dimensions_[handle] = TextureDimension::Texture2D;
            mipCounts_[handle] = 0;
            formats_[handle] = DXGI_FORMAT_UNKNOWN;
            pendingRelease_[handle] = true;
            RemoveCacheEntriesForHandle(nameToHandle_, handle);

            GFX::GpuDeferredReleaseQueue* queue = context_.deferredReleaseQueue;
            if (queue == nullptr) {
                HIKARI_LOG_ERROR("[DxTextureManager][DeferredRelease][WARN] deferredReleaseQueue is null. Falling back to immediate descriptor free.");
                pendingRelease_[handle] = false;
                descriptorAllocator_.Free(slot);
                return;
            }

            const uint64_t retireFenceValue = context_.currentFrameRetireFenceValue;
            queue->Enqueue(
                retireFenceValue,
                [resourceToRelease, slot, handle]() mutable {
                    (void)resourceToRelease;
                    DxTextureManager::CompleteDeferredRelease(handle, slot);
                },
                "DxTextureManager::ReleaseTextureDeferred");
        }

        void DxTextureManager::CompleteDeferredRelease(int handle, GFX::DescriptorSlot slot)
        {
            if (handle >= 0 && handle < static_cast<int>(pendingRelease_.size())) {
                pendingRelease_[handle] = false;
            }

            if (!initialized_) {
                return;
            }

            if (!descriptorAllocator_.IsAllocated(slot)) {
                return;
            }

            descriptorAllocator_.Free(slot);
        }

        UINT DxTextureManager::GetUsedDescriptorCount()
        {
            if (!initialized_) {
                return 0;
            }

            return descriptorAllocator_.GetUsedCount();
        }

        UINT DxTextureManager::GetFreeDescriptorCount()
        {
            if (!initialized_) {
                return 0;
            }

            return descriptorAllocator_.GetFreeCount();
        }

        UINT DxTextureManager::GetMaxDescriptorCount()
        {
            if (!initialized_) {
                return 0;
            }

            return descriptorAllocator_.GetCount();
        }

        bool DxTextureManager::IsTextureHandleValid(int handle)
        {
            if (!initialized_) {
                return false;
            }

            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                return false;
            }

            if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
                return false;
            }

            const GFX::DescriptorSlot slot{
                GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)
            };

            if (!descriptorAllocator_.IsAllocated(slot)) {
                return false;
            }

            return textures_[handle] != nullptr;
        }

        ID3D12Resource* DxTextureManager::GetResource(int handle)
        {
            if (!IsTextureHandleValid(handle)) {
                return nullptr;
            }

            return textures_[handle].Get();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE DxTextureManager::GetSrvCpuHandle(int handle)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE nullHandle{};
            nullHandle.ptr = 0;

            if (!IsTextureHandleValid(handle) || handle >= static_cast<int>(srvCpu_.size())) {
                return nullHandle;
            }

            return srvCpu_[handle];
        }

        D3D12_GPU_DESCRIPTOR_HANDLE DxTextureManager::GetSrvGpuHandle(int handle)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE nullHandle{};
            nullHandle.ptr = 0;

            if (!IsTextureHandleValid(handle) || handle >= static_cast<int>(srvGpu_.size())) {
                return nullHandle;
            }

            return srvGpu_[handle];
        }

        UINT DxTextureManager::GetSrvDescriptorIndex(int handle)
        {
            if (!IsTextureHandleValid(handle)) {
                return kInvalidSrvDescriptorIndex;
            }

            return GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle);
        }

        ID3D12DescriptorHeap* DxTextureManager::GetSrvHeap()
        {
            return srvHeap_.Get();
        }

        void DxTextureManager::GetTextureSize(int handle, UINT& outWidth, UINT& outHeight) {
            outWidth = 0;
            outHeight = 0;
            if (!initialized_) { return; }
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) { return; }
            if (!textures_[handle]) { return; }

            auto desc = textures_[handle]->GetDesc();
            outWidth = static_cast<UINT>(desc.Width);
            outHeight = static_cast<UINT>(desc.Height);
        }

        TextureDimension DxTextureManager::GetTextureDimension(int handle) {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(dimensions_.size())) {
                return TextureDimension::Texture2D;
            }
            return dimensions_[handle];
        }

        UINT DxTextureManager::GetTextureMipCount(int handle) {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(mipCounts_.size())) {
                return 0;
            }

            if (handle >= static_cast<int>(textures_.size()) || !textures_[handle]) {
                return 0;
            }

            if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
                return 0;
            }

            return mipCounts_[handle];
        }

        DXGI_FORMAT DxTextureManager::GetTextureFormat(int handle) {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(formats_.size())) {
                return DXGI_FORMAT_UNKNOWN;
            }

            if (handle >= static_cast<int>(textures_.size()) || !textures_[handle]) {
                return DXGI_FORMAT_UNKNOWN;
            }

            if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
                return DXGI_FORMAT_UNKNOWN;
            }

            return formats_[handle];
        }


    } // namespace DXTEX
} // namespace HIKARI
