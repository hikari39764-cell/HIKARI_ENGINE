#include "HIKARI_TextureImportBackend_DirectXTex.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI {

    namespace {

        std::string ToHexHr(HRESULT hr) {
            std::ostringstream oss;
            oss << "0x" << std::hex
                << std::setw(8)
                << std::setfill('0')
                << static_cast<unsigned long>(hr);
            return oss.str();
        }

        bool IsRgba8Format(DXGI_FORMAT format) {
            return
                format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        }

        HRESULT LoadScalarScratchImage(
            const std::filesystem::path& sourcePath,
            DirectX::TexMetadata& metadata,
            DirectX::ScratchImage& image) {

            const std::string extension =
                TEXT::ToLowerAsciiCopy(
                    sourcePath.extension().string());
            const std::wstring path = sourcePath.wstring();
            if (extension == ".dds") {
                return DirectX::LoadFromDDSFile(
                    path.c_str(),
                    DirectX::DDS_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (extension == ".tga") {
                return DirectX::LoadFromTGAFile(
                    path.c_str(),
                    DirectX::TGA_FLAGS_NONE,
                    &metadata,
                    image);
            }
            if (extension == ".hdr") {
                return DirectX::LoadFromHDRFile(
                    path.c_str(),
                    &metadata,
                    image);
            }
            if (extension == ".png" ||
                extension == ".jpg" ||
                extension == ".jpeg" ||
                extension == ".bmp" ||
                extension == ".tif" ||
                extension == ".tiff") {
                return DirectX::LoadFromWICFile(
                    path.c_str(),
                    DirectX::WIC_FLAGS_NONE,
                    &metadata,
                    image);
            }
            return E_NOTIMPL;
        }

        struct ScalarTextureSource {
            DirectX::ScratchImage image{};
            bool valid = false;
        };

        bool LoadScalarTextureSource(
            const std::filesystem::path& sourcePath,
            ScalarTextureSource& outSource,
            std::string& outMessage) {

            outSource = {};
            if (sourcePath.empty()) {
                return true;
            }

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage loaded{};
            HRESULT hr = LoadScalarScratchImage(
                sourcePath,
                metadata,
                loaded);
            if (FAILED(hr)) {
                outMessage =
                    "[MaterialTexturePacking] failed to load scalar texture: " +
                    sourcePath.generic_string() +
                    " hr=" + ToHexHr(hr);
                return false;
            }
            if (metadata.IsCubemap() ||
                metadata.dimension !=
                    DirectX::TEX_DIMENSION_TEXTURE2D ||
                metadata.arraySize != 1u ||
                metadata.width == 0u ||
                metadata.height == 0u) {
                outMessage =
                    "[MaterialTexturePacking] scalar texture must be a 2D single-slice image: " +
                    sourcePath.generic_string();
                return false;
            }

            DirectX::ScratchImage rgba{};
            if (DirectX::IsCompressed(metadata.format)) {
                hr = DirectX::Decompress(
                    loaded.GetImages(),
                    loaded.GetImageCount(),
                    metadata,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    rgba);
            } else if (!IsRgba8Format(metadata.format)) {
                hr = DirectX::Convert(
                    loaded.GetImages(),
                    loaded.GetImageCount(),
                    metadata,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    rgba);
            } else {
                outSource.image = std::move(loaded);
                outSource.valid = true;
                return true;
            }

            if (FAILED(hr)) {
                outMessage =
                    "[MaterialTexturePacking] failed to convert scalar texture: " +
                    sourcePath.generic_string() +
                    " hr=" + ToHexHr(hr);
                return false;
            }
            outSource.image = std::move(rgba);
            outSource.valid = true;
            return true;
        }

        bool ResizeScalarTextureSource(
            ScalarTextureSource& source,
            size_t width,
            size_t height,
            std::string& outMessage) {

            if (!source.valid) {
                return true;
            }
            const DirectX::TexMetadata metadata =
                source.image.GetMetadata();
            if (metadata.width == width &&
                metadata.height == height) {
                return true;
            }

            DirectX::ScratchImage resized{};
            const HRESULT hr = DirectX::Resize(
                source.image.GetImages(),
                source.image.GetImageCount(),
                metadata,
                width,
                height,
                DirectX::TEX_FILTER_DEFAULT,
                resized);
            if (FAILED(hr)) {
                outMessage =
                    "[MaterialTexturePacking] failed to align scalar texture dimensions hr=" +
                    ToHexHr(hr);
                return false;
            }
            source.image = std::move(resized);
            return true;
        }

        uint8_t ReadScalarRed(
            const ScalarTextureSource& source,
            size_t x,
            size_t y,
            uint8_t fallback) {

            if (!source.valid) {
                return fallback;
            }
            const DirectX::Image* image =
                source.image.GetImage(0u, 0u, 0u);
            if (image == nullptr ||
                image->pixels == nullptr ||
                !IsRgba8Format(image->format)) {
                return fallback;
            }
            const uint8_t* row =
                image->pixels + y * image->rowPitch;
            return row[x * 4u];
        }

        bool SavePackedDds(
            const DirectX::ScratchImage& image,
            const std::filesystem::path& outputPath,
            std::string& outMessage) {

            std::error_code ec{};
            std::filesystem::create_directories(
                outputPath.parent_path(),
                ec);
            if (ec) {
                outMessage =
                    "[MaterialTexturePacking] failed to create output directory: " +
                    ec.message();
                return false;
            }

            const HRESULT hr = DirectX::SaveToDDSFile(
                image.GetImages(),
                image.GetImageCount(),
                image.GetMetadata(),
                DirectX::DDS_FLAGS_FORCE_DX10_EXT,
                outputPath.wstring().c_str());
            if (FAILED(hr)) {
                outMessage =
                    "[MaterialTexturePacking] failed to write DDS: " +
                    outputPath.generic_string() +
                    " hr=" + ToHexHr(hr);
                return false;
            }
            return true;
        }

    } // namespace

    bool WritePackedMetallicRoughnessDdsWithDirectXTex(
        const MetallicRoughnessPackRequest& request,
        const std::filesystem::path& outputPath,
        std::string& outMessage) {

        if (request.metalnessSource.empty() &&
            request.roughnessSource.empty() &&
            request.glossinessSource.empty()) {
            outMessage =
                "[MaterialTexturePacking] no source textures";
            return false;
        }
        if (!request.roughnessSource.empty() &&
            !request.glossinessSource.empty()) {
            outMessage =
                "[MaterialTexturePacking] roughness and glossiness sources are mutually exclusive";
            return false;
        }

        ScalarTextureSource metalness{};
        ScalarTextureSource roughness{};
        ScalarTextureSource glossiness{};
        if (!LoadScalarTextureSource(
                request.metalnessSource,
                metalness,
                outMessage) ||
            !LoadScalarTextureSource(
                request.roughnessSource,
                roughness,
                outMessage) ||
            !LoadScalarTextureSource(
                request.glossinessSource,
                glossiness,
                outMessage)) {
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        size_t width = 1u;
        size_t height = 1u;
        const auto includeDimensions =
            [&width, &height](
                const ScalarTextureSource& source) {
                if (!source.valid) {
                    return;
                }
                const DirectX::TexMetadata metadata =
                    source.image.GetMetadata();
                width = (std::max)(width, metadata.width);
                height = (std::max)(height, metadata.height);
            };
        includeDimensions(metalness);
        includeDimensions(roughness);
        includeDimensions(glossiness);

        if (!ResizeScalarTextureSource(
                metalness,
                width,
                height,
                outMessage) ||
            !ResizeScalarTextureSource(
                roughness,
                width,
                height,
                outMessage) ||
            !ResizeScalarTextureSource(
                glossiness,
                width,
                height,
                outMessage)) {
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        DirectX::ScratchImage packed{};
        HRESULT hr = packed.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1u,
            1u);
        if (FAILED(hr)) {
            outMessage =
                "[MaterialTexturePacking] failed to allocate output image hr=" +
                ToHexHr(hr);
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        const DirectX::Image* packedImage =
            packed.GetImage(0u, 0u, 0u);
        if (packedImage == nullptr ||
            packedImage->pixels == nullptr) {
            outMessage =
                "[MaterialTexturePacking] output image is unavailable";
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        for (size_t y = 0u; y < height; ++y) {
            uint8_t* destination =
                packedImage->pixels +
                y * packedImage->rowPitch;
            for (size_t x = 0u; x < width; ++x) {
                const uint8_t metallic =
                    ReadScalarRed(
                        metalness,
                        x,
                        y,
                        255u);
                uint8_t roughnessValue =
                    ReadScalarRed(
                        roughness,
                        x,
                        y,
                        255u);
                if (glossiness.valid) {
                    roughnessValue =
                        static_cast<uint8_t>(
                            255u -
                            ReadScalarRed(
                                glossiness,
                                x,
                                y,
                                0u));
                }

                destination[x * 4u + 0u] = 255u;
                destination[x * 4u + 1u] =
                    roughnessValue;
                destination[x * 4u + 2u] = metallic;
                destination[x * 4u + 3u] = 255u;
            }
        }

        DirectX::ScratchImage mipmapped{};
        hr = DirectX::GenerateMipMaps(
            packed.GetImages(),
            packed.GetImageCount(),
            packed.GetMetadata(),
            DirectX::TEX_FILTER_DEFAULT,
            0u,
            mipmapped);
        const DirectX::ScratchImage& mipSource =
            SUCCEEDED(hr) ? mipmapped : packed;

        DirectX::ScratchImage compressed{};
        const DirectX::TEX_COMPRESS_FLAGS compressFlags =
            static_cast<DirectX::TEX_COMPRESS_FLAGS>(
                DirectX::TEX_COMPRESS_DEFAULT |
                DirectX::TEX_COMPRESS_PARALLEL |
                DirectX::TEX_COMPRESS_BC7_QUICK);
        hr = DirectX::Compress(
            mipSource.GetImages(),
            mipSource.GetImageCount(),
            mipSource.GetMetadata(),
            DXGI_FORMAT_BC7_UNORM,
            compressFlags,
            DirectX::TEX_THRESHOLD_DEFAULT,
            compressed);
        const DirectX::ScratchImage& output =
            SUCCEEDED(hr) ? compressed : mipSource;
        if (!SavePackedDds(
                output,
                outputPath,
                outMessage)) {
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        outMessage =
            "[MaterialTexturePacking] wrote canonical G=roughness/B=metallic DDS: " +
            outputPath.generic_string();
        HIKARI_LOG_INFO(outMessage);
        return true;
    }

} // namespace HIKARI
