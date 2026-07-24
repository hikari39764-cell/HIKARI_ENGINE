#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "Render2D/HIKARI_DxTexture.h"
#include "Render3D/Resources/HIKARI_RenderResourceSystem.h"

namespace HIKARI::RENDER3D {

    namespace {

        struct TextureResourceSystemState {
            // Keep D3D12 descriptors in the texture backend; render resources are owned by RenderResourceSystem.
            std::unordered_map<uint64_t, int> backendByResource{};
            std::unordered_map<int, TextureResourceHandle> resourceByBackend{};
        };

        TextureResourceSystemState& State() {
            static TextureResourceSystemState state{};
            return state;
        }

        RenderResourcePool& Pool() {
            return GetRenderResourcePool();
        }

        uint64_t PackHandle(TextureResourceHandle handle) {
            return (static_cast<uint64_t>(handle.generation) << 32) |
                static_cast<uint64_t>(handle.index);
        }

        DXTEX::TextureColorSpace ToBackendColorSpace(TextureResourceColorSpace colorSpace) {
            switch (colorSpace) {
            case TextureResourceColorSpace::Linear:
                return DXTEX::TextureColorSpace::Linear;
            case TextureResourceColorSpace::Srgb:
                return DXTEX::TextureColorSpace::Srgb;
            case TextureResourceColorSpace::Auto:
            default:
                return DXTEX::TextureColorSpace::Auto;
            }
        }

        std::string BuildSourceKey(const std::string& name, const std::string& path) {
            return path.empty() ? name : path;
        }

        RenderResourceDesc BuildTextureDesc(
            const std::string& name,
            const std::string& sourceKey,
            RenderResourceLifetime lifetime) {

            RenderResourceDesc desc{};
            desc.kind = RenderResourceKind::Texture;
            desc.usage = RenderResourceUsageFlags::ShaderResource | RenderResourceUsageFlags::CopyDest;
            desc.lifetime = lifetime;
            desc.debugName = name.empty() ? sourceKey : name;
            desc.sourceKey = sourceKey;
            return desc;
        }

        RenderResourceDesc BuildDescFromBackend(int backendHandle, RenderResourceDesc desc) {
            desc.kind = RenderResourceKind::Texture;
            if (desc.usage == RenderResourceUsageFlags::None) {
                desc.usage = RenderResourceUsageFlags::ShaderResource | RenderResourceUsageFlags::CopyDest;
            }

            UINT width = 0;
            UINT height = 0;
            DXTEX::DxTextureManager::GetTextureSize(backendHandle, width, height);
            if (desc.width == 0) {
                desc.width = width;
            }
            if (desc.height == 0) {
                desc.height = height;
            }
            if (desc.mipLevels == 0) {
                desc.mipLevels = static_cast<uint16_t>(DXTEX::DxTextureManager::GetTextureMipCount(backendHandle));
            }
            if (desc.format == DXGI_FORMAT_UNKNOWN) {
                desc.format = DXTEX::DxTextureManager::GetTextureFormat(backendHandle);
            }
            if (desc.depthOrArraySize == 0) {
                desc.depthOrArraySize =
                    DXTEX::DxTextureManager::GetTextureDimension(backendHandle) == DXTEX::TextureDimension::TextureCube
                    ? 6
                    : 1;
            }

            return desc;
        }

        void AttachSrvView(TextureResourceHandle handle, int backendHandle) {
            RenderResourceView srv{};
            srv.descriptorIndex = DXTEX::DxTextureManager::GetSrvDescriptorIndex(backendHandle);
            srv.cpu = DXTEX::DxTextureManager::GetSrvCpuHandle(backendHandle);
            srv.gpu = DXTEX::DxTextureManager::GetSrvGpuHandle(backendHandle);
            Pool().SetView(handle.ToUntyped(), RenderResourceViewKind::Srv, srv);
        }

        TextureResourceHandle RegisterBackendTexture(int backendHandle, RenderResourceDesc desc) {
            if (!DXTEX::DxTextureManager::IsTextureHandleValid(backendHandle)) {
                return {};
            }

            TextureResourceSystemState& state = State();
            const auto cached = state.resourceByBackend.find(backendHandle);
            if (cached != state.resourceByBackend.end() &&
                Pool().IsAlive(cached->second.ToUntyped())) {
                return cached->second;
            }

            desc = BuildDescFromBackend(backendHandle, std::move(desc));

            ID3D12Resource* nativeResource = DXTEX::DxTextureManager::GetResource(backendHandle);
            TextureResourceHandle handle = nativeResource != nullptr
                ? Pool().RegisterExternalTexture(nativeResource, std::move(desc))
                : TextureResourceHandle::FromUntyped(
                    Pool().RegisterVirtual(RenderResourceKind::Texture, std::move(desc)));

            if (!handle) {
                return {};
            }

            AttachSrvView(handle, backendHandle);
            state.backendByResource[PackHandle(handle)] = backendHandle;
            state.resourceByBackend[backendHandle] = handle;
            return handle;
        }

        TextureResourceHandle RegisterLoadedTexture(
            int backendHandle,
            const std::string& name,
            const std::string& sourceKey,
            RenderResourceLifetime lifetime) {

            return RegisterBackendTexture(
                backendHandle,
                BuildTextureDesc(name, sourceKey, lifetime));
        }

        TextureResourceHandle ResolveBackendTextureResource(int backendHandle) {
            TextureResourceHandle handle = FindTextureResourceFromBackendHandle(backendHandle);
            if (handle) {
                return handle;
            }

            return RegisterTextureResourceFromBackendHandle(backendHandle);

        }

    } // namespace

    ID3D12DescriptorHeap* GetTextureResourceSrvHeap() {
        return DXTEX::DxTextureManager::GetSrvHeap();
    }

    TextureResourceSystemStats GetTextureResourceSystemStats() {
        TextureResourceSystemStats stats{};
        stats.pool = Pool().GetStats();
        stats.backendUsedDescriptorCount = DXTEX::DxTextureManager::GetUsedDescriptorCount();
        stats.backendFreeDescriptorCount = DXTEX::DxTextureManager::GetFreeDescriptorCount();
        stats.backendMaxDescriptorCount = DXTEX::DxTextureManager::GetMaxDescriptorCount();
        return stats;
    }

    TextureResourceHandle LoadTextureResource(
        const std::string& name,
        const std::string& path) {

        return LoadTextureResourceWithColorSpace(name, path, TextureResourceColorSpace::Auto);
    }

    TextureResourceHandle LoadTextureResourceWithColorSpace(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle = DXTEX::DxTextureManager::LoadTextureWithColorSpace(
            name,
            path,
            ToBackendColorSpace(colorSpace));

        return RegisterLoadedTexture(
            backendHandle,
            name,
            BuildSourceKey(name, path),
            RenderResourceLifetime::ImportedAsset);
    }

    TextureResourceHandle FindLoadedTextureResourceWithColorSpace(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle =
            DXTEX::DxTextureManager::FindLoadedTextureWithColorSpace(
                name,
                path,
                ToBackendColorSpace(colorSpace));
        if (backendHandle < 0) {
            return {};
        }

        return RegisterLoadedTexture(
            backendHandle,
            name,
            BuildSourceKey(name, path),
            RenderResourceLifetime::ImportedAsset);
    }

    std::vector<TextureResourceHandle> PreloadTextureResourcesWithColorSpace(
        const std::vector<TextureResourceLoadRequest>& requests,
        TextureResourceBatchLoadStats* outStats) {

        std::vector<DXTEX::TextureLoadRequest> backendRequests{};
        backendRequests.reserve(requests.size());
        for (const TextureResourceLoadRequest& request : requests) {
            backendRequests.push_back(DXTEX::TextureLoadRequest{
                request.name,
                request.path,
                ToBackendColorSpace(request.colorSpace) });
        }

        DXTEX::TextureBatchLoadStats backendStats{};
        const std::vector<DXTEX::TextureLoadResult> backendResults =
            DXTEX::DxTextureManager::LoadTexturesWithColorSpaceBatch(
                backendRequests,
                &backendStats);

        std::vector<TextureResourceHandle> handles(requests.size());
        TextureResourceBatchLoadStats stats{};
        stats.requested = backendStats.requested;
        stats.cacheHits = backendStats.cacheHits;
        stats.uploaded = backendStats.uploaded;
        stats.fallbackLoads = backendStats.fallbackLoads;
        stats.failed = backendStats.failed;
        stats.uploadedBytes = backendStats.uploadedBytes;

        const size_t count = (std::min)(requests.size(), backendResults.size());
        for (size_t i = 0; i < count; ++i) {
            const DXTEX::TextureLoadResult& backendResult = backendResults[i];
            if (!backendResult.loaded || backendResult.handle < 0) {
                continue;
            }

            const TextureResourceLoadRequest& request = requests[i];
            TextureResourceHandle handle = RegisterLoadedTexture(
                backendResult.handle,
                request.name,
                BuildSourceKey(request.name, request.path),
                RenderResourceLifetime::ImportedAsset);
            handles[i] = handle;
            if (handle) {
                ++stats.registered;
            }
        }

        if (outStats != nullptr) {
            *outStats = stats;
        }
        return handles;
    }

    TextureResourceHandle LoadTextureResourceSrgb(
        const std::string& name,
        const std::string& path) {

        return LoadTextureResourceWithColorSpace(name, path, TextureResourceColorSpace::Srgb);
    }

    TextureResourceHandle LoadTextureResourceLinear(
        const std::string& name,
        const std::string& path) {

        return LoadTextureResourceWithColorSpace(name, path, TextureResourceColorSpace::Linear);
    }

    TextureResourceHandle LoadCubemapResource(
        const std::string& name,
        const std::string& path,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle = DXTEX::DxTextureManager::LoadCubemap(
            name,
            path,
            ToBackendColorSpace(colorSpace));

        return RegisterLoadedTexture(
            backendHandle,
            name,
            BuildSourceKey(name, path),
            RenderResourceLifetime::ImportedAsset);
    }

    TextureResourceHandle CreateSolidColorTextureResource(
        const std::string& name,
        uint32_t rgba,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle = DXTEX::DxTextureManager::CreateSolidColorTexture(
            name,
            rgba,
            ToBackendColorSpace(colorSpace));

        return RegisterLoadedTexture(
            backendHandle,
            name,
            name,
            RenderResourceLifetime::Persistent);
    }

    TextureResourceHandle CreateSolidColorCubemapResource(
        const std::string& name,
        uint32_t rgba,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle = DXTEX::DxTextureManager::CreateSolidColorCubemap(
            name,
            rgba,
            ToBackendColorSpace(colorSpace));

        return RegisterLoadedTexture(
            backendHandle,
            name,
            name,
            RenderResourceLifetime::Persistent);
    }

    TextureResourceHandle CreateCheckerTextureResource(
        const std::string& name,
        uint32_t colorA,
        uint32_t colorB,
        TextureResourceColorSpace colorSpace) {

        const int backendHandle = DXTEX::DxTextureManager::CreateCheckerTexture(
            name,
            colorA,
            colorB,
            ToBackendColorSpace(colorSpace));

        return RegisterLoadedTexture(
            backendHandle,
            name,
            name,
            RenderResourceLifetime::Persistent);
    }

    TextureResourceHandle RegisterTextureResourceFromNative(
        ID3D12Resource* resource,
        DXGI_FORMAT srvFormat,
        RenderResourceDesc desc) {

        const int backendHandle = DXTEX::DxTextureManager::RegisterFromResourceAs(resource, srvFormat);
        if (desc.lifetime == RenderResourceLifetime::Persistent) {
            desc.lifetime = RenderResourceLifetime::External;
        }
        return RegisterBackendTexture(backendHandle, std::move(desc));
    }

    TextureResourceHandle RegisterCubeTextureResourceFromNative(
        ID3D12Resource* resource,
        DXGI_FORMAT srvFormat,
        RenderResourceDesc desc) {

        const int backendHandle = DXTEX::DxTextureManager::RegisterCubeFromResourceAs(resource, srvFormat);
        if (desc.lifetime == RenderResourceLifetime::Persistent) {
            desc.lifetime = RenderResourceLifetime::External;
        }
        return RegisterBackendTexture(backendHandle, std::move(desc));
    }

    TextureResourceHandle RegisterTextureResourceFromBackendHandle(
        int backendHandle,
        RenderResourceDesc desc) {

        return RegisterBackendTexture(backendHandle, std::move(desc));
    }

    TextureResourceHandle FindTextureResourceFromBackendHandle(int backendHandle) {
        const auto found = State().resourceByBackend.find(backendHandle);
        if (found == State().resourceByBackend.end()) {
            return {};
        }
        return Pool().IsAlive(found->second.ToUntyped()) ? found->second : TextureResourceHandle{};
    }

    bool ReleaseTextureResource(TextureResourceHandle handle) {
        if (!handle) {
            return false;
        }

        TextureResourceSystemState& state = State();
        const uint64_t packed = PackHandle(handle);
        const auto found = state.backendByResource.find(packed);
        if (found == state.backendByResource.end()) {
            return Pool().Release(handle.ToUntyped());
        }

        const int backendHandle = found->second;
        state.backendByResource.erase(found);
        state.resourceByBackend.erase(backendHandle);
        Pool().MarkPendingRelease(handle.ToUntyped());
        DXTEX::DxTextureManager::ReleaseTextureDeferred(backendHandle);
        return Pool().Release(handle.ToUntyped());
    }

    bool IsTextureResourceValid(TextureResourceHandle handle) {
        return handle && Pool().IsAlive(handle.ToUntyped());
    }

    int GetTextureResourceBackendHandle(TextureResourceHandle handle) {
        if (!IsTextureResourceValid(handle)) {
            return -1;
        }

        const auto found = State().backendByResource.find(PackHandle(handle));
        return found != State().backendByResource.end() ? found->second : -1;
    }

    ID3D12Resource* GetTextureResourceNative(TextureResourceHandle handle) {
        return IsTextureResourceValid(handle)
            ? Pool().GetResource(handle.ToUntyped())
            : nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetTextureResourceSrvCpuHandle(TextureResourceHandle handle) {
        D3D12_CPU_DESCRIPTOR_HANDLE nullHandle{};
        const RenderResourceView* srv = Pool().GetView(handle.ToUntyped(), RenderResourceViewKind::Srv);
        return srv != nullptr ? srv->cpu : nullHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureResourceSrvGpuHandle(TextureResourceHandle handle) {
        D3D12_GPU_DESCRIPTOR_HANDLE nullHandle{};
        const RenderResourceView* srv = Pool().GetView(handle.ToUntyped(), RenderResourceViewKind::Srv);
        return srv != nullptr ? srv->gpu : nullHandle;
    }

    UINT GetTextureResourceSrvDescriptorIndex(TextureResourceHandle handle) {
        const RenderResourceView* srv = Pool().GetView(handle.ToUntyped(), RenderResourceViewKind::Srv);
        return srv != nullptr ? srv->descriptorIndex : UINT32_MAX;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureResourceSrvGpuHandleFromBackendHandle(int backendHandle) {
        return GetTextureResourceSrvGpuHandle(ResolveBackendTextureResource(backendHandle));
    }

    UINT GetTextureResourceSrvDescriptorIndexFromBackendHandle(int backendHandle) {
        return GetTextureResourceSrvDescriptorIndex(ResolveBackendTextureResource(backendHandle));
    }

    UINT GetTextureResourceMipCount(TextureResourceHandle handle) {
        const RenderResourceRecord* record = GetTextureResourceRecord(handle);
        return record != nullptr ? record->desc.mipLevels : 0;
    }

    DXGI_FORMAT GetTextureResourceFormat(TextureResourceHandle handle) {
        const RenderResourceRecord* record = GetTextureResourceRecord(handle);
        return record != nullptr ? record->desc.format : DXGI_FORMAT_UNKNOWN;
    }

    TextureResourceDimension GetTextureResourceDimension(TextureResourceHandle handle) {
        const RenderResourceRecord* record = GetTextureResourceRecord(handle);
        if (record == nullptr) {
            return TextureResourceDimension::Unknown;
        }

        return record->desc.depthOrArraySize == 6
            ? TextureResourceDimension::TextureCube
            : TextureResourceDimension::Texture2D;
    }

    const RenderResourceRecord* GetTextureResourceRecord(TextureResourceHandle handle) {
        return IsTextureResourceValid(handle)
            ? Pool().GetRecord(handle.ToUntyped())
            : nullptr;
    }

} // namespace HIKARI::RENDER3D
