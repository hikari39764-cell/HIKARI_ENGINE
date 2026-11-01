#include "RenderCore/Texture/HIKARI_DxTextureInternal.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <DirectXTex.h>
#include <d3dx12.h>
#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
namespace DXTEX {

using namespace DETAIL;

namespace DETAIL {

const char* ColorSpaceSuffix(TextureColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureColorSpace::Linear: return "|linear";
    case TextureColorSpace::Srgb: return "|srgb";
    case TextureColorSpace::Auto:
    default: return "|auto";
    }
}

const char* ColorSpaceName(TextureColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureColorSpace::Linear: return "Linear";
    case TextureColorSpace::Srgb: return "SRGB";
    case TextureColorSpace::Auto:
    default: return "Auto";
    }
}

void LogTextureLoad(const char* kind, const std::string& path, TextureColorSpace colorSpace, DXGI_FORMAT srvFormat,
                    int handle) {
    std::ostringstream oss;
    oss << "[DxTextureManager][TextureLoad] kind=" << (kind ? kind : "Texture2D") << " path=" << path
        << " colorSpace=" << ColorSpaceName(colorSpace) << " srvFormat=" << GFX::FormatToString(srvFormat)
        << " handle=" << handle;
    HIKARI_LOG_INFO(oss.str());
}

std::string NormalizeTextureCachePath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return TEXT::ToLowerAsciiCopy(normalized);
}

std::string MakeTextureCacheKey(const std::string& name, const std::string& path, TextureColorSpace colorSpace) {
    // 同じ論理名でも、実体ファイルが違う場合は別 GPU resource として扱う。
    const bool isCubemap = name.rfind("cube:", 0) == 0;
    const char* dimensionPrefix = isCubemap ? "cube" : "tex";
    const std::string normalizedPath = NormalizeTextureCachePath(path);
    if (!normalizedPath.empty()) {
        return std::string(dimensionPrefix) + "|path:" + normalizedPath + ColorSpaceSuffix(colorSpace);
    }

    return std::string(dimensionPrefix) + "|name:" + TEXT::ToLowerAsciiCopy(name) + ColorSpaceSuffix(colorSpace);
}

bool ContainsAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (needle != nullptr && text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TextureColorSpace ResolveAutoColorSpace(const std::string& name, const std::string& path) {
    const std::string key = TEXT::ToLowerAsciiCopy(name + " " + path);

    // Data textures must stay linear. Sampling these through an SRGB SRV would corrupt values.
    if (ContainsAny(key, {"normal",
                          "_n.",
                          "_n_",
                          "nrm",
                          "roughness",
                          "metallic",
                          "metalness",
                          "metal_rough",
                          "metallicroughness",
                          "occlusion",
                          "ambientocclusion",
                          "ao.",
                          "_ao",
                          "orm",
                          "arm",
                          "height",
                          "displacement",
                          "mask",
                          "opacity",
                          "alpha",
                          "linear"})) {
        return TextureColorSpace::Linear;
    }

    // Most artist-authored color textures, UI textures, albedo/baseColor and emissive maps are SRGB.
    return TextureColorSpace::Srgb;
}

bool IsTgaPath(const std::string& path) {
    const std::string lower = TEXT::ToLowerAsciiCopy(path);
    return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tga";
}

TextureColorSpace ResolveTextureLoadColorSpace(const std::string& name, const std::string& path,
                                               TextureColorSpace colorSpace) {
    if (colorSpace != TextureColorSpace::Auto) {
        return colorSpace;
    }

    return ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path) == CookedAssetFormat::HTEX
               ? TextureColorSpace::Auto
               : ResolveAutoColorSpace(name, path);
}

DXGI_FORMAT ResolveSrvFormat(DXGI_FORMAT resourceFormat, TextureColorSpace colorSpace) {
    if (colorSpace == TextureColorSpace::Srgb) {
        return DirectX::MakeSRGB(resourceFormat);
    }
    if (colorSpace == TextureColorSpace::Linear) {
        return DirectX::MakeLinear(resourceFormat);
    }
    return resourceFormat;
}

int UploadTextureImage(const GFX::Context& context, GFX::UploadSubmissionQueue& uploads,
                       const DirectX::TexMetadata& metadata, const DirectX::Image* images, size_t imageCount,
                       const std::string& name, TextureColorSpace colorSpace, const char* kind) {
    const auto fail = [&](const char* stage) {
        HIKARI_LOG_ERROR(std::string("[DxTextureManager] ") + stage + ": " + name);
        return -1;
    };
    if (!uploads.Initialize(context.device, context.queue, {}))
        return fail("Invalid upload context");
    ComPtr<ID3D12Resource> texture;
    if (FAILED(DirectX::CreateTexture(context.device, metadata, &texture)))
        return fail("CreateTexture failed");
    GFX::VIDEO_MEMORY::Track(texture.Get(), GFX::VIDEO_MEMORY::Category::Texture);
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    if (FAILED(DirectX::PrepareUpload(context.device, images, imageCount, metadata, subresources)) ||
        subresources.empty())
        return fail("PrepareUpload failed");

    const UINT count = static_cast<UINT>(subresources.size());
    const UINT64 bytes = GetRequiredIntermediateSize(texture.Get(), 0, count);
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    ComPtr<ID3D12Resource> staging;
    if (FAILED(context.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging))))
        return fail("Upload allocation failed");
    auto* commands = uploads.Begin();
    if (!commands)
        return fail("Upload command list failed");
    if (!UpdateSubresources(commands, texture.Get(), staging.Get(), 0, 0, count, subresources.data())) {
        uploads.Cancel();
        return fail("Subresource upload failed");
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                              D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    commands->ResourceBarrier(1, &barrier);
    // 公開に失敗した場合も destination と staging を fence 完了まで保持する。
    ID3D12Resource* retained[]{texture.Get(), staging.Get()};
    if (!uploads.Submit(retained, bytes))
        return fail("Upload submission failed");
    const auto format = ResolveSrvFormat(metadata.format, colorSpace);
    const int handle = metadata.IsCubemap() ? DxTextureManager::RegisterCubeFromResourceAs(texture.Get(), format)
                                            : DxTextureManager::RegisterFromResourceAs(texture.Get(), format);
    if (handle >= 0)
        LogTextureLoad(kind, name, colorSpace, format, handle);
    return handle;
}

void RemoveCacheEntriesForHandle(std::unordered_map<std::string, int>& cache, int handle) {
    std::erase_if(cache, [handle](const auto& entry) { return entry.second == handle; });
}

void WriteRgba(uint32_t rgba, uint8_t* outPixel) {
    outPixel[0] = static_cast<uint8_t>((rgba >> 24) & 0xffu);
    outPixel[1] = static_cast<uint8_t>((rgba >> 16) & 0xffu);
    outPixel[2] = static_cast<uint8_t>((rgba >> 8) & 0xffu);
    outPixel[3] = static_cast<uint8_t>(rgba & 0xffu);
}

std::string HexColor(uint32_t rgba) {
    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << rgba;
    return oss.str();
}

bool TryParseGeneratedColor(std::string_view text, uint32_t& color) {
    if (text.size() != 8u)
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), color, 16);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// generated://checker/<colorA>/<colorB> 形式の合成パスを解釈する。
// ディスクを持たない synthetic texture のみ true を返し、色を出力する。
bool TryParseGeneratedCheckerPath(const std::string& path, uint32_t& outColorA, uint32_t& outColorB) {
    constexpr const char* kPrefix = "generated://checker/";
    constexpr size_t kPrefixLen = 20;
    if (path.size() != kPrefixLen + 8u + 1u + 8u || path.compare(0, kPrefixLen, kPrefix) != 0 ||
        path[kPrefixLen + 8u] != '/') {
        return false;
    }
    return TryParseGeneratedColor(std::string_view(path).substr(kPrefixLen, 8u), outColorA) &&
           TryParseGeneratedColor(std::string_view(path).substr(kPrefixLen + 9u, 8u), outColorB);
}
} // namespace DETAIL

bool DxTextureManager::initialized_ = false;
uint64_t DxTextureManager::generation_ = 0;
GFX::Context DxTextureManager::context_{};
UINT DxTextureManager::descriptorSize_ = 0;

ComPtr<ID3D12DescriptorHeap> DxTextureManager::srvHeap_;
GFX::UploadSubmissionQueue DxTextureManager::uploads_;
std::vector<ComPtr<ID3D12Resource>> DxTextureManager::textures_;
std::vector<TextureDimension> DxTextureManager::dimensions_;
std::vector<UINT> DxTextureManager::mipCounts_;
std::vector<DXGI_FORMAT> DxTextureManager::formats_;
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> DxTextureManager::srvCpu_;
std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> DxTextureManager::srvGpu_;
std::vector<bool> DxTextureManager::pendingRelease_;
std::unordered_map<std::string, int> DxTextureManager::nameToHandle_;
GFX::DescriptorAllocator DxTextureManager::descriptorAllocator_;

bool DxTextureManager::Init(const GFX::Context& ctx, int maxTextures) {
    if (initialized_)
        return true;
    context_ = ctx;
    auto* device = context_.device;
    if (!device)
        return false;

    maxTextures = std::clamp(maxTextures, 1, static_cast<int>(GFX::DESCRIPTOR::kUserSrvCount));

    if (context_.srvHeap) {
        srvHeap_ = context_.srvHeap;
    } else {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.NumDescriptors = maxTextures;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!HIKARI_DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_)),
                             "DxTextureManager::Init CreateDescriptorHeap")) {
            return false;
        }
    }

    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!uploads_.Initialize(device, context_.queue, {}))
        return false;

    srvCpu_.resize(maxTextures);
    srvGpu_.resize(maxTextures);
    textures_.resize(maxTextures);
    dimensions_.resize(maxTextures, TextureDimension::Texture2D);
    mipCounts_.resize(maxTextures, 0);
    formats_.resize(maxTextures, DXGI_FORMAT_UNKNOWN);
    pendingRelease_.resize(maxTextures, false);
    descriptorAllocator_.Initialize(GFX::DESCRIPTOR::kUserSrvBegin, static_cast<UINT>(maxTextures));

    for (int i = 0; i < maxTextures; ++i) {
        const UINT descriptorIndex = GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(i);
        srvCpu_[i] = GFX::DESCRIPTOR::CpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
        srvGpu_[i] = GFX::DESCRIPTOR::GpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
    }

    ++generation_;
    initialized_ = true;
    return true;
}

void DxTextureManager::UpdateContext(const GFX::Context& ctx) {
    context_ = ctx;
    uploads_.Collect();
}

void DxTextureManager::Finalize() {
    if (!initialized_) {
        return;
    }

    uploads_.Reset();
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
    nameToHandle_.clear();
    descriptorAllocator_.Reset();
    initialized_ = false;
}

ID3D12GraphicsCommandList* DxTextureManager::BeginUpload() {
    if (!uploads_.Initialize(context_.device, context_.queue, {}))
        return nullptr;
    return uploads_.Begin();
}

void DxTextureManager::EnsureInit() {
    if (!initialized_) {
        Init(context_);
    }
}

int DxTextureManager::LoadTextureWithColorSpace(const std::string& name, const std::string& path,
                                                TextureColorSpace colorSpace) {
    EnsureInit();
    const TextureColorSpace resolvedColorSpace = ResolveTextureLoadColorSpace(name, path, colorSpace);
    const std::string cacheKey = MakeTextureCacheKey(name, path, resolvedColorSpace);
    auto it = nameToHandle_.find(cacheKey);
    if (it != nameToHandle_.end()) {
        return it->second;
    }

    // ディスクを持たない generated:// パスは合成生成へディスパッチする。
    uint32_t genColorA = 0;
    uint32_t genColorB = 0;
    constexpr std::string_view solidPrefix = "generated://solid/";
    if (path.starts_with(solidPrefix) &&
        TryParseGeneratedColor(std::string_view(path).substr(solidPrefix.size()), genColorA)) {
        const int genHandle = CreateSolidColorTexture(name, genColorA, resolvedColorSpace);
        if (genHandle >= 0)
            nameToHandle_[cacheKey] = genHandle;
        return genHandle;
    }
    if (TryParseGeneratedCheckerPath(path, genColorA, genColorB)) {
        const int genHandle = CreateCheckerTexture(name, genColorA, genColorB, resolvedColorSpace);
        if (genHandle >= 0) {
            nameToHandle_[cacheKey] = genHandle;
        }
        return genHandle;
    }

    int handle = CreateTextureFromFile(path, resolvedColorSpace);
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

int DxTextureManager::FindLoadedTextureWithColorSpace(const std::string& name, const std::string& path,
                                                      TextureColorSpace colorSpace) {
    EnsureInit();
    const TextureColorSpace resolvedColorSpace = ResolveTextureLoadColorSpace(name, path, colorSpace);
    const std::string cacheKey = MakeTextureCacheKey(name, path, resolvedColorSpace);
    const auto found = nameToHandle_.find(cacheKey);
    if (found == nameToHandle_.end() || !IsTextureHandleValid(found->second)) {
        return -1;
    }
    return found->second;
}

int DxTextureManager::LoadTextureSrgb(const std::string& name, const std::string& path) {
    return LoadTextureWithColorSpace(name, path, TextureColorSpace::Srgb);
}

int DxTextureManager::LoadTextureLinear(const std::string& name, const std::string& path) {
    return LoadTextureWithColorSpace(name, path, TextureColorSpace::Linear);
}

int DxTextureManager::LoadCubemap(const std::string& name, const std::string& path, TextureColorSpace colorSpace) {
    EnsureInit();
    const std::string cacheKey = MakeTextureCacheKey("cube:" + name, path, colorSpace);
    auto it = nameToHandle_.find(cacheKey);
    if (it != nameToHandle_.end()) {
        return it->second;
    }

    int handle = CreateTextureFromFile(path, colorSpace, true);
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

int DxTextureManager::CreateSolidColorTexture(const std::string& name, uint32_t rgba, TextureColorSpace colorSpace) {
    EnsureInit();

    const std::string syntheticPath = "generated://solid/" + HexColor(rgba);
    const std::string cacheKey = MakeTextureCacheKey(name, syntheticPath, colorSpace);
    auto it = nameToHandle_.find(cacheKey);
    if (it != nameToHandle_.end()) {
        return it->second;
    }

    std::array<uint8_t, 4> pixel{};
    WriteRgba(rgba, pixel.data());
    const int handle = CreateTextureFromRgbaPixels(syntheticPath, 1, 1, pixel.data(), colorSpace, "GeneratedSolid");
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

int DxTextureManager::CreateSolidColorCubemap(const std::string& name, uint32_t rgba, TextureColorSpace colorSpace) {
    EnsureInit();

    const std::string syntheticPath = "generated://solid-cube/" + HexColor(rgba);
    const std::string cacheKey = MakeTextureCacheKey("cube:" + name, syntheticPath, colorSpace);
    auto it = nameToHandle_.find(cacheKey);
    if (it != nameToHandle_.end()) {
        return it->second;
    }

    const int handle = CreateCubemapFromRgbaPixels(syntheticPath, rgba, colorSpace, "GeneratedSolidCube");
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

int DxTextureManager::CreateCheckerTexture(const std::string& name, uint32_t colorA, uint32_t colorB,
                                           TextureColorSpace colorSpace) {
    EnsureInit();

    const std::string syntheticPath = "generated://checker/" + HexColor(colorA) + "/" + HexColor(colorB);
    const std::string cacheKey = MakeTextureCacheKey(name, syntheticPath, colorSpace);
    auto it = nameToHandle_.find(cacheKey);
    if (it != nameToHandle_.end()) {
        return it->second;
    }

    // 8x8 では 1 マス 4 テクセルしかなく縮小表示でぼやけるため、
    // 128x128（1 マス 64 テクセル）にして解像感を確保する。
    constexpr uint32_t kSize = 128;
    constexpr uint32_t kCellPixels = kSize / 2u;
    std::vector<uint8_t> pixels(kSize * kSize * 4);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const bool useA = ((x / kCellPixels) + (y / kCellPixels)) % 2u == 0u;
            WriteRgba(useA ? colorA : colorB, pixels.data() + ((y * kSize + x) * 4u));
        }
    }

    const int handle =
        CreateTextureFromRgbaPixels(syntheticPath, kSize, kSize, pixels.data(), colorSpace, "GeneratedChecker");
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

int DxTextureManager::CreateRgbaTexture(const std::string& name, uint32_t width, uint32_t height,
                                        const std::vector<uint8_t>& rgbaPixels, TextureColorSpace colorSpace) {
    EnsureInit();
    const size_t requiredByteCount = static_cast<size_t>(width) * height * 4u;
    if (width == 0u || height == 0u || rgbaPixels.size() != requiredByteCount) {
        HIKARI_LOG_ERROR("[DxTextureManager][Generated][ERROR] invalid RGBA texture payload: " + name);
        return -1;
    }

    const std::string syntheticPath =
        "generated://rgba/" + std::to_string(width) + "x" + std::to_string(height) + "/" + name;
    const std::string cacheKey = MakeTextureCacheKey(name, syntheticPath, colorSpace);
    const auto cached = nameToHandle_.find(cacheKey);
    if (cached != nameToHandle_.end()) {
        return cached->second;
    }

    const int handle =
        CreateTextureFromRgbaPixels(syntheticPath, width, height, rgbaPixels.data(), colorSpace, "GeneratedRgba");
    if (handle >= 0) {
        nameToHandle_[cacheKey] = handle;
    }
    return handle;
}

void DxTextureManager::InvalidateTextureCacheByPath(const std::string& path) {
    const std::string normalized = NormalizeTextureCachePath(path);
    if (normalized.empty())
        return;
    const std::string needle = "|path:" + normalized + "|";
    std::erase_if(nameToHandle_, [&](const auto& entry) { return entry.first.find(needle) != std::string::npos; });
}

int DxTextureManager::CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace, bool requireCube) {
    const auto format = ASSETS::SEMANTICS::ClassifyCookedAssetFormat(path);
    if (format == CookedAssetFormat::HTEX && !requireCube) {
        const auto loaded = LoadTexturesWithColorSpaceBatch({TextureLoadRequest{"", path, colorSpace}});
        return loaded.empty() ? -1 : loaded.front().handle;
    }
    const auto nativePath = TEXT::PathFromUtf8(path);
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage image;
    HRESULT hr;
    const char* kind;
    if (format == CookedAssetFormat::DDS || requireCube) {
        kind = "DDS";
        hr = DirectX::LoadFromDDSFile(nativePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
    } else if (IsTgaPath(path)) {
        kind = "TGA";
        hr = DirectX::LoadFromTGAFile(nativePath.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, image);
    } else {
        kind = "WIC";
        // 元の WIC 経路と同じく mip 0 の RGBA を読む。色空間は呼出側の指定を使う。
        hr = DirectX::LoadFromWICFile(
            nativePath.c_str(),
            static_cast<DirectX::WIC_FLAGS>(DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB), &metadata,
            image);
        if (SUCCEEDED(hr) && metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            DirectX::ScratchImage converted;
            hr = DirectX::Convert(*image.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                  DirectX::TEX_THRESHOLD_DEFAULT, converted);
            if (SUCCEEDED(hr)) {
                image = std::move(converted);
                metadata = image.GetMetadata();
            }
        }
    }
    if (FAILED(hr) || (requireCube && !metadata.IsCubemap())) {
        std::ostringstream message;
        message << "[DxTextureManager][" << kind << "] Decode or texture dimension failed. path=" << path << " hr=0x"
                << std::hex << static_cast<unsigned long>(hr);
        HIKARI_LOG_ERROR(message.str());
        return -1;
    }
    return UploadTextureImage(context_, uploads_, metadata, image.GetImages(), image.GetImageCount(), path, colorSpace,
                              kind);
}

int DxTextureManager::CreateTextureFromRgbaPixels(const std::string& debugName, uint32_t width, uint32_t height,
                                                  const uint8_t* rgbaPixels, TextureColorSpace colorSpace,
                                                  const char* logKind) {
    if (width == 0 || height == 0 || !rgbaPixels)
        return -1;
    DirectX::TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = metadata.arraySize = metadata.mipLevels = 1;
    metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    metadata.format = ResolveSrvFormat(DXGI_FORMAT_R8G8B8A8_UNORM, colorSpace);
    DirectX::Image source{width,
                          height,
                          metadata.format,
                          size_t(width) * 4,
                          size_t(width) * height * 4,
                          const_cast<uint8_t*>(rgbaPixels)};
    DirectX::ScratchImage mipChain;
    // sRGB の縮小は linear 空間で行う。生成できない場合は有効な mip 0 のみを公開する。
    const auto filter =
        colorSpace == TextureColorSpace::Srgb
            ? static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB)
            : DirectX::TEX_FILTER_DEFAULT;
    if ((width > 1 || height > 1) && SUCCEEDED(DirectX::GenerateMipMaps(source, filter, 0, mipChain))) {
        return UploadTextureImage(context_, uploads_, mipChain.GetMetadata(), mipChain.GetImages(),
                                  mipChain.GetImageCount(), debugName, colorSpace, logKind);
    }
    return UploadTextureImage(context_, uploads_, metadata, &source, 1, debugName, colorSpace, logKind);
}

int DxTextureManager::CreateCubemapFromRgbaPixels(const std::string& debugName, uint32_t rgba,
                                                  TextureColorSpace colorSpace, const char* logKind) {
    DirectX::TexMetadata metadata{};
    metadata.width = metadata.height = metadata.depth = metadata.mipLevels = 1;
    metadata.arraySize = 6;
    metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    metadata.miscFlags = DirectX::TEX_MISC_TEXTURECUBE;
    metadata.format = ResolveSrvFormat(DXGI_FORMAT_R8G8B8A8_UNORM, colorSpace);
    std::array<uint8_t, 4> pixel{};
    WriteRgba(rgba, pixel.data());
    std::array<DirectX::Image, 6> images;
    images.fill({1, 1, metadata.format, 4, 4, pixel.data()});
    return UploadTextureImage(context_, uploads_, metadata, images.data(), images.size(), debugName, colorSpace,
                              logKind);
}

int DxTextureManager::RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat) {
    return RegisterResource(resource, srvFormat, TextureDimension::Texture2D);
}

int DxTextureManager::RegisterCubeFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat) {
    return RegisterResource(resource, srvFormat, TextureDimension::TextureCube);
}

int DxTextureManager::RegisterResource(ID3D12Resource* resource, DXGI_FORMAT format, TextureDimension dimension) {
    EnsureInit();
    if (!initialized_ || !resource || !context_.device)
        return -1;
    const auto desc = resource->GetDesc();
    if (dimension == TextureDimension::TextureCube && desc.DepthOrArraySize < 6)
        return -1;
    const auto slot = descriptorAllocator_.Allocate();
    if (!slot.IsValid()) {
        HIKARI_LOG_ERROR("DxTextureManager: out of texture descriptor slots.");
        return -1;
    }
    const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
    if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
        descriptorAllocator_.Free(slot);
        return -1;
    }
    textures_[handle] = resource;
    dimensions_[handle] = dimension;
    mipCounts_[handle] = std::max<UINT>(1, desc.MipLevels);
    formats_[handle] = format;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (dimension == TextureDimension::TextureCube) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = mipCounts_[handle];
    } else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = mipCounts_[handle];
    }
    context_.device->CreateShaderResourceView(resource, &srv, srvCpu_[handle]);
    return handle;
}

void DxTextureManager::ReleaseTexture(int handle) {
    if (!initialized_ || handle < 0 || handle >= static_cast<int>(textures_.size())) {
        return;
    }

    if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
        return;
    }

    const GFX::DescriptorSlot slot{GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)};

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

void DxTextureManager::ReleaseTextureDeferred(int handle) {
    if (!initialized_ || handle < 0 || handle >= static_cast<int>(textures_.size())) {
        return;
    }

    if (handle >= static_cast<int>(pendingRelease_.size())) {
        return;
    }

    if (pendingRelease_[handle]) {
        return;
    }

    const GFX::DescriptorSlot slot{GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)};

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
        HIKARI_LOG_ERROR(
            "[DxTextureManager][DeferredRelease][WARN] deferredReleaseQueue is null. Falling back to immediate descriptor free.");
        pendingRelease_[handle] = false;
        descriptorAllocator_.Free(slot);
        return;
    }

    const uint64_t retireFenceValue = context_.currentFrameRetireFenceValue;
    const uint64_t generation = generation_;
    queue->Enqueue(
        retireFenceValue,
        [resourceToRelease, slot, handle, generation]() mutable {
            (void)resourceToRelease;
            if (generation == generation_)
                DxTextureManager::CompleteDeferredRelease(handle, slot);
        },
        "DxTextureManager::ReleaseTextureDeferred");
}

void DxTextureManager::CompleteDeferredRelease(int handle, GFX::DescriptorSlot slot) {
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

UINT DxTextureManager::GetUsedDescriptorCount() {
    if (!initialized_) {
        return 0;
    }

    return descriptorAllocator_.GetUsedCount();
}

UINT DxTextureManager::GetFreeDescriptorCount() {
    if (!initialized_) {
        return 0;
    }

    return descriptorAllocator_.GetFreeCount();
}

UINT DxTextureManager::GetMaxDescriptorCount() {
    if (!initialized_) {
        return 0;
    }

    return descriptorAllocator_.GetCount();
}

bool DxTextureManager::IsTextureHandleValid(int handle) {
    if (!initialized_) {
        return false;
    }

    if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
        return false;
    }

    if (handle < static_cast<int>(pendingRelease_.size()) && pendingRelease_[handle]) {
        return false;
    }

    const GFX::DescriptorSlot slot{GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)};

    if (!descriptorAllocator_.IsAllocated(slot)) {
        return false;
    }

    return textures_[handle] != nullptr;
}

ID3D12Resource* DxTextureManager::GetResource(int handle) {
    if (!IsTextureHandleValid(handle)) {
        return nullptr;
    }

    return textures_[handle].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxTextureManager::GetSrvCpuHandle(int handle) {
    D3D12_CPU_DESCRIPTOR_HANDLE nullHandle{};
    nullHandle.ptr = 0;

    if (!IsTextureHandleValid(handle) || handle >= static_cast<int>(srvCpu_.size())) {
        return nullHandle;
    }

    return srvCpu_[handle];
}

D3D12_GPU_DESCRIPTOR_HANDLE DxTextureManager::GetSrvGpuHandle(int handle) {
    D3D12_GPU_DESCRIPTOR_HANDLE nullHandle{};
    nullHandle.ptr = 0;

    if (!IsTextureHandleValid(handle) || handle >= static_cast<int>(srvGpu_.size())) {
        return nullHandle;
    }

    return srvGpu_[handle];
}

UINT DxTextureManager::GetSrvDescriptorIndex(int handle) {
    if (!IsTextureHandleValid(handle)) {
        return kInvalidSrvDescriptorIndex;
    }

    return GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle);
}

ID3D12DescriptorHeap* DxTextureManager::GetSrvHeap() { return srvHeap_.Get(); }

void DxTextureManager::GetTextureSize(int handle, UINT& outWidth, UINT& outHeight) {
    outWidth = 0;
    outHeight = 0;
    if (!initialized_) {
        return;
    }
    if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
        return;
    }
    if (!textures_[handle]) {
        return;
    }

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
