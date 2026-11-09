#include "HIKARI_TerrainKitSurfaces.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <execution>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include <json.hpp>

#include <windows.h>

#include <DirectXTex.h>
#include <combaseapi.h>

#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

constexpr char kSurfaceExtension[] = ".hterrainsurface";
constexpr uint32_t kCookMagic = 0x43535448u; // "HTSC"
constexpr uint32_t kCookVersion = 4u;

struct CookHeader {
    uint32_t magic = kCookMagic;
    uint32_t version = kCookVersion;
    uint32_t size = kSurfaceTextureSize;
    uint32_t mips = kSurfaceTextureMips;
    uint64_t colorBytes = 0u;
    uint64_t detailBytes = 0u;
    float average[3]{};
    float reserved = 0.0f;
};

std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

uint64_t Fnv1a(const void* data, size_t bytes, uint64_t hash = 0xcbf29ce484222325ull) {
    const auto* cursor = static_cast<const uint8_t*>(data);
    for (size_t index = 0u; index < bytes; ++index) {
        hash = (hash ^ cursor[index]) * 0x100000001b3ull;
    }
    return hash;
}

std::string HashOfAsset(const std::filesystem::path& file, const TerrainSurfaceAsset& asset) {
    nlohmann::json json{};
    JSONFIELDS::Write(json, asset, kTerrainSurfaceAssetFields);
    const std::string text = json.dump();
    uint64_t hash = Fnv1a(text.data(), text.size());
    hash = Fnv1a(&kCookVersion, sizeof(kCookVersion), hash);
    for (const std::string* part : {&asset.color, &asset.normal, &asset.height, &asset.roughness, &asset.ao}) {
        if (part->empty()) {
            continue;
        }
        std::error_code ec{};
        const uint64_t size = std::filesystem::file_size(file.parent_path() / Utf8Path(*part), ec);
        hash = Fnv1a(&size, sizeof(size), hash);
    }
    char text16[17]{};
    std::snprintf(text16, sizeof(text16), "%016llx", static_cast<unsigned long long>(hash));
    return text16;
}

// 図を読み、RGBA8 の決まった大きさへ揃える。
bool LoadImageRgba8(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error) {
    DirectX::ScratchImage source{};
    DirectX::TexMetadata metadata{};
    // sRGB の印は無視して生の値を扱う（色の変換は焼く所で決める）。
    if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, source))) {
        error = "Could not read " + path.generic_string();
        return false;
    }
    const DirectX::Image* image = source.GetImage(0u, 0u, 0u);
    DirectX::ScratchImage converted{};
    if (metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                    DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
            error = "Could not convert " + path.generic_string();
            return false;
        }
        image = converted.GetImage(0u, 0u, 0u);
    }
    DirectX::ScratchImage resized{};
    if (image->width != kSurfaceTextureSize || image->height != kSurfaceTextureSize) {
        if (FAILED(DirectX::Resize(*image, kSurfaceTextureSize, kSurfaceTextureSize, DirectX::TEX_FILTER_CUBIC,
                                   resized))) {
            error = "Could not resize " + path.generic_string();
            return false;
        }
        image = resized.GetImage(0u, 0u, 0u);
    }
    out.resize(static_cast<size_t>(kSurfaceTextureSize) * kSurfaceTextureSize * 4u);
    for (uint32_t row = 0u; row < kSurfaceTextureSize; ++row) {
        std::memcpy(out.data() + static_cast<size_t>(row) * kSurfaceTextureSize * 4u,
                    image->pixels + row * image->rowPitch, kSurfaceTextureSize * 4u);
    }
    return true;
}

float SrgbToLinear(float value) noexcept {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float value) noexcept {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

uint8_t ToByte(float value) noexcept { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); }

// RGBA8 の全段を作って BC7 へ。段ごとに並列で圧縮する。
bool CompressWithMips(std::vector<uint8_t>& rgba, DXGI_FORMAT format, bool srgb, std::vector<uint8_t>& out,
                      std::string& error) {
    DirectX::Image base{};
    base.width = kSurfaceTextureSize;
    base.height = kSurfaceTextureSize;
    // 中身が sRGB なら入力もそう名乗る。UNORM のままだと BC7_UNORM_SRGB への圧縮が線形と見てもう一度符号化する。
    base.format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    base.rowPitch = static_cast<size_t>(kSurfaceTextureSize) * 4u;
    base.slicePitch = base.rowPitch * kSurfaceTextureSize;
    base.pixels = rgba.data();
    DirectX::ScratchImage mips{};
    if (FAILED(DirectX::GenerateMipMaps(base, srgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_LINEAR,
                                        kSurfaceTextureMips, mips))) {
        error = "Could not generate surface mips.";
        return false;
    }
    // BC の塊は行の順に並ぶので、横長の帯に切って別々に圧縮し、つなげれば一枚と同じになる。
    constexpr uint32_t kStripRows = 64u;
    struct Strip {
        DirectX::Image image{};
        DirectX::ScratchImage compressed{};
    };
    std::vector<Strip> strips{};
    for (uint32_t level = 0u; level < kSurfaceTextureMips; ++level) {
        const DirectX::Image* image = mips.GetImage(level, 0u, 0u);
        for (size_t row = 0u; row < image->height; row += kStripRows) {
            Strip strip{};
            strip.image = *image;
            strip.image.height = (std::min)(image->height - row, static_cast<size_t>(kStripRows));
            strip.image.slicePitch = image->rowPitch * strip.image.height;
            strip.image.pixels = image->pixels + row * image->rowPitch;
            strips.push_back(std::move(strip));
        }
    }
    std::atomic<bool> failed{false};
    std::for_each(std::execution::par, strips.begin(), strips.end(), [&](Strip& strip) {
        if (FAILED(DirectX::Compress(strip.image, format, DirectX::TEX_COMPRESS_BC7_QUICK,
                                     DirectX::TEX_THRESHOLD_DEFAULT, strip.compressed))) {
            failed = true;
        }
    });
    if (failed) {
        error = "Could not compress a surface texture.";
        return false;
    }
    out.clear();
    for (const Strip& strip : strips) {
        const DirectX::Image* image = strip.compressed.GetImage(0u, 0u, 0u);
        out.insert(out.end(), image->pixels, image->pixels + image->slicePitch);
    }
    return true;
}

bool CookSurface(const std::filesystem::path& file, const TerrainSurfaceAsset& asset, CookedSurface& out,
                 std::string& error) {
    const std::filesystem::path directory = file.parent_path();
    const size_t pixels = static_cast<size_t>(kSurfaceTextureSize) * kSurfaceTextureSize;
    const auto load = [&](const std::string& name, std::vector<uint8_t>& target) {
        return name.empty() || LoadImageRgba8(directory / Utf8Path(name), target, error);
    };
    std::vector<uint8_t> color{};
    std::vector<uint8_t> normal{};
    std::vector<uint8_t> height{};
    std::vector<uint8_t> roughness{};
    std::vector<uint8_t> ao{};
    if (!load(asset.color, color) || !load(asset.normal, normal) || !load(asset.height, height) ||
        !load(asset.roughness, roughness) || !load(asset.ao, ao)) {
        return false;
    }
    // 明るさの大きな流れを引いて、図の明るさから高さを作る（高さの図が無い材質の高さ混合に使う）。
    std::vector<float> luminance{};
    float lumMin = 1.0f;
    float lumMax = 0.0f;
    if (height.empty() && !color.empty()) {
        luminance.resize(pixels);
        for (size_t index = 0u; index < pixels; ++index) {
            const uint8_t* rgb = color.data() + index * 4u;
            luminance[index] = (rgb[0] * 0.3f + rgb[1] * 0.59f + rgb[2] * 0.11f) / 255.0f;
            lumMin = (std::min)(lumMin, luminance[index]);
            lumMax = (std::max)(lumMax, luminance[index]);
        }
    }
    std::vector<uint8_t> colorHeight(pixels * 4u);
    std::vector<uint8_t> detail(pixels * 4u);
    std::array<double, 3> sum{};
    for (size_t index = 0u; index < pixels; ++index) {
        uint8_t* outColor = colorHeight.data() + index * 4u;
        uint8_t* outDetail = detail.data() + index * 4u;
        for (int channel = 0; channel < 3; ++channel) {
            sum[channel] += color.empty() ? 1.0f : SrgbToLinear(color[index * 4u + channel] / 255.0f);
            outColor[channel] = color.empty() ? 255u : color[index * 4u + channel];
        }
        if (!height.empty()) {
            outColor[3] = height[index * 4u];
        } else if (!luminance.empty()) {
            outColor[3] = ToByte((luminance[index] - lumMin) / (std::max)(lumMax - lumMin, 1.0e-3f));
        } else {
            outColor[3] = 128u;
        }
        outDetail[0] = normal.empty() ? 128u : normal[index * 4u];
        outDetail[1] = normal.empty() ? 128u : normal[index * 4u + 1u];
        outDetail[2] = roughness.empty() ? ToByte(asset.roughnessValue)
                                         : ToByte(roughness[index * 4u] / 255.0f * asset.roughnessValue);
        outDetail[3] = ao.empty() ? 255u : ao[index * 4u];
    }
    for (int channel = 0; channel < 3; ++channel) {
        out.averageColor[channel] = static_cast<float>(sum[channel] / static_cast<double>(pixels));
    }
    return CompressWithMips(colorHeight, DXGI_FORMAT_BC7_UNORM_SRGB, true, out.colorHeight, error) &&
           CompressWithMips(detail, DXGI_FORMAT_BC7_UNORM, false, out.detail, error);
}

bool ReadCooked(const std::filesystem::path& path, CookedSurface& out) {
    std::ifstream file(path, std::ios::binary);
    CookHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.magic != kCookMagic ||
        header.version != kCookVersion || header.size != kSurfaceTextureSize || header.mips != kSurfaceTextureMips) {
        return false;
    }
    out.colorHeight.resize(static_cast<size_t>(header.colorBytes));
    out.detail.resize(static_cast<size_t>(header.detailBytes));
    out.averageColor = {header.average[0], header.average[1], header.average[2]};
    const auto read = [&](void* target, size_t bytes) {
        return static_cast<bool>(file.read(static_cast<char*>(target), static_cast<std::streamsize>(bytes)));
    };
    return read(out.colorHeight.data(), out.colorHeight.size()) && read(out.detail.data(), out.detail.size());
}

void WriteCooked(const std::filesystem::path& path, const CookedSurface& surface) {
    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CookHeader header{};
    header.colorBytes = surface.colorHeight.size();
    header.detailBytes = surface.detail.size();
    header.average[0] = surface.averageColor[0];
    header.average[1] = surface.averageColor[1];
    header.average[2] = surface.averageColor[2];
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(surface.colorHeight.data()),
               static_cast<std::streamsize>(surface.colorHeight.size()));
    file.write(reinterpret_cast<const char*>(surface.detail.data()),
               static_cast<std::streamsize>(surface.detail.size()));
}

} // namespace

TERRAIN::DATA::SurfaceMapStyle MakeSurfaceMapStyle(const CookedSurface& surface) {
    const TerrainSurfaceAsset& asset = surface.asset;
    TERRAIN::DATA::SurfaceMapStyle style{};
    style.bandMeters = asset.mapBandMeters;
    style.bandAmount = asset.mapBandAmount;
    const auto linear = [](const MATH::Vec3& color) {
        return std::array<float, 3>{SrgbToLinear(color.x), SrgbToLinear(color.y), SrgbToLinear(color.z)};
    };
    const bool hasPalette = asset.mapColorA.x > 0.0f || asset.mapColorA.y > 0.0f || asset.mapColorA.z > 0.0f;
    style.colors = hasPalette ? std::array<std::array<float, 3>, 3>{linear(asset.mapColorA), linear(asset.mapColorB),
                                                                    linear(asset.mapColorC)}
                              : std::array<std::array<float, 3>, 3>{surface.averageColor, surface.averageColor,
                                                                    surface.averageColor};
    return style;
}

bool ReadTerrainSurfaceAsset(const std::filesystem::path& file, TerrainSurfaceAsset& out, std::string& error) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = "Could not open " + file.generic_string();
        return false;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(stream);
        TerrainSurfaceAsset asset{};
        JSONFIELDS::Read(json, asset, kTerrainSurfaceAssetFields);
        if (asset.name.empty()) {
            asset.name = file.stem().string();
        }
        out = std::move(asset);
        return true;
    } catch (const std::exception& exception) {
        error = file.generic_string() + ": " + exception.what();
        return false;
    }
}

std::vector<SurfaceCatalogEntry> ListTerrainSurfaces(const std::filesystem::path& projectRoot) {
    std::vector<SurfaceCatalogEntry> entries{};
    const auto scan = [&](const std::filesystem::path& directory, bool builtin) {
        std::error_code ec{};
        for (auto it = std::filesystem::recursive_directory_iterator(directory, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().extension() == kSurfaceExtension) {
                TerrainSurfaceAsset asset{};
                std::string error{};
                if (ReadTerrainSurfaceAsset(it->path(), asset, error)) {
                    entries.push_back({asset.name, it->path(), builtin});
                }
            }
        }
    };
    scan(TerrainModuleDirectory() / "Content" / "Surfaces", true);
    scan(projectRoot / "Assets", false);
    return entries;
}

void TerrainSurfaceLibrary::Shutdown() {
    if (shared_ != nullptr) {
        shared_->cancel = true;
    }
    if (pending_.valid()) {
        pending_.wait();
        (void)pending_.get();
    }
}

void TerrainSurfaceLibrary::SetSlots(const std::filesystem::path& projectRoot, std::vector<std::string> names) {
    if (names == names_ && (pending_.valid() || !surfaces_.empty() || names.empty())) {
        return;
    }
    Shutdown();
    names_ = std::move(names);
    auto shared = std::make_shared<Shared>();
    shared->total = (std::max)(static_cast<uint32_t>(names_.size()), 1u);
    shared_ = shared;
    const std::vector<SurfaceCatalogEntry> catalog = ListTerrainSurfaces(projectRoot);
    const std::filesystem::path cacheDirectory = projectRoot / "Library" / "TerrainKit" / "Surfaces";
    pending_ = std::async(std::launch::async, [names = names_, catalog, cacheDirectory, shared]() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Loaded loaded{};
        for (const std::string& name : names) {
            if (shared->cancel) {
                break;
            }
            CookedSurface surface{};
            const auto entry = std::find_if(catalog.begin(), catalog.end(),
                                            [&](const SurfaceCatalogEntry& item) { return item.name == name; });
            std::string error{};
            if (entry == catalog.end()) {
                error = "Unknown terrain surface: " + name;
            } else if (ReadTerrainSurfaceAsset(entry->file, surface.asset, error)) {
                const std::filesystem::path cache =
                    cacheDirectory / (HashOfAsset(entry->file, surface.asset) + ".htsc");
                if (!ReadCooked(cache, surface)) {
                    if (CookSurface(entry->file, surface.asset, surface, error)) {
                        surface.cooked = true;
                        WriteCooked(cache, surface);
                    }
                }
            }
            if (!error.empty()) {
                loaded.error = error;
                surface.asset.name = name;
            }
            loaded.surfaces.push_back(std::move(surface));
            ++shared->finished;
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return loaded;
    });
}

bool TerrainSurfaceLibrary::Poll() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    Loaded loaded = pending_.get();
    surfaces_ = std::move(loaded.surfaces);
    lastError_ = std::move(loaded.error);
    ++revision_;
    return true;
}

float TerrainSurfaceLibrary::Progress() const noexcept {
    return shared_ == nullptr
               ? 1.0f
               : static_cast<float>(shared_->finished.load()) / static_cast<float>(shared_->total.load());
}

} // namespace HIKARI::TERRAINKIT
