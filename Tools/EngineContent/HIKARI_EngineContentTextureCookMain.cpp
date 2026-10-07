// Cooks every texture an engine content manifest declares into its .htex
// artifact. The manifests are the only source of truth: this tool keeps no
// asset list and no per-texture settings of its own.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <objbase.h>
#include <DirectXTex.h>
#include <json.hpp>

#include "Assets/Formats/HIKARI_HtexFormat.h"
#include "Assets/Importers/HIKARI_TextureImportBackend_DirectXTex.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Core/Hashing/HIKARI_Sha256.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace {
namespace FS = std::filesystem;
using HIKARI::TextureAssetColorSpace;
using HIKARI::TextureCompression;
using HIKARI::TextureMipPolicy;
using HIKARI::TextureUsage;
using HIKARI::ASSETS::IMPORT_POLICY::ParseTextureColorSpace;
using HIKARI::ASSETS::IMPORT_POLICY::ParseTextureCompression;
using HIKARI::ASSETS::IMPORT_POLICY::ParseTextureMipPolicy;
using HIKARI::ASSETS::IMPORT_POLICY::ParseTextureUsage;

struct TextureRequest {
    std::string id{};
    FS::path sourcePath{};
    FS::path cookedPath{};
    TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
    TextureUsage usage = TextureUsage::Auto;
    TextureCompression compression = TextureCompression::Auto;
    TextureMipPolicy mipPolicy = TextureMipPolicy::Auto;
};

void Report(const std::string& message) { std::cerr << message << '\n'; }

// マニフェストは唯一の宣言元。ここで列挙した設定以外は受け付けず、
// 未知の値や未対応の圧縮を黙って置き換えない。
bool ReadSettings(const nlohmann::json& asset, TextureRequest& outRequest, std::string& outError) {
    const std::string colorSpaceText = asset.value("colorSpace", std::string{});
    outRequest.colorSpace = ParseTextureColorSpace(colorSpaceText, TextureAssetColorSpace::Auto);
    if (outRequest.colorSpace != TextureAssetColorSpace::Srgb &&
        outRequest.colorSpace != TextureAssetColorSpace::Linear) {
        outError = "unsupported colorSpace '" + colorSpaceText + "'";
        return false;
    }

    const std::string compressionText = asset.value("compression", std::string{});
    outRequest.compression = ParseTextureCompression(compressionText, TextureCompression::Auto);
    if (outRequest.compression != TextureCompression::BC7 && outRequest.compression != TextureCompression::BC5) {
        outError = "unsupported compression '" + compressionText + "'";
        return false;
    }

    const std::string mipPolicyText = asset.value("mipPolicy", std::string{});
    outRequest.mipPolicy = ParseTextureMipPolicy(mipPolicyText, TextureMipPolicy::Auto);
    if (outRequest.mipPolicy != TextureMipPolicy::Generate && outRequest.mipPolicy != TextureMipPolicy::None) {
        outError = "unsupported mipPolicy '" + mipPolicyText + "'";
        return false;
    }

    const std::string usageText = asset.value("usage", std::string{});
    outRequest.usage = ParseTextureUsage(usageText, TextureUsage::Auto);
    if (outRequest.usage == TextureUsage::Auto) {
        outError = "unsupported usage '" + usageText + "'";
        return false;
    }
    if (outRequest.compression == TextureCompression::BC5 &&
        (outRequest.usage != TextureUsage::Normal || outRequest.colorSpace != TextureAssetColorSpace::Linear)) {
        outError = "BC5 requires a linear normal texture";
        return false;
    }
    return true;
}

bool CollectRequests(const FS::path& contentRoot, std::string_view onlyManifest, std::string_view onlyAssetId,
                     std::vector<TextureRequest>& outRequests, bool& outFailed) {
    std::vector<FS::path> manifests{};
    std::error_code error{};
    for (FS::recursive_directory_iterator iterator(contentRoot, error), end; iterator != end && !error;
         iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().filename() == "BuiltinAssets.json") {
            if (!onlyManifest.empty()) {
                const FS::path relative = iterator->path().lexically_relative(contentRoot);
                if (relative.generic_string() != onlyManifest) {
                    continue;
                }
            }
            manifests.push_back(iterator->path());
        }
    }
    if (error) {
        Report("Enumerate content manifests failed: " + error.message());
        outFailed = true;
        return false;
    }
    std::sort(manifests.begin(), manifests.end());

    for (const FS::path& manifestPath : manifests) {
        nlohmann::json root{};
        std::string readError{};
        if (!HIKARI::SERIALIZATION::JSON::ReadJsonFile(manifestPath, root, &readError) || !root.is_object() ||
            root.value("schema", std::string{}) != "hikari.engine_content_assets" || !root.contains("assets") ||
            !root["assets"].is_array()) {
            Report("Invalid content manifest " + manifestPath.generic_string() + ": " + readError);
            outFailed = true;
            continue;
        }

        for (const nlohmann::json& asset : root["assets"]) {
            if (!asset.is_object() || asset.value("type", std::string{}) != "Texture") {
                continue;
            }
            const std::string id = asset.value("id", std::string{});
            if (!onlyAssetId.empty() && id != onlyAssetId) {
                continue;
            }
            TextureRequest request{};
            request.id = id;
            const std::string sourceText = asset.value("sourcePath", std::string{});
            const std::string cookedText = asset.value("path", std::string{});
            std::string settingsError{};
            if (id.empty() || sourceText.empty() || cookedText.empty()) {
                Report("Manifest entry is missing id/sourcePath/path: " + manifestPath.generic_string());
                outFailed = true;
                continue;
            }
            if (!ReadSettings(asset, request, settingsError)) {
                Report("Unsupported settings for " + id + ": " + settingsError);
                outFailed = true;
                continue;
            }
            request.sourcePath = contentRoot / FS::path(sourceText);
            request.cookedPath = contentRoot / FS::path(cookedText);
            outRequests.push_back(std::move(request));
        }
    }
    return true;
}

// 透明領域の RGB は「無色の黒」であり、mip 生成はカラーとアルファを分けて
// 平均する（TEX_FILTER_SEPARATE_ALPHA）。そのため縮小するほど黒が色へ混ざり、
// 暗い縁と「ズームで見え方が変わる」症状になる。mip を作る前に、色だけを
// 透明領域へにじませておく。アルファ値には触らない。
bool DilateColorIntoTransparentPixels(DirectX::ScratchImage& image, std::string& outError) {
    if (image.GetImageCount() == 0u) {
        outError = "no image";
        return false;
    }
    // 以降のインデックス計算を単純にするため既知のレイアウトへ寄せる。
    const DXGI_FORMAT rgbaFormat =
        DirectX::IsSRGB(image.GetMetadata().format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (image.GetMetadata().format != rgbaFormat) {
        DirectX::ScratchImage converted{};
        if (FAILED(DirectX::Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(), rgbaFormat,
                                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
            outError = "conversion to R8G8B8A8_UNORM failed";
            return false;
        }
        image = std::move(converted);
    }

    const DirectX::Image* base = image.GetImage(0u, 0u, 0u);
    if (base == nullptr || base->pixels == nullptr || base->width == 0u || base->height == 0u) {
        outError = "empty base level";
        return false;
    }
    const size_t width = base->width;
    const size_t height = base->height;
    const size_t pixelCount = width * height;

    std::vector<uint8_t> color(pixelCount * 4u, 0u);
    std::vector<uint8_t> hasColor(pixelCount, 0u);
    bool anyTransparent = false;
    for (size_t y = 0u; y < height; ++y) {
        const uint8_t* source = base->pixels + y * base->rowPitch;
        for (size_t x = 0u; x < width; ++x) {
            const size_t index = y * width + x;
            color[index * 4u + 0u] = source[x * 4u + 0u];
            color[index * 4u + 1u] = source[x * 4u + 1u];
            color[index * 4u + 2u] = source[x * 4u + 2u];
            color[index * 4u + 3u] = source[x * 4u + 3u];
            if (source[x * 4u + 3u] == 0u) {
                anyTransparent = true;
            } else {
                hasColor[index] = 1u;
            }
        }
    }
    // 透明画素が無いなら滲ませる先が無い。この場合は何もしない。
    if (!anyTransparent)
        return true;

    constexpr int kDilateIterations = 8;
    std::vector<uint8_t> snapshot = color;
    for (int iteration = 0; iteration < kDilateIterations; ++iteration) {
        std::vector<uint8_t> next = hasColor;
        bool changed = false;
        for (size_t y = 0u; y < height; ++y) {
            for (size_t x = 0u; x < width; ++x) {
                const size_t index = y * width + x;
                if (hasColor[index] != 0u)
                    continue;
                uint32_t red = 0u;
                uint32_t green = 0u;
                uint32_t blue = 0u;
                uint32_t count = 0u;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int64_t sx = static_cast<int64_t>(x) + dx;
                        const int64_t sy = static_cast<int64_t>(y) + dy;
                        if (sx < 0 || sy < 0 || sx >= static_cast<int64_t>(width) ||
                            sy >= static_cast<int64_t>(height)) {
                            continue;
                        }
                        const size_t neighbor = static_cast<size_t>(sy) * width + static_cast<size_t>(sx);
                        if (hasColor[neighbor] == 0u)
                            continue;
                        red += snapshot[neighbor * 4u + 0u];
                        green += snapshot[neighbor * 4u + 1u];
                        blue += snapshot[neighbor * 4u + 2u];
                        ++count;
                    }
                }
                if (count == 0u)
                    continue;
                color[index * 4u + 0u] = static_cast<uint8_t>(red / count);
                color[index * 4u + 1u] = static_cast<uint8_t>(green / count);
                color[index * 4u + 2u] = static_cast<uint8_t>(blue / count);
                // アルファは 0 のまま。次反復で参照元になる。
                next[index] = 1u;
                changed = true;
            }
        }
        if (!changed)
            break;
        hasColor.swap(next);
        snapshot = color;
    }

    for (size_t y = 0u; y < height; ++y) {
        uint8_t* destination = base->pixels + y * base->rowPitch;
        for (size_t x = 0u; x < width; ++x) {
            const size_t index = y * width + x;
            destination[x * 4u + 0u] = color[index * 4u + 0u];
            destination[x * 4u + 1u] = color[index * 4u + 1u];
            destination[x * 4u + 2u] = color[index * 4u + 2u];
            destination[x * 4u + 3u] = color[index * 4u + 3u];
        }
    }
    return true;
}

bool CookTexture(const TextureRequest& request) {
    if (request.usage == TextureUsage::Normal) {
        // 法線の decode・mip 正規化・圧縮は通常 importer と同じ所有者へ渡す。
        // カラー用の単純平均を法線へ流用しない。
        HIKARI::TextureImportSettings settings{};
        settings.colorSpace = request.colorSpace;
        settings.usage = request.usage;
        settings.compression = request.compression;
        settings.mipPolicy = request.mipPolicy;
        HIKARI::DirectXTexTextureImportBackend backend{};
        std::string message{};
        if (!backend.ConvertToHtexAndDds(request.sourcePath, request.cookedPath, {}, settings, message)) {
            Report(message);
            return false;
        }
        return true;
    }

    DirectX::TexMetadata sourceMetadata{};
    DirectX::ScratchImage source{};
    // WIC は TGA を読めないため、拡張子で読み込み経路を分ける。
    std::string extension = request.sourcePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    HRESULT result = extension == ".tga"
                         ? DirectX::LoadFromTGAFile(request.sourcePath.c_str(), &sourceMetadata, source)
                         : DirectX::LoadFromWICFile(request.sourcePath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB,
                                                    &sourceMetadata, source);
    if (FAILED(result) || source.GetImageCount() == 0u) {
        Report("Loading the source image failed: " + request.sourcePath.generic_string());
        return false;
    }
    // 通常の Texture importer と同じく、manifest が元画像の符号化を所有する。
    // WIC/TGA の既定 UNORM を線形光として圧縮すると sRGB が二重に掛かる。
    const DXGI_FORMAT sourceFormat = request.colorSpace == TextureAssetColorSpace::Srgb
                                         ? DirectX::MakeSRGB(source.GetMetadata().format)
                                         : DirectX::MakeLinear(source.GetMetadata().format);
    if (!source.OverrideFormat(sourceFormat)) {
        Report("Source color-space declaration failed: " + request.sourcePath.generic_string());
        return false;
    }

    {
        // 半透明の縁が黒く濁る/縮小時に暗くなるのを防ぐ。mip より前に必須。
        std::string bleedError{};
        if (!DilateColorIntoTransparentPixels(source, bleedError)) {
            Report("Alpha bleed failed for " + request.sourcePath.generic_string() + ": " + bleedError);
            return false;
        }
    }

    DirectX::ScratchImage mipmapped{};
    const DirectX::ScratchImage* compressedInput = &source;
    if (request.mipPolicy == TextureMipPolicy::Generate) {
        result = DirectX::GenerateMipMaps(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
                                          // 自動フィルタは非 2 冪サイズにも対応する。BOX/FANT 固定は
                                          // sRGB のソフトウェア経路で 400x400 等の mip 生成に失敗する。
                                          DirectX::TEX_FILTER_SEPARATE_ALPHA | DirectX::TEX_FILTER_FORCE_NON_WIC, 0u,
                                          mipmapped);
        if (FAILED(result)) {
            Report("GenerateMipMaps failed: " + request.sourcePath.generic_string());
            return false;
        }
        compressedInput = &mipmapped;
    }

    const DXGI_FORMAT compressedFormat =
        request.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
    DirectX::ScratchImage compressed{};
    result = DirectX::Compress(compressedInput->GetImages(), compressedInput->GetImageCount(),
                               compressedInput->GetMetadata(), compressedFormat,
                               DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_PARALLEL,
                               DirectX::TEX_THRESHOLD_DEFAULT, compressed);
    if (FAILED(result)) {
        Report("BC7 compression failed: " + request.sourcePath.generic_string());
        return false;
    }

    const DirectX::TexMetadata metadata = compressed.GetMetadata();
    HIKARI::HtexTexture texture{};
    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);
    texture.depth = static_cast<uint32_t>(metadata.depth);
    texture.arraySize = static_cast<uint32_t>(metadata.arraySize);
    texture.mipLevels = static_cast<uint32_t>(metadata.mipLevels);
    texture.format = metadata.format;
    texture.dimension = HIKARI::HtexTextureDimension::Texture2D;
    texture.colorSpace = request.colorSpace;
    texture.usage = request.usage;
    texture.compression = request.compression;
    for (size_t mip = 0u; mip < metadata.mipLevels; ++mip) {
        const DirectX::Image* image = compressed.GetImage(mip, 0u, 0u);
        if (image == nullptr) {
            Report("Missing compressed mip: " + request.sourcePath.generic_string());
            return false;
        }
        HIKARI::HtexSubresource subresource{};
        subresource.mipLevel = static_cast<uint32_t>(mip);
        subresource.width = static_cast<uint32_t>(image->width);
        subresource.height = static_cast<uint32_t>(image->height);
        subresource.rowPitch = image->rowPitch;
        subresource.slicePitch = image->slicePitch;
        subresource.data.assign(image->pixels, image->pixels + image->slicePitch);
        texture.subresources.push_back(std::move(subresource));
    }

    std::error_code error{};
    FS::create_directories(request.cookedPath.parent_path(), error);
    if (error) {
        Report("Create cooked directory failed: " + error.message());
        return false;
    }
    std::string message{};
    if (!HIKARI::WriteHtexFile(request.cookedPath, texture, message)) {
        Report(message);
        return false;
    }
    return true;
}

std::string HashOf(const FS::path& path) {
    HIKARI::HASH::Sha256Digest digest{};
    std::string error{};
    if (!HIKARI::HASH::ComputeFileSha256(path, digest, error)) {
        return "<" + error + ">";
    }
    return HIKARI::HASH::ToHex(digest);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: HIKARI_EngineContentTextureCookTool <engine-content-root>"
                     " [--manifest <relative-manifest-path>] [--only <assetId>]\n";
        return 2;
    }
    const FS::path contentRoot = argv[1];
    std::string onlyManifest{};
    std::string onlyAssetId{};
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        const bool hasValue = index + 1 < argc;
        if (option == "--manifest" && hasValue) {
            onlyManifest = argv[++index];
        } else if (option == "--only" && hasValue) {
            onlyAssetId = argv[++index];
        } else {
            std::cerr << "unknown option: " << option << '\n';
            return 2;
        }
    }
    std::error_code error{};
    if (!FS::is_directory(contentRoot, error) || error) {
        Report("Engine content root is not a directory: " + contentRoot.generic_string());
        return 3;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        Report("CoInitializeEx failed");
        return 4;
    }
    const bool ownsCom = SUCCEEDED(comResult);

    std::vector<TextureRequest> requests{};
    bool failed = false;
    (void)CollectRequests(contentRoot, onlyManifest, onlyAssetId, requests, failed);
    if (requests.empty() && !failed) {
        Report("No texture asset was found under " + contentRoot.generic_string());
        failed = true;
    }
    for (const TextureRequest& request : requests) {
        if (!CookTexture(request)) {
            failed = true;
            continue;
        }
        // マニフェストへ書き戻すハッシュをその場で出す。手計算は不要。
        std::cout << "cooked " << request.cookedPath.filename().string() << "\n  id            " << request.id
                  << "\n  cookedSha256  " << HashOf(request.cookedPath) << "\n  sourceSha256  "
                  << HashOf(request.sourcePath) << '\n';
    }

    if (ownsCom) {
        CoUninitialize();
    }
    return failed ? 1 : 0;
}
