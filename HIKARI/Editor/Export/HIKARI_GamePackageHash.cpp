#include "Editor/Export/HIKARI_GamePackageHash.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace HIKARI::EDITOR {
namespace {

struct HashContext final {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object{};

    ~HashContext() {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0u);
        }
    }
};

void SetMessage(std::string* output, std::string message) {
    if (output != nullptr) {
        *output = std::move(message);
    }
}

std::string StatusMessage(std::string_view operation, NTSTATUS status) {
    std::ostringstream stream{};
    stream << operation << " failed with NTSTATUS 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(status);
    return stream.str();
}

bool BeginHash(HashContext& context, std::string* outMessage) {
    NTSTATUS status = BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
    if (!BCRYPT_SUCCESS(status)) {
        SetMessage(outMessage, StatusMessage("BCryptOpenAlgorithmProvider", status));
        return false;
    }
    ULONG objectBytes = 0u;
    ULONG copiedBytes = 0u;
    status = BCryptGetProperty(context.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
                               sizeof(objectBytes), &copiedBytes, 0u);
    if (!BCRYPT_SUCCESS(status) || objectBytes == 0u) {
        SetMessage(outMessage, StatusMessage("BCryptGetProperty", status));
        return false;
    }
    context.object.resize(objectBytes);
    status = BCryptCreateHash(context.algorithm, &context.hash, context.object.data(),
                              static_cast<ULONG>(context.object.size()), nullptr, 0u, 0u);
    if (!BCRYPT_SUCCESS(status)) {
        SetMessage(outMessage, StatusMessage("BCryptCreateHash", status));
        return false;
    }
    return true;
}

bool AppendHash(HashContext& context, const unsigned char* bytes, size_t byteCount, std::string* outMessage) {
    while (byteCount > 0u) {
        const ULONG chunk =
            static_cast<ULONG>((std::min)(byteCount, static_cast<size_t>((std::numeric_limits<ULONG>::max)())));
        const NTSTATUS status = BCryptHashData(context.hash, const_cast<PUCHAR>(bytes), chunk, 0u);
        if (!BCRYPT_SUCCESS(status)) {
            SetMessage(outMessage, StatusMessage("BCryptHashData", status));
            return false;
        }
        bytes += chunk;
        byteCount -= chunk;
    }
    return true;
}

bool FinishHash(HashContext& context, std::string& outHex, std::string* outMessage) {
    std::array<unsigned char, 32u> digest{};
    const NTSTATUS status = BCryptFinishHash(context.hash, digest.data(), static_cast<ULONG>(digest.size()), 0u);
    if (!BCRYPT_SUCCESS(status)) {
        SetMessage(outMessage, StatusMessage("BCryptFinishHash", status));
        return false;
    }
    std::ostringstream stream{};
    stream << std::hex << std::setfill('0');
    for (const unsigned char value : digest) {
        stream << std::setw(2) << static_cast<unsigned>(value);
    }
    outHex = stream.str();
    SetMessage(outMessage, {});
    return true;
}

} // namespace

bool ComputeGamePackageSha256(std::string_view bytes, std::string& outHex, std::string* outMessage) {
    outHex.clear();
    HashContext context{};
    return BeginHash(context, outMessage) &&
           AppendHash(context, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), outMessage) &&
           FinishHash(context, outHex, outMessage);
}

bool ComputeGamePackageFileSha256(const std::filesystem::path& path, std::string& outHex, std::string* outMessage) {
    outHex.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        SetMessage(outMessage, "Could not open file for SHA-256: " + path.string());
        return false;
    }
    HashContext context{};
    if (!BeginHash(context, outMessage)) {
        return false;
    }
    std::vector<unsigned char> buffer(1024u * 1024u);
    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0 && !AppendHash(context, buffer.data(), static_cast<size_t>(count), outMessage)) {
            return false;
        }
    }
    if (!stream.eof()) {
        SetMessage(outMessage, "Could not read file for SHA-256: " + path.string());
        return false;
    }
    return FinishHash(context, outHex, outMessage);
}

} // namespace HIKARI::EDITOR
