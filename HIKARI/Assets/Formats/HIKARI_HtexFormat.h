#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <dxgiformat.h>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI {

    enum class HtexTextureDimension : uint32_t {
        Texture2D = 1,
        TextureCube = 2,
    };

    struct HtexSubresource {
        uint32_t mipLevel = 0;
        uint32_t arraySlice = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint64_t rowPitch = 0;
        uint64_t slicePitch = 0;
        std::vector<uint8_t> data{};
    };

    struct HtexTexture {
        uint32_t version = 2;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        HtexTextureDimension dimension = HtexTextureDimension::Texture2D;
        TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
        TextureUsage usage = TextureUsage::Auto;
        TextureCompression compression = TextureCompression::Auto;
        std::vector<HtexSubresource> subresources{};
    };

    struct HtexSubresourceInfo {
        uint32_t mipLevel = 0;
        uint32_t arraySlice = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint64_t rowPitch = 0;
        uint64_t slicePitch = 0;
        uint64_t dataOffset = 0;
        uint64_t dataSize = 0;
    };

    struct HtexFileInfo {
        uint32_t version = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        HtexTextureDimension dimension = HtexTextureDimension::Texture2D;
        TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
        TextureUsage usage = TextureUsage::Auto;
        TextureCompression compression = TextureCompression::Auto;
        uint64_t subresourceTableOffset = 0;
        uint64_t payloadOffset = 0;
        uint64_t payloadSize = 0;
        std::vector<HtexSubresourceInfo> subresources{};
    };

    bool WriteHtexFile(
        const std::filesystem::path& path,
        const HtexTexture& texture,
        std::string& outMessage);

    bool InspectHtexFile(
        const std::filesystem::path& path,
        HtexFileInfo& outInfo,
        std::string& outMessage);

    bool ReadHtexFile(
        const std::filesystem::path& path,
        HtexTexture& outTexture,
        std::string& outMessage);

} // namespace HIKARI
