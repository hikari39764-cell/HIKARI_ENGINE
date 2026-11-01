#include "RenderCore/Texture/HIKARI_DxTextureInternal.h"
#include <algorithm>
#include <iterator>
#include <sstream>
#include <d3dx12.h>
#include "RenderContracts/Texture/HIKARI_HtexFormat.h"
#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_VideoMemory.h"

using Microsoft::WRL::ComPtr;
namespace HIKARI::DXTEX {
using namespace DETAIL;
namespace {
TextureColorSpace ToRuntimeColorSpace(TextureAssetColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureAssetColorSpace::Linear: return TextureColorSpace::Linear;
    case TextureAssetColorSpace::Srgb: return TextureColorSpace::Srgb;
    case TextureAssetColorSpace::Auto:
    default: return TextureColorSpace::Auto;
    }
}

} // namespace
std::vector<TextureLoadResult> DxTextureManager::LoadTexturesWithColorSpaceBatch(
    const std::vector<TextureLoadRequest>& requests, TextureBatchLoadStats* outStats, TextureBatchLoadLimits limits) {
    EnsureInit();
    constexpr uint64_t maximumBytes = 512ull * 1024 * 1024;
    limits.maxSingleTextureBytes = std::clamp(limits.maxSingleTextureBytes, uint64_t{1}, maximumBytes);
    limits.maxPreparedBytes = std::clamp(limits.maxPreparedBytes, uint64_t{1}, limits.maxSingleTextureBytes);
    limits.maxUploadBytes = std::clamp(limits.maxUploadBytes, uint64_t{1}, limits.maxSingleTextureBytes);
    limits.maxTexturesPerBatch = std::clamp(limits.maxTexturesPerBatch, 1u, 256u);
    TextureBatchLoadStats total{};
    std::vector<TextureLoadResult> results;
    results.reserve(requests.size());
    std::vector<TextureLoadRequest> batch;
    batch.reserve(limits.maxTexturesPerBatch);
    uint64_t preparedBytes = 0, uploadBytes = 0;
    const auto flush = [&] {
        if (batch.empty())
            return;
        TextureBatchLoadStats stats;
        auto loaded = LoadTextureBatch(batch, &stats, (std::max)(limits.maxPreparedBytes, preparedBytes),
                                       (std::max)(limits.maxUploadBytes, uploadBytes));
        results.insert(results.end(), std::make_move_iterator(loaded.begin()), std::make_move_iterator(loaded.end()));
        total.requested += stats.requested;
        total.cacheHits += stats.cacheHits;
        total.uploaded += stats.uploaded;
        total.fallbackLoads += stats.fallbackLoads;
        total.failed += stats.failed;
        total.uploadedBytes += stats.uploadedBytes;
        total.submissions += stats.submissions;
        total.peakPreparedBytes = (std::max)(total.peakPreparedBytes, stats.peakPreparedBytes);
        total.peakUploadBytes = (std::max)(total.peakUploadBytes, stats.peakUploadBytes);
        batch.clear();
        preparedBytes = uploadBytes = 0;
    };
    for (const auto& request : requests) {
        uint64_t payload = 0, footprint = 0;
        if (context_.device && ASSETS::SEMANTICS::ClassifyCookedAssetFormat(request.path) == CookedAssetFormat::HTEX &&
            FindLoadedTextureWithColorSpace(request.name, request.path, request.colorSpace) < 0) {
            HtexFileInfo info;
            std::string error;
            if (InspectHtexFile(TEXT::PathFromUtf8(request.path), info, error)) {
                payload = info.payloadSize;
                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = info.width;
                desc.Height = info.height;
                desc.DepthOrArraySize = static_cast<UINT16>(info.arraySize);
                desc.MipLevels = static_cast<UINT16>(info.mipLevels);
                desc.Format = info.format;
                desc.SampleDesc.Count = 1;
                context_.device->GetCopyableFootprints(&desc, 0, static_cast<UINT>(info.subresources.size()), 0,
                                                       nullptr, nullptr, nullptr, &footprint);
            }
        }
        if (payload > limits.maxSingleTextureBytes || footprint > limits.maxSingleTextureBytes) {
            flush();
            results.push_back({request.name, request.path, request.colorSpace});
            ++total.requested;
            ++total.failed;
            HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch] single texture exceeds load budget: " + request.path);
            continue;
        }
        if (!batch.empty() &&
            (batch.size() >= limits.maxTexturesPerBatch || preparedBytes + payload > limits.maxPreparedBytes ||
             uploadBytes + footprint > limits.maxUploadBytes))
            flush();
        batch.push_back(request);
        preparedBytes += payload;
        uploadBytes += footprint;
    }
    flush();
    if (outStats)
        *outStats = total;
    return results;
}

std::vector<TextureLoadResult> DxTextureManager::LoadTextureBatch(const std::vector<TextureLoadRequest>& requests,
                                                                  TextureBatchLoadStats* outStats,
                                                                  uint64_t preparedBudget, uint64_t uploadBudget) {
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
        result.colorSpace = ResolveTextureLoadColorSpace(request.name, request.path, request.colorSpace);

        const std::string cacheKey = MakeTextureCacheKey(request.name, request.path, result.colorSpace);

        const auto cached = nameToHandle_.find(cacheKey);
        if (cached != nameToHandle_.end() && IsTextureHandleValid(cached->second)) {
            result.handle = cached->second;
            result.cacheHit = true;
            result.loaded = true;
            ++stats.cacheHits;
            continue;
        }

        if (ASSETS::SEMANTICS::ClassifyCookedAssetFormat(request.path) != CookedAssetFormat::HTEX) {
            result.handle = LoadTextureWithColorSpace(request.name, request.path, request.colorSpace);
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
    if (!device || !queue) {
        HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch][ERROR] invalid D3D12 context.");
        for (const PendingHtexLoad& load : pendingHtexLoads) {
            failResults(load.resultIndices);
        }
        if (outStats != nullptr) {
            *outStats = stats;
        }
        return results;
    }

    uint64_t preparedBytes = 0, uploadBytes = 0;
    for (PendingHtexLoad& load : pendingHtexLoads) {
        std::string htexMessage{};
        if (!ReadHtexFile(TEXT::PathFromUtf8(load.request.path), load.htex, htexMessage,
                          preparedBudget - preparedBytes)) {
            HIKARI_LOG_ERROR(htexMessage);
            failResults(load.resultIndices);
            continue;
        }
        for (const auto& subresource : load.htex.subresources)
            preparedBytes += subresource.data.size();
        stats.peakPreparedBytes = preparedBytes;

        if (load.htex.width == 0 || load.htex.height == 0 || load.htex.arraySize == 0 ||
            load.htex.subresources.empty() || load.htex.format == DXGI_FORMAT_UNKNOWN ||
            load.htex.arraySize > 0xffffu || load.htex.mipLevels > 0xffffu) {
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
        HRESULT hr = device->CreateCommittedResource(&textureHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(load.textureResource.GetAddressOf()));
        if (FAILED(hr) || !load.textureResource) {
            std::ostringstream oss;
            oss << "[DxTextureManager][HTEXBatch][ERROR] CreateCommittedResource failed. path=" << load.request.path
                << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
            HIKARI_LOG_ERROR(oss.str());
            failResults(load.resultIndices);
            continue;
        }
        GFX::VIDEO_MEMORY::Track(load.textureResource.Get(), GFX::VIDEO_MEMORY::Category::Texture);

        load.subresources.reserve(load.htex.subresources.size());
        for (const HtexSubresource& source : load.htex.subresources) {
            D3D12_SUBRESOURCE_DATA subresource{};
            subresource.pData = source.data.data();
            subresource.RowPitch = static_cast<LONG_PTR>(source.rowPitch);
            subresource.SlicePitch = static_cast<LONG_PTR>(source.slicePitch);
            load.subresources.push_back(subresource);
        }

        load.uploadBytes =
            GetRequiredIntermediateSize(load.textureResource.Get(), 0, static_cast<UINT>(load.subresources.size()));
        if (load.uploadBytes == 0 || load.uploadBytes > uploadBudget - uploadBytes) {
            HIKARI_LOG_ERROR("[DxTextureManager][HTEXBatch] texture upload exceeds batch budget: " + load.request.path);
            failResults(load.resultIndices);
            continue;
        }

        auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(load.uploadBytes);
        hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(load.uploadResource.GetAddressOf()));
        if (FAILED(hr) || !load.uploadResource) {
            std::ostringstream oss;
            oss << "[DxTextureManager][HTEXBatch][ERROR] upload buffer creation failed. path=" << load.request.path
                << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
            HIKARI_LOG_ERROR(oss.str());
            failResults(load.resultIndices);
            continue;
        }

        const TextureColorSpace embeddedColorSpace = ToRuntimeColorSpace(load.htex.colorSpace);
        load.effectiveColorSpace =
            load.requestedColorSpace == TextureColorSpace::Auto ? embeddedColorSpace : load.requestedColorSpace;
        load.srvFormat = ResolveSrvFormat(load.htex.format, load.effectiveColorSpace);
        load.readyForUpload = true;
        uploadBytes += load.uploadBytes;
        stats.peakUploadBytes = uploadBytes;
    }

    const bool hasReadyUpload = std::any_of(pendingHtexLoads.begin(), pendingHtexLoads.end(),
                                            [](const PendingHtexLoad& load) { return load.readyForUpload; });
    if (!hasReadyUpload) {
        if (outStats != nullptr) {
            *outStats = stats;
        }
        return results;
    }

    auto* uploadCommands = BeginUpload();
    if (!uploadCommands) {
        for (const auto& load : pendingHtexLoads)
            if (load.readyForUpload)
                failResults(load.resultIndices);
        if (outStats)
            *outStats = stats;
        return results;
    }

    for (PendingHtexLoad& load : pendingHtexLoads) {
        if (!load.readyForUpload) {
            continue;
        }

        const auto copiedBytes =
            UpdateSubresources(uploadCommands, load.textureResource.Get(), load.uploadResource.Get(), 0, 0,
                               static_cast<UINT>(load.subresources.size()), load.subresources.data());
        if (copiedBytes == 0) {
            failResults(load.resultIndices);
            load.readyForUpload = false;
            continue;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(load.textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                            D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        uploadCommands->ResourceBarrier(1, &barrier);
    }

    std::vector<ID3D12Resource*> retained;
    retained.reserve(pendingHtexLoads.size() * 2);
    for (const auto& load : pendingHtexLoads) {
        retained.push_back(load.textureResource.Get());
        retained.push_back(load.uploadResource.Get());
    }
    if (!uploads_.Submit(retained, uploadBytes)) {
        for (const auto& load : pendingHtexLoads)
            if (load.readyForUpload)
                failResults(load.resultIndices);
        if (outStats)
            *outStats = stats;
        return results;
    }
    ++stats.submissions;

    for (PendingHtexLoad& load : pendingHtexLoads) {
        if (!load.readyForUpload) {
            continue;
        }

        stats.uploadedBytes += load.uploadBytes;

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

        LogTextureLoad(load.htex.dimension == HtexTextureDimension::TextureCube ? "HTEX Cube Batch" : "HTEX 2D Batch",
                       load.request.path, load.effectiveColorSpace, load.srvFormat, handle);
    }

    {
        std::ostringstream oss;
        oss << "[DxTextureManager][HTEXBatch] requested=" << stats.requested << " cacheHits=" << stats.cacheHits
            << " uploaded=" << stats.uploaded << " fallbackLoads=" << stats.fallbackLoads << " failed=" << stats.failed
            << " uploadBytes=" << stats.uploadedBytes << " peakPreparedBytes=" << stats.peakPreparedBytes
            << " peakUploadBytes=" << stats.peakUploadBytes;
        HIKARI_LOG_INFO(oss.str());
    }

    if (outStats != nullptr) {
        *outStats = stats;
    }
    return results;
}

} // namespace HIKARI::DXTEX
