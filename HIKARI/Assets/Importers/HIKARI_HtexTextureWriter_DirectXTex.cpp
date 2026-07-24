#include "HIKARI_HtexTextureWriter_DirectXTex.h"

#include <cstring>

#include <DirectXTex.h>

#include "Assets/Formats/HIKARI_HtexFormat.h"
#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    namespace {
        HtexTextureDimension ToHtexDimension(const DirectX::TexMetadata& metadata) {
            return metadata.IsCubemap()
                ? HtexTextureDimension::TextureCube
                : HtexTextureDimension::Texture2D;
        }
    }

    bool WriteHtexFromDdsWithDirectXTex(
        const std::filesystem::path& ddsPath,
        const std::filesystem::path& htexPath,
        const TextureImportSettings& settings,
        std::string& outMessage) {

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image{};
        const HRESULT hr = DirectX::LoadFromDDSFile(
            ddsPath.wstring().c_str(),
            DirectX::DDS_FLAGS_NONE,
            &metadata,
            image);
        if (FAILED(hr) || image.GetImageCount() == 0) {
            outMessage = "[HTEX] failed to read DDS for HTEX conversion: " + ddsPath.generic_string();
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        HtexTexture texture{};
        texture.width = static_cast<uint32_t>(metadata.width);
        texture.height = static_cast<uint32_t>(metadata.height);
        texture.depth = static_cast<uint32_t>(metadata.depth);
        texture.arraySize = static_cast<uint32_t>(metadata.arraySize);
        texture.mipLevels = static_cast<uint32_t>(metadata.mipLevels);
        texture.format = metadata.format;
        texture.dimension = ToHtexDimension(metadata);
        texture.colorSpace = settings.colorSpace;
        texture.usage = settings.usage;
        texture.compression = settings.compression;

        texture.subresources.reserve(image.GetImageCount());
        const DirectX::Image* images = image.GetImages();
        const uint32_t mipLevels = texture.mipLevels == 0 ? 1u : texture.mipLevels;
        for (size_t i = 0; i < image.GetImageCount(); ++i) {
            const DirectX::Image* source = images + i;

            HtexSubresource subresource{};
            subresource.mipLevel = static_cast<uint32_t>(i % mipLevels);
            subresource.arraySlice = static_cast<uint32_t>(i / mipLevels);
            subresource.width = static_cast<uint32_t>(source->width);
            subresource.height = static_cast<uint32_t>(source->height);
            subresource.depth = 1;
            subresource.rowPitch = static_cast<uint64_t>(source->rowPitch);
            subresource.slicePitch = static_cast<uint64_t>(source->slicePitch);
            subresource.data.resize(source->slicePitch);
            if (source->pixels && source->slicePitch > 0) {
                std::memcpy(subresource.data.data(), source->pixels, source->slicePitch);
            }
            texture.subresources.push_back(std::move(subresource));
        }

        if (!WriteHtexFile(htexPath, texture, outMessage)) {
            HIKARI_LOG_ERROR(outMessage);
            return false;
        }

        HIKARI_LOG_INFO(outMessage);
        return true;
    }

} // namespace HIKARI
