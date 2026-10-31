#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace HIKARI::HASH {

struct Sha256Digest {
    std::array<uint8_t, 32u> bytes{};

    bool IsZero() const noexcept;

    friend bool operator==(const Sha256Digest& lhs, const Sha256Digest& rhs) noexcept { return lhs.bytes == rhs.bytes; }
};

std::string ToHex(const Sha256Digest& digest);
bool ParseSha256(std::string_view text, Sha256Digest& outDigest) noexcept;

class Sha256Hasher final {
public:
    Sha256Hasher() noexcept;
    ~Sha256Hasher();

    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;

    bool IsReady() const noexcept;
    bool Reset() noexcept;
    bool Append(std::span<const std::byte> bytes) noexcept;
    bool Append(std::string_view text) noexcept;
    bool Finish(Sha256Digest& outDigest) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

bool ComputeSha256(std::span<const std::byte> bytes, Sha256Digest& outDigest) noexcept;
bool ComputeSha256(std::string_view text, Sha256Digest& outDigest) noexcept;
bool ComputeFileSha256(const std::filesystem::path& path, Sha256Digest& outDigest, std::string& outError);

} // namespace HIKARI::HASH
