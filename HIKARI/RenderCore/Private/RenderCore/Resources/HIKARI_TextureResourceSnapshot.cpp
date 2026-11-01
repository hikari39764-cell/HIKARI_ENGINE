#include "RenderCore/Resources/HIKARI_TextureResourceSnapshot.h"

#include <DirectXTex.h>
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::RENDER3D {

TextureResourceSnapshot::~TextureResourceSnapshot() { Reset(); }

void TextureResourceSnapshot::Reset() {
    GFX::RetireD3D12ObjectForCurrentFrame(heap_, "TextureSnapshot.Descriptors");
    for (auto& texture : textures_)
        GFX::RetireD3D12ObjectForCurrentFrame(texture, "TextureSnapshot.Texture");
    textures_.clear();
    descriptors_.clear();
    stride_ = 0;
}

TextureResourceSnapshot::SourceKey TextureResourceSnapshot::MakeSourceKey(const TextureSnapshotSource& source) {
    return {source.load.path, source.load.colorSpace, source.dimension,
            source.load.path.empty() ? source.resource.index : 0,
            source.load.path.empty() ? source.resource.generation : 0};
}

uint32_t TextureResourceSnapshot::GetDescriptorIndex(const TextureSnapshotSource& source) const {
    if (!source.enabled)
        return UINT32_MAX;
    const auto found = descriptors_.find(MakeSourceKey(source));
    return found != descriptors_.end() ? found->second : UINT32_MAX;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureResourceSnapshot::GetDescriptor(uint32_t index) const {
    if (!heap_ || index >= GFX::DESCRIPTOR::kUserSrvCount)
        return {};
    return {heap_->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(index) * stride_};
}

bool TextureResourceSnapshot::Initialize(ID3D12Device* device, std::span<const TextureSnapshotSource> sources,
                                         std::string& error) {
    Reset();
    error.clear();
    const auto fail = [&](std::string message) {
        error = std::move(message);
        Reset();
        return false;
    };
    if (!device)
        return fail("Texture snapshot requires a device.");
    std::map<SourceKey, size_t> requestIndices;
    std::vector<TextureResourceLoadRequest> requests;
    for (const auto& source : sources) {
        if (!source.enabled || source.load.path.empty() || source.dimension != TextureResourceDimension::Texture2D)
            continue;
        const auto [entry, inserted] = requestIndices.try_emplace(MakeSourceKey(source), requests.size());
        if (inserted)
            requests.push_back(source.load);
    }
    const auto loaded = PreloadTextureResourcesWithColorSpace(requests);
    if (loaded.size() != requests.size())
        return fail("Texture snapshot batch load failed.");
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_))))
        return fail("Failed to allocate texture snapshot descriptors.");
    stride_ = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 formatOptions{};
    const bool canCastTypedFormats =
        SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &formatOptions, sizeof(formatOptions))) &&
        formatOptions.CastingFullyTypedFormatSupported;
    const auto cpu = [&](uint32_t index) {
        return D3D12_CPU_DESCRIPTOR_HANDLE{heap_->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(index) * stride_};
    };
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    for (UINT i = 0; i < heapDesc.NumDescriptors; ++i)
        device->CreateShaderResourceView(nullptr, &view, cpu(i));
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    view.TextureCube.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &view,
                                     cpu(GetNullDescriptorIndex(TextureResourceDimension::TextureCube)));

    using ViewKey = std::tuple<uint32_t, uint32_t, DXGI_FORMAT, TextureResourceDimension>;
    std::map<ViewKey, uint32_t> views;
    constexpr uint32_t nullViewCount = 2;
    for (const auto& source : sources) {
        if (!source.enabled)
            continue;
        const auto key = MakeSourceKey(source);
        if (descriptors_.contains(key))
            continue;
        TextureResourceHandle handle = source.resource;
        if (!source.load.path.empty()) {
            if (source.dimension == TextureResourceDimension::TextureCube)
                handle = LoadCubemapResource(source.load.name, source.load.path, source.load.colorSpace);
            else if (const auto entry = requestIndices.find(key); entry != requestIndices.end())
                handle = loaded[entry->second];
        }
        auto* native = GetTextureResourceNative(handle);
        const auto* record = GetTextureResourceRecord(handle);
        if (!native || !record || GetTextureResourceDimension(handle) != source.dimension ||
            (source.dimension != TextureResourceDimension::Texture2D &&
             source.dimension != TextureResourceDimension::TextureCube))
            return fail("Missing or unsupported texture: " + source.load.path);
        const auto desc = native->GetDesc();
        if (record->desc.lifetime == RenderResourceLifetime::External ||
            record->desc.lifetime == RenderResourceLifetime::FrameTransient ||
            (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0)
            return fail("Texture snapshot requires immutable assets: " + source.load.path);
        auto format = GetTextureResourceFormat(handle);
        if (source.load.colorSpace == TextureResourceColorSpace::Srgb)
            format = DirectX::MakeSRGB(format);
        else if (source.load.colorSpace == TextureResourceColorSpace::Linear)
            format = DirectX::MakeLinear(format);
        if (format != desc.Format && !DirectX::IsTypeless(desc.Format) && !canCastTypedFormats)
            return fail(
                "Device cannot reinterpret this texture color space; reimport it with the requested color space: " +
                source.load.path);
        const ViewKey viewKey{handle.index, handle.generation, format, source.dimension};
        if (const auto found = views.find(viewKey); found != views.end()) {
            descriptors_[key] = found->second;
            continue;
        }
        const uint32_t index = static_cast<uint32_t>(views.size()) + nullViewCount;
        if (index >= heapDesc.NumDescriptors)
            return fail("Texture snapshot exceeds the descriptor budget.");
        view = {};
        view.Format = format;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (source.dimension == TextureResourceDimension::TextureCube) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            view.TextureCube.MipLevels = GetTextureResourceMipCount(handle);
        } else {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = GetTextureResourceMipCount(handle);
        }
        // shader-visible heap を CopyDescriptors のコピー元として使わない。
        device->CreateShaderResourceView(native, &view, cpu(index));
        textures_.emplace_back(native);
        views.emplace(viewKey, index);
        descriptors_.emplace(key, index);
    }
    return true;
}

} // namespace HIKARI::RENDER3D
