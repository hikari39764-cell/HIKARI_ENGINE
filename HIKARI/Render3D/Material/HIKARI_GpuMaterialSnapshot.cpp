#include "Render3D/Material/HIKARI_GpuMaterialSnapshot.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <DirectXTex.h>
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Material/Program/HIKARI_GpuMaterialProgramBinding.h"

namespace HIKARI::RENDER3D::MATERIAL {

    GpuMaterialSnapshot::~GpuMaterialSnapshot() { Reset(); }

    void GpuMaterialSnapshot::Reset() {
        const auto context = GFX::GetCurrentContext();
        upload_.Finalize(&context);
        GFX::RetireD3D12ObjectForFrame(textureHeap_, context, "MaterialSnapshot.Descriptors");
        for (auto& texture : textures_)
            GFX::RetireD3D12ObjectForFrame(texture, context, "MaterialSnapshot.Texture");
        textures_.clear();
        materialAddress_ = parameterAddress_ = resourceAddress_ = 0;
    }

    bool GpuMaterialSnapshot::Initialize(ID3D12Device* device,
        std::span<const MaterialSnapshot> materials, std::string& error) {
        Reset();
        error.clear();
        const auto fail = [&](std::string message) {
            error = std::move(message);
            Reset();
            return false;
        };
        if (!device || materials.empty() || materials.size() > kDefaultGpuMaterialCapacity)
            return fail("Invalid material snapshot size or device.");

        // 全材質の要求を一括ロードする。同じ path / color space は一度だけ準備する。
        using LoadKey = std::pair<std::string, TextureResourceColorSpace>;
        std::map<LoadKey, size_t> requestIndices;
        std::vector<TextureResourceLoadRequest> requests;
        const auto collect = [&](const MaterialTextureSnapshotSource& source) {
            if (!source.enabled || source.load.path.empty()) return;
            const auto [entry, inserted] = requestIndices.try_emplace(
                LoadKey{source.load.path, source.load.colorSpace}, requests.size());
            if (inserted) requests.push_back(source.load);
        };
        for (const auto& material : materials) {
            if (!material.error.empty()) return fail(material.error);
            if (material.programParameters.size() > kGpuMaterialProgramParameterSlotCount ||
                material.programTextures.size() > kGpuMaterialProgramResourceSlotCount)
                return fail("Material program exceeds the shared parameter or resource budget.");
            for (const auto& texture : material.textures) collect(texture);
            for (const auto& texture : material.programTextures) collect(texture);
        }
        const auto loaded = PreloadTextureResourcesWithColorSpace(requests);
        if (loaded.size() != requests.size()) return fail("Material texture batch load failed.");

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = GFX::DESCRIPTOR::kUserSrvCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&textureHeap_))))
            return fail("Failed to allocate material snapshot descriptors.");
        const UINT stride = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 formatOptions{};
        const bool canCastTypedFormats = SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS3, &formatOptions, sizeof(formatOptions))) &&
            formatOptions.CastingFullyTypedFormatSupported;
        const auto heapStart = textureHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        for (UINT i = 0; i < heapDesc.NumDescriptors; ++i)
            device->CreateShaderResourceView(nullptr, &view, {heapStart.ptr + SIZE_T(i) * stride});

        using ViewKey = std::tuple<uint32_t, uint32_t, DXGI_FORMAT>;
        std::map<ViewKey, uint32_t> descriptors;
        const auto resolve = [&](const MaterialTextureSnapshotSource& source, uint32_t& index) -> bool {
            index = kInvalidTextureDescriptorIndex;
            if (!source.enabled) return true;
            TextureResourceHandle handle = source.resource;
            if (!source.load.path.empty()) {
                const auto entry = requestIndices.find({source.load.path, source.load.colorSpace});
                handle = loaded[entry->second];
            }
            auto* native = GetTextureResourceNative(handle);
            const auto* record = GetTextureResourceRecord(handle);
            if (!native || !record || GetTextureResourceDimension(handle) != TextureResourceDimension::Texture2D) {
                error = "Missing or unsupported material texture: " + source.load.path;
                return false;
            }
            const auto desc = native->GetDesc();
            if (record->desc.lifetime == RenderResourceLifetime::External ||
                record->desc.lifetime == RenderResourceLifetime::FrameTransient ||
                (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0) {
                error = "Static material snapshot requires immutable texture assets: " + source.load.path;
                return false;
            }
            auto format = GetTextureResourceFormat(handle);
            if (source.load.colorSpace == TextureResourceColorSpace::Srgb) format = DirectX::MakeSRGB(format);
            else if (source.load.colorSpace == TextureResourceColorSpace::Linear) format = DirectX::MakeLinear(format);
            if (format != desc.Format && !DirectX::IsTypeless(desc.Format) && !canCastTypedFormats) {
                error = "Device cannot reinterpret this texture color space; reimport it with the material channel's color space: " + source.load.path;
                return false;
            }
            const ViewKey key{handle.index, handle.generation, format};
            if (const auto found = descriptors.find(key); found != descriptors.end()) {
                index = found->second;
                return true;
            }
            if (descriptors.size() >= heapDesc.NumDescriptors) {
                error = "Material snapshot exceeds the texture descriptor budget.";
                return false;
            }
            index = static_cast<uint32_t>(descriptors.size());
            view.Format = format;
            view.Texture2D.MipLevels = GetTextureResourceMipCount(handle);
            // Shader-visible heap を CopyDescriptors のコピー元として使わない。
            device->CreateShaderResourceView(native, &view, {heapStart.ptr + SIZE_T(index) * stride});
            textures_.emplace_back(native);
            descriptors.emplace(key, index);
            return true;
        };

        std::vector<GpuMaterialData> table;
        std::vector<MATH::Vec4> parameters;
        std::vector<uint32_t> resources;
        for (const auto& material : materials) {
            auto projection = material.projection;
            for (size_t i = 0; i < material.textures.size(); ++i) {
                auto& texture = projection.textures[i];
                texture.backendHandle = -1;
                texture.enabled = material.textures[i].enabled;
                if (!resolve(material.textures[i], texture.descriptorIndex)) return fail(error);
            }
            auto data = PackGpuMaterialData(projection);
            if (material.hasProgram) {
                auto binding = material.programBinding;
                binding.parameterBlockOffset = static_cast<uint32_t>(parameters.size());
                binding.resourceTableOffset = static_cast<uint32_t>(resources.size());
                binding.parameterBlockCount = static_cast<uint32_t>(material.programParameters.size());
                ApplyGpuMaterialProgramBinding(data, binding);
                parameters.insert(parameters.end(), material.programParameters.begin(), material.programParameters.end());
                // HLSL の resource slot 読み出しは固定長32。隣の材質へ読み越さない。
                resources.resize(resources.size() + kGpuMaterialProgramResourceSlotCount, kInvalidTextureDescriptorIndex);
                for (size_t i = 0; i < material.programTextures.size(); ++i)
                    if (!resolve(material.programTextures[i], resources[binding.resourceTableOffset + i])) return fail(error);
            }
            table.push_back(data);
        }
        if (parameters.empty()) parameters.emplace_back();
        if (resources.empty()) resources.push_back(kInvalidTextureDescriptorIndex);
        const size_t totalBytes = table.size() * sizeof(GpuMaterialData) +
            parameters.size() * sizeof(MATH::Vec4) + resources.size() * sizeof(uint32_t) + 3u * 256u;
        if (!upload_.Init(device, totalBytes)) return fail("Failed to allocate material snapshot upload.");
        const auto upload = [&](const auto& values, D3D12_GPU_VIRTUAL_ADDRESS& address) {
            const size_t bytes = values.size() * sizeof(values[0]);
            void* destination = upload_.Allocate(bytes, address);
            if (!destination) return false;
            std::memcpy(destination, values.data(), bytes);
            return true;
        };
        if (!upload(table, materialAddress_) || !upload(parameters, parameterAddress_) || !upload(resources, resourceAddress_))
            return fail("Failed to upload material snapshot.");
        return true;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
