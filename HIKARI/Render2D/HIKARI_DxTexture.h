#pragma once
#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxContext.h"
#include <d3d12.h>
#include <cstdint>
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {
    namespace DXTEX {

        constexpr UINT kInvalidSrvDescriptorIndex = 0xffffffffu;

        enum class TextureColorSpace {
            Auto,
            Linear,
            Srgb
        };

        enum class TextureDimension {
            Texture2D,
            TextureCube
        };

        struct TextureLoadRequest {
            std::string name{};
            std::string path{};
            TextureColorSpace colorSpace = TextureColorSpace::Auto;
        };

        struct TextureLoadResult {
            std::string name{};
            std::string path{};
            TextureColorSpace colorSpace = TextureColorSpace::Auto;
            int handle = -1;
            bool cacheHit = false;
            bool loaded = false;
        };

        struct TextureBatchLoadStats {
            uint32_t requested = 0;
            uint32_t cacheHits = 0;
            uint32_t uploaded = 0;
            uint32_t fallbackLoads = 0;
            uint32_t failed = 0;
            uint64_t uploadedBytes = 0;
        };

        class DxTextureManager {
        public:
            static void Init(const GFX::Context& ctx, int maxTextures = GFX::DESCRIPTOR::kUserSrvCount);
            static void Finalize();
            static void UpdateContext(const GFX::Context& ctx);

            static int LoadTexture(const std::string& name, const std::string& path);
            static int LoadTextureWithColorSpace(const std::string& name, const std::string& path, TextureColorSpace colorSpace);
            static int FindLoadedTextureWithColorSpace(
                const std::string& name,
                const std::string& path,
                TextureColorSpace colorSpace);
            static std::vector<TextureLoadResult> LoadTexturesWithColorSpaceBatch(
                const std::vector<TextureLoadRequest>& requests,
                TextureBatchLoadStats* outStats = nullptr);
            static int LoadTextureSrgb(const std::string& name, const std::string& path);
            static int LoadTextureLinear(const std::string& name, const std::string& path);
            static int LoadCubemap(const std::string& name, const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int CreateSolidColorTexture(const std::string& name, uint32_t rgba, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int CreateSolidColorCubemap(const std::string& name, uint32_t rgba, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int CreateCheckerTexture(const std::string& name, uint32_t colorA, uint32_t colorB, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int RegisterFromResource(ID3D12Resource* resource);
            static int RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat);
            static int RegisterCubeFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat);

            static void InvalidateTextureCacheByName(const std::string& name);
            static void InvalidateTextureCacheByPath(const std::string& path);
            static void InvalidateAllTextureCache();

            static void ReleaseTexture(int handle);
            // Defers releasing the texture resource and descriptor slot until the GPU fence
            // confirms the current frame no longer uses them.
            static void ReleaseTextureDeferred(int handle);
            static UINT GetUsedDescriptorCount();
            static UINT GetFreeDescriptorCount();
            static UINT GetMaxDescriptorCount();
            static ID3D12Resource* GetResource(int handle);
            static D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuHandle(int handle);
            static D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(int handle);
            static UINT GetSrvDescriptorIndex(int handle);
            static bool IsTextureHandleValid(int handle);
            static ID3D12DescriptorHeap* GetSrvHeap();
            static void GetTextureSize(int handle, UINT& outWidth, UINT& outHeight);
            static TextureDimension GetTextureDimension(int handle);
            static UINT GetTextureMipCount(int handle);
            static DXGI_FORMAT GetTextureFormat(int handle);

        private:
            static void EnsureInit();
            static int CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Auto);
            static int CreateDdsTextureFromFile(const std::string& path, TextureColorSpace colorSpace);
            static int CreateTgaTextureFromFile(const std::string& path, TextureColorSpace colorSpace);
            static int CreateTextureFromHtexFile(const std::string& path, TextureColorSpace colorSpace);
            static int CreateCubemapFromFile(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int CreateTextureFromRgbaPixels(
                const std::string& debugName,
                uint32_t width,
                uint32_t height,
                const uint8_t* rgbaPixels,
                TextureColorSpace colorSpace,
                const char* logKind);
            static int CreateCubemapFromRgbaPixels(
                const std::string& debugName,
                uint32_t rgba,
                TextureColorSpace colorSpace,
                const char* logKind);
            static void CompleteDeferredRelease(int handle, GFX::DescriptorSlot slot);

            static bool initialized_;
            static GFX::Context context_;
            static UINT descriptorSize_;

            static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
            static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator_;
            static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCmdList_;
            static Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence_;
            static HANDLE uploadFenceEvent_;
            static uint64_t uploadFenceValue_;
            static std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
            static std::vector<TextureDimension> dimensions_;
            static std::vector<UINT> mipCounts_;
            static std::vector<DXGI_FORMAT> formats_;
            static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvCpu_;
            static std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> srvGpu_;
            static std::vector<bool> pendingRelease_;
            static std::unordered_map<std::string, int> nameToHandle_;
            static GFX::DescriptorAllocator descriptorAllocator_;
        };

    } // namespace DXTEX
} // namespace HIKARI
