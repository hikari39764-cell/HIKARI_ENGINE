#include "HIKARI_TextureImportBackend_DirectXTex.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#include "Assets/Formats/HIKARI_HtexFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"

namespace HIKARI {

    namespace {

        std::string ToHexHr(HRESULT hr) {
            std::ostringstream oss;
            oss << "0x" << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<unsigned long>(hr);
            return oss.str();
        }

        bool IsWicExtension(const std::string& ext) {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".bmp" || ext == ".tif" || ext == ".tiff";
        }

        HRESULT LoadScratchImage(
            const std::filesystem::path& sourcePath,
            DirectX::TexMetadata& metadata,
            DirectX::ScratchImage& image,
            std::string& outMessage) {

            const std::string ext = TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
            const std::wstring widePath = sourcePath.wstring();

            if (ext == ".dds") {
                return DirectX::LoadFromDDSFile(
                    widePath.c_str(),
                    DirectX::DDS_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (ext == ".tga") {
                return DirectX::LoadFromTGAFile(
                    widePath.c_str(),
                    DirectX::TGA_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (ext == ".hdr") {
                return DirectX::LoadFromHDRFile(
                    widePath.c_str(),
                    &metadata,
                    image);
            }
            if (IsWicExtension(ext)) {
                return DirectX::LoadFromWICFile(
                    widePath.c_str(),
                    DirectX::WIC_FLAGS_NONE,
                    &metadata,
                    image);
            }

            outMessage = "[DirectXTexBackend] unsupported texture extension: " + ext;
            return E_NOTIMPL;
        }

        bool IsHdrLike(const std::filesystem::path& sourcePath, const DirectX::TexMetadata& metadata, TextureUsage usage) {
            const std::string ext = TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
            if (ext == ".hdr" || ext == ".exr") {
                return true;
            }
            if (usage == TextureUsage::SkyCubemap ||
                usage == TextureUsage::IblIrradiance ||
                usage == TextureUsage::IblPrefiltered) {
                return true;
            }
            return DirectX::IsTypeless(metadata.format) == false &&
                (metadata.format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                    metadata.format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                    metadata.format == DXGI_FORMAT_R11G11B10_FLOAT ||
                    metadata.format == DXGI_FORMAT_BC6H_UF16 ||
                    metadata.format == DXGI_FORMAT_BC6H_SF16);
        }

        DXGI_FORMAT ResolveWorkingFormat(
            const std::filesystem::path& sourcePath,
            const DirectX::TexMetadata& metadata,
            const TextureImportSettings& settings) {

            if (IsHdrLike(sourcePath, metadata, settings.usage)) {
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            }

            if (settings.colorSpace == TextureAssetColorSpace::Srgb) {
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            }

            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        DXGI_FORMAT ResolveCompressedFormat(
            const std::filesystem::path& sourcePath,
            const DirectX::TexMetadata& metadata,
            const TextureImportSettings& settings) {

            TextureCompression compression = settings.compression;
            if (compression == TextureCompression::Auto) {
                switch (settings.usage) {
                case TextureUsage::Normal:
                    compression = TextureCompression::BC7;
                    break;
                case TextureUsage::BaseColor:
                case TextureUsage::Emissive:
                case TextureUsage::UI:
                    compression = TextureCompression::BC7;
                    break;
                case TextureUsage::MetallicRoughness:
                case TextureUsage::Occlusion:
                case TextureUsage::Mask:
                    compression = TextureCompression::BC7;
                    break;
                case TextureUsage::SkyCubemap:
                case TextureUsage::IblIrradiance:
                case TextureUsage::IblPrefiltered:
                    compression = TextureCompression::None;
                    break;
                default:
                    compression = IsHdrLike(sourcePath, metadata, settings.usage)
                        ? TextureCompression::None
                        : TextureCompression::BC7;
                    break;
                }
            }

            switch (compression) {
            case TextureCompression::BC1:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
            case TextureCompression::BC3:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
            case TextureCompression::BC4:
                return DXGI_FORMAT_BC4_UNORM;
            case TextureCompression::BC5:
                return DXGI_FORMAT_BC5_UNORM;
            case TextureCompression::BC6H:
                return DXGI_FORMAT_BC6H_UF16;
            case TextureCompression::BC7:
                return settings.colorSpace == TextureAssetColorSpace::Srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
            case TextureCompression::None:
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        DirectX::TEX_FILTER_FLAGS ResolveFilterFlags(TextureAssetColorSpace colorSpace) {
            if (colorSpace == TextureAssetColorSpace::Srgb) {
                return static_cast<DirectX::TEX_FILTER_FLAGS>(
                    DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB);
            }
            return DirectX::TEX_FILTER_DEFAULT;
        }

        DirectX::TEX_COMPRESS_FLAGS ResolveCompressFlags(
            TextureAssetColorSpace colorSpace,
            DXGI_FORMAT compressedFormat,
            bool quick)
        {
            DirectX::TEX_COMPRESS_FLAGS flags = DirectX::TEX_COMPRESS_DEFAULT;

            if (colorSpace == TextureAssetColorSpace::Srgb) {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                    flags | DirectX::TEX_COMPRESS_SRGB_OUT);
            }

            // 允许 DirectXTex 内部多线程压缩。
            flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                flags | DirectX::TEX_COMPRESS_PARALLEL);

            // 只对 BC7 开 quick。
            if (quick &&
                (compressedFormat == DXGI_FORMAT_BC7_UNORM ||
                    compressedFormat == DXGI_FORMAT_BC7_UNORM_SRGB)) {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                    flags | DirectX::TEX_COMPRESS_BC7_QUICK);
            }

            return flags;
        }

        bool NeedsMipGeneration(const TextureImportSettings& settings, const DirectX::TexMetadata& metadata) {
            if (settings.mipPolicy == TextureMipPolicy::None ||
                settings.mipPolicy == TextureMipPolicy::Preserve) {
                return false;
            }
            if (metadata.IsCubemap()) {
                return false;
            }
            return metadata.mipLevels <= 1;
        }

        const DirectX::ScratchImage& SelectImage(const DirectX::ScratchImage& source, const DirectX::ScratchImage& candidate) {
            return candidate.GetImageCount() > 0 ? candidate : source;
        }

        bool SaveScratchImageToDds(
            const DirectX::ScratchImage& image,
            const std::filesystem::path& outputPath,
            std::string& outMessage) {

            std::error_code ec{};
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec) {
                outMessage = "[DirectXTexBackend] failed to create DDS output directory: " +
                    outputPath.parent_path().generic_string();
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }

            const HRESULT hr = DirectX::SaveToDDSFile(
                image.GetImages(),
                image.GetImageCount(),
                image.GetMetadata(),
                DirectX::DDS_FLAGS_FORCE_DX10_EXT,
                outputPath.wstring().c_str());
            if (FAILED(hr)) {
                outMessage = "[DirectXTexBackend] SaveToDDSFile failed: " +
                    outputPath.generic_string() +
                    " hr=" +
                    ToHexHr(hr);
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }

            return true;
        }

        TextureCompression CompressionFromDxgiFormat(DXGI_FORMAT format, TextureCompression fallback) {
            switch (format) {
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                return TextureCompression::BC1;
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
                return TextureCompression::BC3;
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
                return TextureCompression::BC4;
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
                return TextureCompression::BC5;
            case DXGI_FORMAT_BC6H_TYPELESS:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
                return TextureCompression::BC6H;
            case DXGI_FORMAT_BC7_TYPELESS:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return TextureCompression::BC7;
            default:
                return DirectX::IsCompressed(format) ? fallback : TextureCompression::None;
            }
        }

        bool WriteScratchImageToHtex(
            const DirectX::ScratchImage& image,
            const TextureImportSettings& settings,
            const std::filesystem::path& outputPath,
            std::string& outMessage) {

            const DirectX::TexMetadata metadata = image.GetMetadata();
            if (metadata.width == 0 || metadata.height == 0 || image.GetImageCount() == 0) {
                outMessage = "[DirectXTexBackend] invalid cooked texture image for HTEX";
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }

            HtexTexture texture{};
            texture.width = static_cast<uint32_t>(metadata.width);
            texture.height = static_cast<uint32_t>(metadata.height);
            texture.depth = static_cast<uint32_t>(metadata.depth);
            texture.arraySize = static_cast<uint32_t>(metadata.arraySize);
            texture.mipLevels = static_cast<uint32_t>(metadata.mipLevels == 0 ? 1 : metadata.mipLevels);
            texture.format = metadata.format;
            texture.dimension = metadata.IsCubemap()
                ? HtexTextureDimension::TextureCube
                : HtexTextureDimension::Texture2D;
            texture.colorSpace = settings.colorSpace;
            texture.usage = settings.usage;
            texture.compression = CompressionFromDxgiFormat(metadata.format, settings.compression);

            const uint32_t mipLevels = texture.mipLevels == 0 ? 1u : texture.mipLevels;
            texture.subresources.reserve(image.GetImageCount());
            const DirectX::Image* images = image.GetImages();
            for (size_t i = 0; i < image.GetImageCount(); ++i) {
                const DirectX::Image& source = images[i];
                HtexSubresource subresource{};
                subresource.mipLevel = static_cast<uint32_t>(i % mipLevels);
                subresource.arraySlice = static_cast<uint32_t>(i / mipLevels);
                subresource.width = static_cast<uint32_t>(source.width);
                subresource.height = static_cast<uint32_t>(source.height);
                subresource.depth = 1;
                subresource.rowPitch = static_cast<uint64_t>(source.rowPitch);
                subresource.slicePitch = static_cast<uint64_t>(source.slicePitch);
                subresource.data.resize(source.slicePitch);
                if (source.pixels && source.slicePitch > 0) {
                    std::memcpy(subresource.data.data(), source.pixels, source.slicePitch);
                }
                texture.subresources.push_back(std::move(subresource));
            }

            if (!WriteHtexFile(outputPath, texture, outMessage)) {
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }

            return true;
        }

        bool CookTextureImage(
            const std::filesystem::path& sourcePath,
            const TextureImportSettings& settings,
            DirectX::ScratchImage& outImage,
            std::string& outMessage,
            AssetTaskContext* task) {

            if (task != nullptr) {
                task->ReportStage(
                    "Reading texture source",
                    0.22f,
                    true,
                    sourcePath.filename().string());
                if (task->IsCancellationRequested()) {
                    outMessage = "[DirectXTexBackend] import canceled";
                    return false;
                }
            }

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage loaded{};
            HRESULT hr = LoadScratchImage(sourcePath, metadata, loaded, outMessage);
            if (FAILED(hr)) {
                if (outMessage.empty()) {
                    outMessage = "[DirectXTexBackend] load failed: " +
                        sourcePath.generic_string() +
                        " hr=" +
                        ToHexHr(hr);
                }
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }

            DirectX::ScratchImage working{};
            const DXGI_FORMAT workingFormat = ResolveWorkingFormat(sourcePath, metadata, settings);
            if (metadata.format != workingFormat && !metadata.IsCubemap()) {
                if (task != nullptr) {
                    task->ReportStage(
                        "Converting texture format",
                        0.38f,
                        true,
                        sourcePath.filename().string());
                }
                hr = DirectX::Convert(
                    loaded.GetImages(),
                    loaded.GetImageCount(),
                    metadata,
                    workingFormat,
                    ResolveFilterFlags(settings.colorSpace),
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    working);
                if (FAILED(hr)) {
                    outMessage = "[DirectXTexBackend] format conversion failed: " +
                        sourcePath.generic_string() +
                        " hr=" +
                        ToHexHr(hr);
                    HIKARI_LOG_ERROR(outMessage);
                    return false;
                }
            }

            const DirectX::ScratchImage& converted = SelectImage(loaded, working);
            DirectX::ScratchImage mipmapped{};
            const DirectX::TexMetadata convertedMetadata = converted.GetMetadata();
            if (NeedsMipGeneration(settings, convertedMetadata) && !DirectX::IsCompressed(convertedMetadata.format)) {
                if (task != nullptr) {
                    if (task->IsCancellationRequested()) {
                        outMessage = "[DirectXTexBackend] import canceled";
                        return false;
                    }
                    task->ReportStage(
                        "Generating texture mipmaps",
                        0.54f,
                        true,
                        sourcePath.filename().string());
                }
                hr = DirectX::GenerateMipMaps(
                    converted.GetImages(),
                    converted.GetImageCount(),
                    convertedMetadata,
                    ResolveFilterFlags(settings.colorSpace),
                    0,
                    mipmapped);
                if (FAILED(hr)) {
                    outMessage = "[DirectXTexBackend] mipmap generation failed, falling back to source mip: " +
                        sourcePath.generic_string() +
                        " hr=" +
                        ToHexHr(hr);
                    HIKARI_LOG_WARN(outMessage);
                }
            }

            const DirectX::ScratchImage& mipSource = SelectImage(converted, mipmapped);
            const DirectX::TexMetadata mipMetadata = mipSource.GetMetadata();

            DirectX::ScratchImage compressed{};
            const DXGI_FORMAT compressedFormat = ResolveCompressedFormat(sourcePath, mipMetadata, settings);
            if (compressedFormat != DXGI_FORMAT_UNKNOWN && !mipMetadata.IsCubemap()) {
                if (task != nullptr) {
                    if (task->IsCancellationRequested()) {
                        outMessage = "[DirectXTexBackend] import canceled";
                        return false;
                    }
                    task->ReportStage(
                        "Compressing texture blocks",
                        0.70f,
                        false,
                        sourcePath.filename().string());
                }
                const bool quickCompress = true;
                hr = DirectX::Compress(
                    mipSource.GetImages(),
                    mipSource.GetImageCount(),
                    mipMetadata,
                    compressedFormat,
                    ResolveCompressFlags(settings.colorSpace, compressedFormat, quickCompress),
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    compressed);
                if (FAILED(hr)) {
                    outMessage = "[DirectXTexBackend] block compression failed, writing uncompressed DDS: " +
                        sourcePath.generic_string() +
                        " hr=" +
                        ToHexHr(hr);
                    HIKARI_LOG_WARN(outMessage);
                }
            }

            if (compressed.GetImageCount() > 0) {
                outImage = std::move(compressed);
            } else if (mipmapped.GetImageCount() > 0) {
                outImage = std::move(mipmapped);
            } else if (working.GetImageCount() > 0) {
                outImage = std::move(working);
            } else {
                outImage = std::move(loaded);
            }

            if (task != nullptr) {
                if (task->IsCancellationRequested()) {
                    outMessage = "[DirectXTexBackend] import canceled";
                    return false;
                }
                task->ReportStage(
                    "Texture cook ready",
                    0.82f,
                    true,
                    sourcePath.filename().string());
            }
            return true;
        }

        struct AlphaScanResult {
            bool hasAlphaChannel = false;
            bool hasMeaningfulAlpha = false;
            bool hasTranslucentAlpha = false;
            bool hasCutoutAlpha = false;
            float nonOpaqueRatio = 0.0f;
            float translucentRatio = 0.0f;
            float cutoutRatio = 0.0f;
        };

        bool IsRgba8Format(DXGI_FORMAT format) {
            return
                format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        }

        AlphaScanResult ScanMeaningfulAlpha(
            const DirectX::ScratchImage& source,
            const DirectX::TexMetadata& metadata) {

            AlphaScanResult result{};
            DirectX::ScratchImage rgba{};
            const DirectX::ScratchImage* scanSource = &source;
            DirectX::TexMetadata scanMetadata = metadata;

            if (DirectX::IsCompressed(metadata.format)) {
                if (FAILED(DirectX::Decompress(
                        source.GetImages(),
                        source.GetImageCount(),
                        metadata,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        rgba))) {
                    return result;
                }
                scanSource = &rgba;
                scanMetadata = rgba.GetMetadata();
            } else if (!IsRgba8Format(metadata.format)) {
                if (FAILED(DirectX::Convert(
                        source.GetImages(),
                        source.GetImageCount(),
                        metadata,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        DirectX::TEX_FILTER_DEFAULT,
                        DirectX::TEX_THRESHOLD_DEFAULT,
                        rgba))) {
                    return result;
                }
                scanSource = &rgba;
                scanMetadata = rgba.GetMetadata();
            }

            if (!IsRgba8Format(scanMetadata.format)) {
                return result;
            }

            const DirectX::Image* images = scanSource->GetImages();
            const size_t imageCount = scanSource->GetImageCount();
            uint64_t pixelCount = 0;
            uint64_t nonOpaqueCount = 0;
            uint64_t translucentCount = 0;
            uint64_t cutoutCount = 0;
            uint8_t minAlpha = (std::numeric_limits<uint8_t>::max)();
            uint8_t maxAlpha = 0;

            for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
                const DirectX::Image& image = images[imageIndex];
                if (!image.pixels || !IsRgba8Format(image.format)) {
                    continue;
                }
                if (image.width != scanMetadata.width || image.height != scanMetadata.height) {
                    continue;
                }
                for (size_t y = 0; y < image.height; ++y) {
                    const uint8_t* row = image.pixels + y * image.rowPitch;
                    for (size_t x = 0; x < image.width; ++x) {
                        const uint8_t alpha = row[x * 4u + 3u];
                        minAlpha = (std::min)(minAlpha, alpha);
                        maxAlpha = (std::max)(maxAlpha, alpha);
                        ++pixelCount;
                        if (alpha < 250u) {
                            ++nonOpaqueCount;
                        }
                        if (alpha <= 5u) {
                            ++cutoutCount;
                        } else if (alpha < 250u) {
                            ++translucentCount;
                        }
                    }
                }
            }

            if (pixelCount == 0) {
                return result;
            }

            result.hasAlphaChannel = minAlpha < 255u || maxAlpha < 255u;
            result.hasMeaningfulAlpha = nonOpaqueCount > 0u;
            result.hasTranslucentAlpha = translucentCount > 0u;
            result.hasCutoutAlpha = cutoutCount > 0u;
            result.nonOpaqueRatio =
                static_cast<float>(static_cast<double>(nonOpaqueCount) / static_cast<double>(pixelCount));
            result.translucentRatio =
                static_cast<float>(static_cast<double>(translucentCount) / static_cast<double>(pixelCount));
            result.cutoutRatio =
                static_cast<float>(static_cast<double>(cutoutCount) / static_cast<double>(pixelCount));
            return result;
        }
    }

    bool InspectTextureAlphaWithDirectXTex(
        const std::filesystem::path& sourcePath,
        TextureImportSettings& inOutSettings,
        std::string& outMessage) {

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image{};
        const HRESULT hr = LoadScratchImage(sourcePath, metadata, image, outMessage);
        if (FAILED(hr)) {
            if (outMessage.empty()) {
                outMessage = "[DirectXTexBackend] alpha inspect failed: " +
                    sourcePath.generic_string() +
                    " hr=" +
                    ToHexHr(hr);
            }
            return false;
        }

        const AlphaScanResult alpha = ScanMeaningfulAlpha(image, metadata);
        inOutSettings.sourceHasAlphaChannel = alpha.hasAlphaChannel;
        inOutSettings.sourceHasMeaningfulAlpha = alpha.hasMeaningfulAlpha;
        inOutSettings.sourceHasTranslucentAlpha = alpha.hasTranslucentAlpha;
        inOutSettings.sourceHasCutoutAlpha = alpha.hasCutoutAlpha;
        inOutSettings.sourceAlphaNonOpaqueRatio = alpha.nonOpaqueRatio;
        inOutSettings.sourceAlphaTranslucentRatio = alpha.translucentRatio;
        inOutSettings.sourceAlphaCutoutRatio = alpha.cutoutRatio;
        return true;
    }

    bool DirectXTexTextureImportBackend::IsAvailable() const {
        return true;
    }

    bool DirectXTexTextureImportBackend::Inspect(
        const std::filesystem::path& sourcePath,
        TextureImportSettings& inOutSettings,
        std::string& outMessage) {

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image{};
        const HRESULT hr = LoadScratchImage(sourcePath, metadata, image, outMessage);
        if (FAILED(hr)) {
            if (outMessage.empty()) {
                outMessage = "[DirectXTexBackend] inspect failed: " + sourcePath.generic_string() + " hr=" + ToHexHr(hr);
            }
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        inOutSettings.dimension = metadata.IsCubemap()
            ? TextureAssetDimension::TextureCube
            : TextureAssetDimension::Texture2D;

        if (inOutSettings.colorSpace == TextureAssetColorSpace::Auto) {
            inOutSettings.colorSpace = DirectX::IsSRGB(metadata.format)
                ? TextureAssetColorSpace::Srgb
                : TextureAssetColorSpace::Linear;
        }

        const AlphaScanResult alpha = ScanMeaningfulAlpha(image, metadata);
        inOutSettings.sourceHasAlphaChannel = alpha.hasAlphaChannel;
        inOutSettings.sourceHasMeaningfulAlpha = alpha.hasMeaningfulAlpha;
        inOutSettings.sourceHasTranslucentAlpha = alpha.hasTranslucentAlpha;
        inOutSettings.sourceHasCutoutAlpha = alpha.hasCutoutAlpha;
        inOutSettings.sourceAlphaNonOpaqueRatio = alpha.nonOpaqueRatio;
        inOutSettings.sourceAlphaTranslucentRatio = alpha.translucentRatio;
        inOutSettings.sourceAlphaCutoutRatio = alpha.cutoutRatio;

        std::ostringstream oss;
        oss << "[DirectXTexBackend] inspected "
            << sourcePath.generic_string()
            << " width=" << metadata.width
            << " height=" << metadata.height
            << " mips=" << metadata.mipLevels
            << " array=" << metadata.arraySize
            << " cubemap=" << (metadata.IsCubemap() ? "true" : "false")
            << " alpha=" << (alpha.hasMeaningfulAlpha ? "meaningful" : "opaque");
        outMessage = oss.str();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

    bool DirectXTexTextureImportBackend::ConvertToDds(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        const TextureImportSettings& settings,
        std::string& outMessage,
        AssetTaskContext* task) {

        DirectX::ScratchImage finalImage{};
        if (!CookTextureImage(
                sourcePath,
                settings,
                finalImage,
                outMessage,
                task)) {
            return false;
        }

        if (task != nullptr) {
            task->ReportStage(
                "Writing DDS artifact",
                0.90f,
                true,
                outputPath.filename().string());
        }
        if (!SaveScratchImageToDds(finalImage, outputPath, outMessage)) {
            return false;
        }

        outMessage = "[DirectXTexBackend] wrote DDS: " + outputPath.generic_string();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

    bool DirectXTexTextureImportBackend::ConvertToHtexAndDds(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& htexOutputPath,
        const std::filesystem::path& debugDdsOutputPath,
        const TextureImportSettings& settings,
        std::string& outMessage,
        AssetTaskContext* task) {

        DirectX::ScratchImage finalImage{};
        if (!CookTextureImage(
                sourcePath,
                settings,
                finalImage,
                outMessage,
                task)) {
            return false;
        }

        if (task != nullptr) {
            task->ReportStage(
                "Writing HTEX artifact",
                0.88f,
                true,
                htexOutputPath.filename().string());
        }
        if (!WriteScratchImageToHtex(finalImage, settings, htexOutputPath, outMessage)) {
            return false;
        }

        if (task != nullptr) {
            if (task->IsCancellationRequested()) {
                outMessage = "[DirectXTexBackend] import canceled";
                return false;
            }
            task->ReportStage(
                "Writing debug DDS",
                0.94f,
                true,
                debugDdsOutputPath.filename().string());
        }
        if (!debugDdsOutputPath.empty() &&
            !SaveScratchImageToDds(finalImage, debugDdsOutputPath, outMessage)) {
            return false;
        }

        outMessage = "[DirectXTexBackend] wrote HTEX and debug DDS: " +
            htexOutputPath.generic_string();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

} // namespace HIKARI
