#include "Core/Hashing/HIKARI_Sha256.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <new>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>

namespace HIKARI::HASH {
    namespace {

        constexpr char kHexDigits[] = "0123456789abcdef";

        int DecodeHex(char value) noexcept {
            if (value >= '0' && value <= '9') {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f') {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F') {
                return value - 'A' + 10;
            }
            return -1;
        }

        bool Succeeded(NTSTATUS status) noexcept {
            return status >= 0;
        }

    } // namespace

    struct Sha256Hasher::Impl {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<uint8_t> object{};
        bool ready = false;

        ~Impl() {
            if (hash != nullptr) {
                BCryptDestroyHash(hash);
            }
            if (algorithm != nullptr) {
                BCryptCloseAlgorithmProvider(algorithm, 0u);
            }
        }

        bool Initialize() noexcept {
            if (algorithm == nullptr) {
                if (!Succeeded(BCryptOpenAlgorithmProvider(
                        &algorithm,
                        BCRYPT_SHA256_ALGORITHM,
                        nullptr,
                        0u))) {
                    return false;
                }
            }

            ULONG objectBytes = 0u;
            ULONG copied = 0u;
            ULONG digestBytes = 0u;
            if (!Succeeded(BCryptGetProperty(
                    algorithm,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectBytes),
                    sizeof(objectBytes),
                    &copied,
                    0u)) ||
                copied != sizeof(objectBytes) ||
                objectBytes == 0u ||
                !Succeeded(BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&digestBytes),
                    sizeof(digestBytes),
                    &copied,
                    0u)) ||
                copied != sizeof(digestBytes) ||
                digestBytes != static_cast<ULONG>(
                    Sha256Digest{}.bytes.size())) {
                return false;
            }

            try {
                object.resize(objectBytes);
            }
            catch (...) {
                return false;
            }
            if (hash != nullptr) {
                BCryptDestroyHash(hash);
                hash = nullptr;
            }
            if (!Succeeded(BCryptCreateHash(
                    algorithm,
                    &hash,
                    object.data(),
                    static_cast<ULONG>(object.size()),
                    nullptr,
                    0u,
                    0u))) {
                return false;
            }
            ready = true;
            return true;
        }
    };

    bool Sha256Digest::IsZero() const noexcept {
        return std::all_of(
            bytes.begin(),
            bytes.end(),
            [](uint8_t value) { return value == 0u; });
    }

    std::string ToHex(const Sha256Digest& digest) {
        std::string text(digest.bytes.size() * 2u, '0');
        for (size_t index = 0u;
            index < digest.bytes.size();
            ++index) {
            text[index * 2u] = kHexDigits[digest.bytes[index] >> 4u];
            text[index * 2u + 1u] =
                kHexDigits[digest.bytes[index] & 0x0fu];
        }
        return text;
    }

    bool ParseSha256(
        std::string_view text,
        Sha256Digest& outDigest) noexcept {
        if (text.size() != outDigest.bytes.size() * 2u) {
            return false;
        }
        Sha256Digest digest{};
        for (size_t index = 0u;
            index < digest.bytes.size();
            ++index) {
            const int high = DecodeHex(text[index * 2u]);
            const int low = DecodeHex(text[index * 2u + 1u]);
            if (high < 0 || low < 0) {
                return false;
            }
            digest.bytes[index] = static_cast<uint8_t>(
                (high << 4) | low);
        }
        outDigest = digest;
        return true;
    }

    Sha256Hasher::Sha256Hasher() noexcept {
        try {
            impl_ = std::make_unique<Impl>();
        }
        catch (...) {
            return;
        }
        (void)impl_->Initialize();
    }

    Sha256Hasher::~Sha256Hasher() = default;

    bool Sha256Hasher::IsReady() const noexcept {
        return impl_ != nullptr && impl_->ready;
    }

    bool Sha256Hasher::Reset() noexcept {
        if (impl_ == nullptr) {
            return false;
        }
        impl_->ready = false;
        return impl_->Initialize();
    }

    bool Sha256Hasher::Append(
        std::span<const std::byte> bytes) noexcept {
        if (!IsReady()) {
            return false;
        }
        size_t offset = 0u;
        while (offset < bytes.size()) {
            const size_t chunkSize = (std::min)(
                bytes.size() - offset,
                static_cast<size_t>(
                    (std::numeric_limits<ULONG>::max)()));
            if (!Succeeded(BCryptHashData(
                    impl_->hash,
                    reinterpret_cast<PUCHAR>(
                        const_cast<std::byte*>(
                            bytes.data() + offset)),
                    static_cast<ULONG>(chunkSize),
                    0u))) {
                impl_->ready = false;
                return false;
            }
            offset += chunkSize;
        }
        return true;
    }

    bool Sha256Hasher::Append(std::string_view text) noexcept {
        return Append(std::as_bytes(std::span<const char>(
            text.data(),
            text.size())));
    }

    bool Sha256Hasher::Finish(
        Sha256Digest& outDigest) noexcept {
        if (!IsReady() ||
            !Succeeded(BCryptFinishHash(
                impl_->hash,
                outDigest.bytes.data(),
                static_cast<ULONG>(outDigest.bytes.size()),
                0u))) {
            return false;
        }
        BCryptDestroyHash(impl_->hash);
        impl_->hash = nullptr;
        impl_->ready = false;
        return true;
    }

    bool ComputeSha256(
        std::span<const std::byte> bytes,
        Sha256Digest& outDigest) noexcept {
        Sha256Hasher hasher{};
        return hasher.Append(bytes) && hasher.Finish(outDigest);
    }

    bool ComputeSha256(
        std::string_view text,
        Sha256Digest& outDigest) noexcept {
        return ComputeSha256(
            std::as_bytes(std::span<const char>(
                text.data(),
                text.size())),
            outDigest);
    }

    bool ComputeFileSha256(
        const std::filesystem::path& path,
        Sha256Digest& outDigest,
        std::string& outError) {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            outError = "could not open file for SHA-256: " +
                path.generic_string();
            return false;
        }

        Sha256Hasher hasher{};
        if (!hasher.IsReady()) {
            outError = "could not initialize SHA-256 provider";
            return false;
        }
        std::array<char, 64u * 1024u> buffer{};
        while (input) {
            input.read(
                buffer.data(),
                static_cast<std::streamsize>(buffer.size()));
            const std::streamsize readBytes = input.gcount();
            if (readBytes > 0 &&
                !hasher.Append(std::as_bytes(std::span(
                    buffer.data(),
                    static_cast<size_t>(readBytes))))) {
                outError = "SHA-256 update failed";
                return false;
            }
        }
        if (!input.eof()) {
            outError = "could not read file for SHA-256: " +
                path.generic_string();
            return false;
        }
        if (!hasher.Finish(outDigest)) {
            outError = "SHA-256 finalize failed";
            return false;
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::HASH
