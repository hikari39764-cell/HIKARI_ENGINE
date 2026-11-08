#include "HIKARI_TerrainDataCodec.h"

#include <limits>

#include <miniz.h>

namespace HIKARI::TERRAIN::DATA::CODEC {

uint32_t Crc32(std::span<const uint8_t> bytes) noexcept {
    return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, bytes.data(), bytes.size()));
}

bool Deflate(std::span<const uint8_t> raw, std::vector<uint8_t>& out) {
    if (raw.size() > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    out.resize(static_cast<std::size_t>(size));
    if (mz_compress2(out.data(), &size, raw.data(), static_cast<mz_ulong>(raw.size()), MZ_BEST_SPEED) != MZ_OK) {
        out.clear();
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    return true;
}

bool Inflate(std::span<const uint8_t> stored, std::size_t rawSize, std::vector<uint8_t>& out) {
    if (rawSize > (std::numeric_limits<uint32_t>::max)() || stored.size() > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    out.resize(rawSize);
    mz_ulong size = static_cast<mz_ulong>(rawSize);
    if (mz_uncompress(out.data(), &size, stored.data(), static_cast<mz_ulong>(stored.size())) != MZ_OK ||
        size != rawSize) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace HIKARI::TERRAIN::DATA::CODEC
