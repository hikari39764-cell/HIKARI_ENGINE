#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// セルファイルと撤销記録が共有する、deflate・CRC32・little endian の読み書き（miniz を使う）。
namespace HIKARI::TERRAIN::DATA::CODEC {

uint32_t Crc32(std::span<const uint8_t> bytes) noexcept;
bool Deflate(std::span<const uint8_t> raw, std::vector<uint8_t>& out);
// 展開した大きさが rawSize と違えば失敗。
bool Inflate(std::span<const uint8_t> stored, std::size_t rawSize, std::vector<uint8_t>& out);

template <class T>
std::span<const uint8_t> AsBytes(const std::vector<T>& values) noexcept {
    return {reinterpret_cast<const uint8_t*>(values.data()), values.size() * sizeof(T)};
}

// 生バイトを要素の配列へ（大きさが合わなければ失敗）。
template <class T>
bool FromBytes(std::span<const uint8_t> bytes, std::size_t count, std::vector<T>& out) {
    if (bytes.size() != count * sizeof(T)) {
        return false;
    }
    out.resize(count);
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return true;
}

class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& out) : out_(out) {}
    void U8(uint8_t value) { out_.push_back(value); }
    void U16(uint16_t value) { Little(value); }
    void U32(uint32_t value) { Little(value); }
    void U64(uint64_t value) { Little(value); }
    void I32(int32_t value) { Little(static_cast<uint32_t>(value)); }
    void F32(float value) { Little(std::bit_cast<uint32_t>(value)); }
    void Bytes(std::span<const uint8_t> bytes) { out_.insert(out_.end(), bytes.begin(), bytes.end()); }
    std::size_t Size() const noexcept { return out_.size(); }
    // 既に書いた位置へ u32 を上書きする。
    void PatchU32(std::size_t offset, uint32_t value) {
        for (std::size_t i = 0u; i < 4u; ++i) {
            out_[offset + i] = static_cast<uint8_t>(value >> (8u * i));
        }
    }

private:
    template <class T>
    void Little(T value) {
        for (std::size_t i = 0u; i < sizeof(T); ++i) {
            out_.push_back(static_cast<uint8_t>(value >> (8u * i)));
        }
    }
    std::vector<uint8_t>& out_;
};

// 読み過ぎると ok() が false になり、以後は 0 を返す。
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes, std::size_t offset = 0u) : bytes_(bytes), offset_(offset) {}
    uint8_t U8() { return Little<uint8_t>(); }
    uint16_t U16() { return Little<uint16_t>(); }
    uint32_t U32() { return Little<uint32_t>(); }
    uint64_t U64() { return Little<uint64_t>(); }
    int32_t I32() { return static_cast<int32_t>(Little<uint32_t>()); }
    float F32() { return std::bit_cast<float>(Little<uint32_t>()); }
    bool ok() const noexcept { return ok_; }
    std::size_t Offset() const noexcept { return offset_; }

private:
    template <class T>
    T Little() {
        if (!ok_ || offset_ > bytes_.size() || bytes_.size() - offset_ < sizeof(T)) {
            ok_ = false;
            return T{};
        }
        T value{};
        for (std::size_t i = 0u; i < sizeof(T); ++i) {
            value = static_cast<T>(value | (static_cast<T>(bytes_[offset_ + i]) << (8u * i)));
        }
        offset_ += sizeof(T);
        return value;
    }
    std::span<const uint8_t> bytes_;
    std::size_t offset_ = 0u;
    bool ok_ = true;
};

} // namespace HIKARI::TERRAIN::DATA::CODEC
