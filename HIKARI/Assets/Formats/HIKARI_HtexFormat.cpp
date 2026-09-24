#include "HIKARI_HtexFormat.h"

#include <fstream>
#include <limits>
#include <algorithm>
#include <bit>
#include <DirectXTex.h>

#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {

namespace {
constexpr uint32_t kHtexMagic = 0x58455448u; // 'HTEX'
constexpr uint32_t kHtexVersion = 2;

using SERIALIZATION::BINARY::STREAM::ReadTrivial;
using SERIALIZATION::BINARY::STREAM::ReadTrivialArray;
using SERIALIZATION::BINARY::STREAM::WriteTrivial;
using SERIALIZATION::BINARY::STREAM::WriteTrivialArray;

struct HtexFileHeader {
    uint32_t magic = kHtexMagic;
    uint32_t version = kHtexVersion;
    uint32_t headerSize = sizeof(HtexFileHeader);
    uint32_t flags = 0;
    uint32_t subresourceCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t arraySize = 1;
    uint32_t mipLevels = 1;
    uint32_t format = 0;
    uint32_t dimension = static_cast<uint32_t>(HtexTextureDimension::Texture2D);
    uint32_t colorSpace = static_cast<uint32_t>(TextureAssetColorSpace::Auto);
    uint32_t usage = static_cast<uint32_t>(TextureUsage::Auto);
    uint32_t compression = static_cast<uint32_t>(TextureCompression::Auto);
    uint32_t reserved = 0;
    uint64_t subresourceTableOffset = 0;
    uint64_t payloadOffset = 0;
    uint64_t payloadSize = 0;
};

struct HtexFileSubresource {
    uint32_t mipLevel = 0;
    uint32_t arraySlice = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t reserved = 0;
    uint64_t rowPitch = 0;
    uint64_t slicePitch = 0;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
};

bool IsSupportedDimension(uint32_t value) {
    return value == static_cast<uint32_t>(HtexTextureDimension::Texture2D) ||
           value == static_cast<uint32_t>(HtexTextureDimension::TextureCube);
}

bool FitsSizeT(uint64_t value) { return value <= static_cast<uint64_t>((std::numeric_limits<size_t>::max)()); }

bool FitsStreamOff(uint64_t value) {
    return value <= static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)());
}

bool AddWouldOverflow(uint64_t lhs, uint64_t rhs) { return lhs > (std::numeric_limits<uint64_t>::max)() - rhs; }

bool ValidateShape(const HtexFileHeader& header) {
    const auto format = static_cast<DXGI_FORMAT>(header.format);
    return header.width > 0 && header.width <= 16384 && header.height > 0 && header.height <= 16384 &&
           header.depth == 1 && header.arraySize > 0 && header.arraySize <= 2048 && header.mipLevels > 0 &&
           header.mipLevels <= static_cast<uint32_t>(std::bit_width((std::max)(header.width, header.height))) &&
           header.subresourceCount == header.arraySize * header.mipLevels && IsSupportedDimension(header.dimension) &&
           (header.dimension != static_cast<uint32_t>(HtexTextureDimension::TextureCube) ||
            (header.width == header.height && header.arraySize % 6 == 0)) &&
           DirectX::IsValid(format) && !DirectX::IsTypeless(format) && !DirectX::IsPlanar(format) &&
           !DirectX::IsDepthStencil(format) && DirectX::BitsPerPixel(format) > 0;
}

bool ValidateSubresources(const HtexFileHeader& header, const std::vector<HtexFileSubresource>& table) {
    uint64_t previousEnd = header.payloadOffset;
    const auto format = static_cast<DXGI_FORMAT>(header.format);
    for (size_t index = 0; index < table.size(); ++index) {
        const auto& entry = table[index];
        const auto mip = static_cast<uint32_t>(index % header.mipLevels);
        const auto width = (std::max)(1u, header.width >> mip);
        const auto height = (std::max)(1u, header.height >> mip);
        size_t rowBytes = 0, sliceBytes = 0;
        const auto rows = DirectX::ComputeScanlines(format, height);
        if (FAILED(DirectX::ComputePitch(format, width, height, rowBytes, sliceBytes)) || rows == 0 ||
            entry.mipLevel != mip || entry.arraySlice != index / header.mipLevels || entry.width != width ||
            entry.height != height || entry.depth != 1 || entry.rowPitch < rowBytes ||
            entry.rowPitch > UINT64_MAX / rows || entry.slicePitch < entry.rowPitch * rows ||
            entry.dataSize < entry.slicePitch || entry.dataOffset < previousEnd ||
            AddWouldOverflow(entry.dataOffset, entry.dataSize) ||
            entry.dataOffset + entry.dataSize > header.payloadOffset + header.payloadSize ||
            !FitsStreamOff(entry.dataOffset) || !FitsSizeT(entry.dataSize))
            return false;
        previousEnd = entry.dataOffset + entry.dataSize;
    }
    return true;
}

uint32_t ResolveMipLevel(const HtexTexture& texture, size_t index) {
    const uint32_t mipLevels = texture.mipLevels == 0 ? 1u : texture.mipLevels;
    return static_cast<uint32_t>(index % mipLevels);
}

uint32_t ResolveArraySlice(const HtexTexture& texture, size_t index) {
    const uint32_t mipLevels = texture.mipLevels == 0 ? 1u : texture.mipLevels;
    return static_cast<uint32_t>(index / mipLevels);
}

bool ReadHeaderAndTable(const std::filesystem::path& path, HtexFileHeader& outHeader,
                        std::vector<HtexFileSubresource>& outTable, std::string& outMessage) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        outMessage = "[HTEX] failed to open: " + TEXT::PathToUtf8(path);
        return false;
    }

    HtexFileHeader header{};
    if (!ReadTrivial(ifs, header)) {
        outMessage = "[HTEX] failed to read header: " + TEXT::PathToUtf8(path);
        return false;
    }

    if (header.magic != kHtexMagic || header.version != kHtexVersion || header.headerSize != sizeof(HtexFileHeader) ||
        !ValidateShape(header)) {
        outMessage = "[HTEX] invalid or unsupported v2 file: " + TEXT::PathToUtf8(path);
        return false;
    }

    const uint64_t tableBytes = static_cast<uint64_t>(header.subresourceCount) * sizeof(HtexFileSubresource);
    if (header.subresourceTableOffset < header.headerSize ||
        AddWouldOverflow(header.subresourceTableOffset, tableBytes) ||
        header.payloadOffset < header.subresourceTableOffset + tableBytes ||
        AddWouldOverflow(header.payloadOffset, header.payloadSize) || !FitsStreamOff(header.subresourceTableOffset)) {
        outMessage = "[HTEX] invalid v2 table or payload range: " + TEXT::PathToUtf8(path);
        return false;
    }

    std::error_code ec{};
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || header.payloadOffset + header.payloadSize > fileSize) {
        outMessage = "[HTEX] truncated v2 payload: " + TEXT::PathToUtf8(path);
        return false;
    }

    std::vector<HtexFileSubresource> table(header.subresourceCount);
    ifs.seekg(static_cast<std::streamoff>(header.subresourceTableOffset), std::ios::beg);
    if (!ReadTrivialArray(ifs, table.data(), table.size())) {
        outMessage = "[HTEX] failed to read v2 subresource table: " + TEXT::PathToUtf8(path);
        return false;
    }

    if (!ValidateSubresources(header, table)) {
        outMessage = "[HTEX] invalid v2 subresource layout: " + TEXT::PathToUtf8(path);
        return false;
    }

    outHeader = header;
    outTable = std::move(table);
    return true;
}

HtexFileInfo MakeFileInfo(const HtexFileHeader& header, const std::vector<HtexFileSubresource>& table) {
    HtexFileInfo info{};
    info.version = header.version;
    info.width = header.width;
    info.height = header.height;
    info.depth = header.depth;
    info.arraySize = header.arraySize;
    info.mipLevels = header.mipLevels;
    info.format = static_cast<DXGI_FORMAT>(header.format);
    info.dimension = static_cast<HtexTextureDimension>(header.dimension);
    info.colorSpace = static_cast<TextureAssetColorSpace>(header.colorSpace);
    info.usage = static_cast<TextureUsage>(header.usage);
    info.compression = static_cast<TextureCompression>(header.compression);
    info.subresourceTableOffset = header.subresourceTableOffset;
    info.payloadOffset = header.payloadOffset;
    info.payloadSize = header.payloadSize;
    info.subresources.reserve(table.size());

    for (const HtexFileSubresource& entry : table) {
        HtexSubresourceInfo subresource{};
        subresource.mipLevel = entry.mipLevel;
        subresource.arraySlice = entry.arraySlice;
        subresource.width = entry.width;
        subresource.height = entry.height;
        subresource.depth = entry.depth;
        subresource.rowPitch = entry.rowPitch;
        subresource.slicePitch = entry.slicePitch;
        subresource.dataOffset = entry.dataOffset;
        subresource.dataSize = entry.dataSize;
        info.subresources.push_back(subresource);
    }

    return info;
}
} // namespace

bool WriteHtexFile(const std::filesystem::path& path, const HtexTexture& texture, std::string& outMessage) {
    if (texture.width == 0 || texture.height == 0 || texture.format == DXGI_FORMAT_UNKNOWN ||
        texture.subresources.empty() || texture.subresources.size() > 2048u * 15u) {
        outMessage = "[HTEX] invalid texture data";
        return false;
    }

    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        outMessage = "[HTEX] failed to create output directory: " + ec.message();
        return false;
    }

    std::vector<HtexFileSubresource> table;
    table.reserve(texture.subresources.size());

    HtexFileHeader header{};
    header.subresourceCount = static_cast<uint32_t>(texture.subresources.size());
    header.width = texture.width;
    header.height = texture.height;
    header.depth = texture.depth;
    header.arraySize = texture.arraySize;
    header.mipLevels = texture.mipLevels == 0 ? 1u : texture.mipLevels;
    header.format = static_cast<uint32_t>(texture.format);
    header.dimension = static_cast<uint32_t>(texture.dimension);
    header.colorSpace = static_cast<uint32_t>(texture.colorSpace);
    header.usage = static_cast<uint32_t>(texture.usage);
    header.compression = static_cast<uint32_t>(texture.compression);
    header.subresourceTableOffset = sizeof(HtexFileHeader);
    header.payloadOffset = sizeof(HtexFileHeader) + sizeof(HtexFileSubresource) * texture.subresources.size();
    if (!ValidateShape(header)) {
        outMessage = "[HTEX] invalid texture shape or format";
        return false;
    }

    uint64_t cursor = header.payloadOffset;
    for (size_t i = 0; i < texture.subresources.size(); ++i) {
        const HtexSubresource& source = texture.subresources[i];
        HtexFileSubresource entry{};
        entry.mipLevel = source.mipLevel != 0 ? source.mipLevel : ResolveMipLevel(texture, i);
        entry.arraySlice = source.arraySlice != 0 ? source.arraySlice : ResolveArraySlice(texture, i);
        entry.width = source.width;
        entry.height = source.height;
        entry.depth = source.depth;
        entry.rowPitch = source.rowPitch;
        entry.slicePitch = source.slicePitch;
        entry.dataOffset = cursor;
        entry.dataSize = static_cast<uint64_t>(source.data.size());
        if (AddWouldOverflow(cursor, entry.dataSize)) {
            outMessage = "[HTEX] texture payload size overflow";
            return false;
        }
        cursor += entry.dataSize;
        table.push_back(entry);
    }
    header.payloadSize = cursor - header.payloadOffset;
    if (!ValidateSubresources(header, table)) {
        outMessage = "[HTEX] invalid texture subresource layout";
        return false;
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) {
        outMessage = "[HTEX] failed to open for write: " + TEXT::PathToUtf8(path);
        return false;
    }

    bool writeSucceeded = WriteTrivial(ofs, header) && WriteTrivialArray(ofs, table.data(), table.size());
    for (const HtexSubresource& source : texture.subresources) {
        writeSucceeded = writeSucceeded && WriteTrivialArray(ofs, source.data.data(), source.data.size());
    }

    if (!writeSucceeded || !ofs.good()) {
        outMessage = "[HTEX] failed while writing: " + TEXT::PathToUtf8(path);
        return false;
    }

    outMessage = "[HTEX] wrote v2 " + TEXT::PathToUtf8(path);
    return true;
}

bool InspectHtexFile(const std::filesystem::path& path, HtexFileInfo& outInfo, std::string& outMessage) {
    HtexFileHeader header{};
    std::vector<HtexFileSubresource> table{};
    if (!ReadHeaderAndTable(path, header, table, outMessage)) {
        return false;
    }

    outInfo = MakeFileInfo(header, table);
    outMessage = "[HTEX] inspected v2 " + TEXT::PathToUtf8(path);
    return true;
}

bool ReadHtexFile(const std::filesystem::path& path, HtexTexture& outTexture, std::string& outMessage,
                  uint64_t maxPayloadBytes) {
    HtexFileHeader header{};
    std::vector<HtexFileSubresource> table{};
    if (!ReadHeaderAndTable(path, header, table, outMessage)) {
        return false;
    }

    if (header.payloadSize > maxPayloadBytes) {
        outMessage = "[HTEX] texture payload exceeds the read budget";
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        outMessage = "[HTEX] failed to open: " + TEXT::PathToUtf8(path);
        return false;
    }

    HtexTexture texture{};
    texture.version = header.version;
    texture.width = header.width;
    texture.height = header.height;
    texture.depth = header.depth;
    texture.arraySize = header.arraySize;
    texture.mipLevels = header.mipLevels;
    texture.format = static_cast<DXGI_FORMAT>(header.format);
    texture.dimension = static_cast<HtexTextureDimension>(header.dimension);
    texture.colorSpace = static_cast<TextureAssetColorSpace>(header.colorSpace);
    texture.usage = static_cast<TextureUsage>(header.usage);
    texture.compression = static_cast<TextureCompression>(header.compression);
    texture.subresources.reserve(table.size());

    for (const HtexFileSubresource& entry : table) {
        if (!FitsSizeT(entry.dataSize)) {
            outMessage = "[HTEX] subresource too large for this platform: " + TEXT::PathToUtf8(path);
            return false;
        }

        HtexSubresource subresource{};
        subresource.mipLevel = entry.mipLevel;
        subresource.arraySlice = entry.arraySlice;
        subresource.width = entry.width;
        subresource.height = entry.height;
        subresource.depth = entry.depth;
        subresource.rowPitch = entry.rowPitch;
        subresource.slicePitch = entry.slicePitch;
        subresource.data.resize(static_cast<size_t>(entry.dataSize));

        ifs.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
        if (!ReadTrivialArray(ifs, subresource.data.data(), subresource.data.size())) {
            outMessage = "[HTEX] failed to read v2 subresource payload: " + TEXT::PathToUtf8(path);
            return false;
        }

        texture.subresources.push_back(std::move(subresource));
    }

    outTexture = std::move(texture);
    outMessage = "[HTEX] read v2 " + TEXT::PathToUtf8(path);
    return true;
}

} // namespace HIKARI
