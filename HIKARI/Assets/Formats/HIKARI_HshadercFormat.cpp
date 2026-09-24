#include "Assets/Formats/HIKARI_HshadercFormat.h"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>

#include "Assets/Formats/Internal/HIKARI_HshadercMetadata.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI::ASSETS::SHADER {
namespace {

constexpr uint32_t MakeFourCc(char byte0, char byte1, char byte2, char byte3) noexcept {
    return static_cast<uint32_t>(byte0) | (static_cast<uint32_t>(byte1) << 8u) | (static_cast<uint32_t>(byte2) << 16u) |
           (static_cast<uint32_t>(byte3) << 24u);
}

// On-disk bytes are the stable FourCC "HHSC" (HIKARI HShader Cooked).
inline constexpr uint32_t kHshadercMagic = MakeFourCc('H', 'H', 'S', 'C');
inline constexpr uint32_t kHshadercFlags = 0u;

struct HshadercHeader {
    uint32_t magic = kHshadercMagic;
    uint32_t formatVersion = kHshadercFormatVersion;
    uint32_t headerSize = sizeof(HshadercHeader);
    uint32_t flags = kHshadercFlags;
    uint64_t metadataBytes = 0u;
    uint64_t payloadBytes = 0u;
    uint64_t fileBytes = 0u;
    HASH::Sha256Digest sourceClosureHash{};
    HASH::Sha256Digest engineInterfaceHash{};
    HASH::Sha256Digest rootLayoutHash{};
    HASH::Sha256Digest artifactContentHash{};
};

static_assert(kHshadercMagic == 0x43534848u);
static_assert(std::endian::native == std::endian::little);
static_assert(sizeof(HshadercHeader) == 168u);
static_assert(std::is_standard_layout_v<HshadercHeader>);
static_assert(std::is_trivially_copyable_v<HshadercHeader>);
static_assert(offsetof(HshadercHeader, magic) == 0u);
static_assert(offsetof(HshadercHeader, formatVersion) == 4u);
static_assert(offsetof(HshadercHeader, headerSize) == 8u);
static_assert(offsetof(HshadercHeader, flags) == 12u);
static_assert(offsetof(HshadercHeader, metadataBytes) == 16u);
static_assert(offsetof(HshadercHeader, payloadBytes) == 24u);
static_assert(offsetof(HshadercHeader, fileBytes) == 32u);
static_assert(offsetof(HshadercHeader, sourceClosureHash) == 40u);
static_assert(offsetof(HshadercHeader, engineInterfaceHash) == 72u);
static_assert(offsetof(HshadercHeader, rootLayoutHash) == 104u);
static_assert(offsetof(HshadercHeader, artifactContentHash) == 136u);

size_t Align16(size_t value) noexcept { return (value + 15u) & ~size_t{15u}; }

bool ComputeContentHash(std::string_view metadata, std::span<const uint8_t> payload,
                        HASH::Sha256Digest& outHash) noexcept {
    HASH::Sha256Hasher hasher{};
    return hasher.Append(metadata) && hasher.Append(std::as_bytes(payload)) && hasher.Finish(outHash);
}

std::filesystem::path MakeStagedPath(const std::filesystem::path& finalPath) {
    static std::atomic_uint64_t sequence{0u};
    std::filesystem::path staged = finalPath;
    staged += ".hikari-hshaderc-" + std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed));
    return staged;
}

} // namespace

bool SerializeHshaderc(const ShaderCookedArtifact& artifact, std::vector<uint8_t>& outBytes, std::string& outError) {
    if (!ValidateShaderCookedArtifact(artifact, outError)) {
        return false;
    }
    try {
        std::vector<uint8_t> payload{};
        std::vector<INTERNAL::HshadercDxilRange> ranges{};
        ranges.reserve(artifact.entries.size());
        for (const ShaderCookedEntry& entry : artifact.entries) {
            const size_t alignedOffset = Align16(payload.size());
            if (alignedOffset > kMaximumShaderArtifactBytes ||
                entry.dxil.size() > kMaximumShaderArtifactBytes - alignedOffset) {
                outError = "shader DXIL payload exceeds artifact limit";
                return false;
            }
            payload.resize(alignedOffset, 0u);
            ranges.push_back({static_cast<uint64_t>(alignedOffset), static_cast<uint64_t>(entry.dxil.size())});
            payload.insert(payload.end(), entry.dxil.begin(), entry.dxil.end());
        }

        std::string metadata{};
        if (!INTERNAL::SerializeHshadercMetadata(artifact, ranges, metadata, outError)) {
            return false;
        }
        HASH::Sha256Digest contentHash{};
        if (!ComputeContentHash(metadata, payload, contentHash)) {
            outError = "could not hash shader artifact content";
            return false;
        }
        const uint64_t fileBytes =
            sizeof(HshadercHeader) + static_cast<uint64_t>(metadata.size()) + static_cast<uint64_t>(payload.size());
        if (fileBytes > sizeof(HshadercHeader) + kMaximumHshadercMetadataBytes + kMaximumShaderArtifactBytes ||
            fileBytes > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
            outError = "shader artifact file size is invalid";
            return false;
        }
        const HshadercHeader header{kHshadercMagic,
                                    kHshadercFormatVersion,
                                    sizeof(HshadercHeader),
                                    kHshadercFlags,
                                    static_cast<uint64_t>(metadata.size()),
                                    static_cast<uint64_t>(payload.size()),
                                    fileBytes,
                                    artifact.sourceClosureHash,
                                    artifact.engineInterfaceHash,
                                    artifact.rootLayoutHash,
                                    contentHash};
        std::vector<uint8_t> bytes(static_cast<size_t>(fileBytes));
        size_t offset = 0u;
        std::memcpy(bytes.data(), &header, sizeof(header));
        offset += sizeof(header);
        std::memcpy(bytes.data() + offset, metadata.data(), metadata.size());
        offset += metadata.size();
        if (!payload.empty()) {
            std::memcpy(bytes.data() + offset, payload.data(), payload.size());
        }

        ShaderCookedArtifact verified{};
        if (!DeserializeHshaderc(bytes, verified, outError)) {
            return false;
        }
        outBytes = std::move(bytes);
        outError.clear();
        return true;
    } catch (const std::exception& error) {
        outError = "shader artifact serialization failed: " + std::string(error.what());
        return false;
    }
}

bool DeserializeHshaderc(std::span<const uint8_t> bytes, ShaderCookedArtifact& outArtifact, std::string& outError) {
    const size_t maximumFileBytes =
        sizeof(HshadercHeader) + kMaximumHshadercMetadataBytes + kMaximumShaderArtifactBytes;
    if (bytes.size() < sizeof(HshadercHeader) || bytes.size() > maximumFileBytes) {
        outError = "shader artifact file size is outside the limit";
        return false;
    }
    HshadercHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kHshadercMagic || header.formatVersion != kHshadercFormatVersion ||
        header.headerSize != sizeof(HshadercHeader) || header.flags != kHshadercFlags ||
        header.fileBytes != bytes.size() || header.metadataBytes == 0u ||
        header.metadataBytes > kMaximumHshadercMetadataBytes || header.payloadBytes == 0u ||
        header.payloadBytes > kMaximumShaderArtifactBytes ||
        header.metadataBytes > header.fileBytes - sizeof(HshadercHeader) ||
        header.payloadBytes != header.fileBytes - sizeof(HshadercHeader) - header.metadataBytes ||
        header.sourceClosureHash.IsZero() || header.engineInterfaceHash.IsZero() || header.rootLayoutHash.IsZero() ||
        header.artifactContentHash.IsZero()) {
        outError = "shader artifact header is invalid";
        return false;
    }

    const size_t metadataOffset = sizeof(HshadercHeader);
    const size_t metadataBytes = static_cast<size_t>(header.metadataBytes);
    const size_t payloadOffset = metadataOffset + metadataBytes;
    const std::string_view metadata(reinterpret_cast<const char*>(bytes.data() + metadataOffset), metadataBytes);
    const std::span<const uint8_t> payload(bytes.data() + payloadOffset, static_cast<size_t>(header.payloadBytes));
    HASH::Sha256Digest contentHash{};
    if (!ComputeContentHash(metadata, payload, contentHash) || contentHash != header.artifactContentHash) {
        outError = "shader artifact content hash mismatch";
        return false;
    }

    ShaderCookedArtifact artifact{};
    if (!INTERNAL::ParseHshadercMetadata(metadata, payload, artifact, outError) ||
        artifact.sourceClosureHash != header.sourceClosureHash ||
        artifact.engineInterfaceHash != header.engineInterfaceHash ||
        artifact.rootLayoutHash != header.rootLayoutHash) {
        if (outError.empty()) {
            outError = "shader artifact header/metadata hash mismatch";
        }
        return false;
    }
    outArtifact = std::move(artifact);
    outError.clear();
    return true;
}

bool WriteHshadercFile(const std::filesystem::path& path, const ShaderCookedArtifact& artifact, std::string& outError) {
    std::vector<uint8_t> bytes{};
    if (path.empty() || !SerializeHshaderc(artifact, bytes, outError)) {
        return false;
    }
    std::error_code ec{};
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (ec) {
        outError = "could not create shader artifact directory: " + ec.message();
        return false;
    }

    const std::filesystem::path staged = MakeStagedPath(path);
    std::ofstream output(staged, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        outError = "could not open staged shader artifact";
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (output.fail()) {
        std::filesystem::remove(staged, ec);
        outError = "could not write staged shader artifact";
        return false;
    }

    ShaderCookedArtifact verified{};
    if (!ReadHshadercFile(staged, verified, outError)) {
        std::filesystem::remove(staged, ec);
        return false;
    }
    std::string commitMessage{};
    if (!IO::CommitStagedFile(staged, path, commitMessage)) {
        std::filesystem::remove(staged, ec);
        outError = "could not commit shader artifact: " + commitMessage;
        return false;
    }
    outError.clear();
    return true;
}

bool ReadHshadercFile(const std::filesystem::path& path, ShaderCookedArtifact& outArtifact, std::string& outError) {
    std::error_code ec{};
    const uintmax_t fileBytes = std::filesystem::file_size(path, ec);
    const size_t maximumFileBytes =
        sizeof(HshadercHeader) + kMaximumHshadercMetadataBytes + kMaximumShaderArtifactBytes;
    if (ec || fileBytes < sizeof(HshadercHeader) || fileBytes > maximumFileBytes) {
        outError = "shader artifact file size is invalid";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        outError = "could not open shader artifact";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(fileBytes));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        outError = "shader artifact changed while reading";
        return false;
    }
    return DeserializeHshaderc(bytes, outArtifact, outError);
}

} // namespace HIKARI::ASSETS::SHADER
