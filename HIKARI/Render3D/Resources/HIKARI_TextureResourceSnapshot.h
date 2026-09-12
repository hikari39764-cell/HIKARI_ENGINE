#pragma once

#include <map>
#include <span>
#include <tuple>
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D {

    // 不変な asset texture とその view を一つの SRV heap に固定する。
    // 材質や空の意味付けは消費側が所有する。破棄は frame fence へ委譲する。
    class TextureResourceSnapshot final {
    public:
        ~TextureResourceSnapshot();
        bool Initialize(ID3D12Device* device, std::span<const TextureSnapshotSource> sources, std::string& error);
        void Reset();
        ID3D12DescriptorHeap* GetHeap() const { return heap_.Get(); }
        uint32_t GetDescriptorIndex(const TextureSnapshotSource& source) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetDescriptor(uint32_t index) const;
        static constexpr uint32_t GetNullDescriptorIndex(TextureResourceDimension dimension) {
            return dimension == TextureResourceDimension::TextureCube ? 1u : 0u;
        }

    private:
        using SourceKey = std::tuple<std::string, TextureResourceColorSpace, TextureResourceDimension, uint32_t, uint32_t>;
        static SourceKey MakeSourceKey(const TextureSnapshotSource& source);
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_{};
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_{};
        std::map<SourceKey, uint32_t> descriptors_{};
        UINT stride_ = 0;
    };

} // namespace HIKARI::RENDER3D
