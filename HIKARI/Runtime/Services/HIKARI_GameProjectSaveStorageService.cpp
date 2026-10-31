#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"
#include "Core/Text/HIKARI_OutMessage.h"

#include "Core/Paths/HIKARI_UserDataPath.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI::SERVICES {
namespace {

constexpr std::string_view kSaveDirectoryName = "SaveData";
constexpr std::string_view kUserDirectoryName = "HIKARI";
constexpr std::string_view kSlotExtension = ".save";

void RemoveIfPresent(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return;
    }
    std::error_code ignored{};
    std::filesystem::remove(path, ignored);
}

std::filesystem::path MakeStagedPath(const std::filesystem::path& finalPath) {
    static std::atomic_uint64_t sequence{0u};
    const uint64_t timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path stagedPath = finalPath;
    stagedPath += std::filesystem::path(".hikari-save-" + std::to_string(timestamp) + "-" +
                                        std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed)))
                      .native();
    return stagedPath;
}

std::filesystem::path MakeDefaultUserSaveRoot(const std::filesystem::path& projectRoot) {
    auto ReadEnvironment = [](const char* name) {
        char* value = nullptr;
        size_t valueLength = 0u;
        if (_dupenv_s(&value, &valueLength, name) != 0 || value == nullptr || valueLength == 0u) {
            if (value != nullptr) {
                std::free(value);
            }
            return std::string{};
        }
        std::string result(value, valueLength - 1u);
        std::free(value);
        return result;
    };
    std::string base = ReadEnvironment("LOCALAPPDATA");
    if (base.empty()) {
        base = ReadEnvironment("APPDATA");
    }
    if (base.empty()) {
        return {};
    }

    std::filesystem::path projectName = projectRoot.filename();
    if (projectName.empty() || projectName == std::filesystem::path(".") ||
        projectName == std::filesystem::path("..")) {
        projectName = "Project";
    }
    return std::filesystem::path(base) / std::filesystem::path(kUserDirectoryName) / projectName /
           std::filesystem::path(kSaveDirectoryName);
}

bool IsStrictSlotName(std::string_view slotName) noexcept {
    if (slotName.empty() || slotName.size() > GameProjectSaveStorageService::kMaxSlotNameBytes || slotName == "." ||
        slotName == "..") {
        return false;
    }

    for (const unsigned char character : slotName) {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '-' && character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

bool IsRegularFile(const std::filesystem::path& path, std::error_code& outError) noexcept {
    outError.clear();
    return std::filesystem::is_regular_file(path, outError) && !outError;
}

} // namespace

bool GameProjectSaveStorageService::Configure(const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& userSaveRoot, std::string* outMessage) {
    Reset();
    if (projectRoot.empty()) {
        SetMessage(outMessage, "save storage project root is empty");
        return false;
    }

    std::error_code ec{};
    std::filesystem::path normalizedProjectRoot = std::filesystem::absolute(projectRoot, ec);
    if (ec) {
        SetMessage(outMessage, "save storage project root could not be resolved: " + ec.message());
        return false;
    }
    normalizedProjectRoot = normalizedProjectRoot.lexically_normal();

    std::filesystem::path normalizedUserRoot = userSaveRoot;
    if (normalizedUserRoot.empty()) {
        const std::filesystem::path runtimeUserRoot = PLATFORM::GetHikariRuntimeUserRoot();
        normalizedUserRoot =
            runtimeUserRoot.empty() ? MakeDefaultUserSaveRoot(normalizedProjectRoot) : runtimeUserRoot / "Saves";
    }
    if (normalizedUserRoot.empty()) {
        SetMessage(outMessage, "save storage user root is unavailable; provide one explicitly");
        return false;
    }
    normalizedUserRoot = std::filesystem::absolute(normalizedUserRoot, ec);
    if (ec) {
        SetMessage(outMessage, "save storage user root could not be resolved: " + ec.message());
        return false;
    }

    projectSaveRoot_ = normalizedProjectRoot / std::filesystem::path(kSaveDirectoryName);
    userSaveRoot_ = normalizedUserRoot.lexically_normal();
    SetMessage(outMessage, {});
    return true;
}

void GameProjectSaveStorageService::Reset() noexcept {
    projectSaveRoot_.clear();
    userSaveRoot_.clear();
}

bool GameProjectSaveStorageService::IsConfigured() const noexcept {
    return !projectSaveRoot_.empty() && !userSaveRoot_.empty();
}

bool GameProjectSaveStorageService::Write(std::string_view slotName, std::span<const std::byte> bytes,
                                          std::string* outMessage) {
    return Write(GameProjectSaveStorageRoot::User, slotName, bytes, outMessage);
}

bool GameProjectSaveStorageService::Write(GameProjectSaveStorageRoot root, std::string_view slotName,
                                          std::span<const std::byte> bytes, std::string* outMessage) {
    if (!IsConfigured()) {
        SetMessage(outMessage, "save storage is not configured");
        return false;
    }
    if (bytes.size() > kMaxSlotBytes) {
        SetMessage(outMessage, "save slot exceeds the 1 MiB limit");
        return false;
    }

    std::filesystem::path finalPath{};
    if (!TryResolveSlotPath(root, slotName, finalPath, outMessage)) {
        return false;
    }

    std::error_code ec{};
    std::filesystem::create_directories(finalPath.parent_path(), ec);
    if (ec) {
        SetMessage(outMessage, "failed to create save directory: " + ec.message());
        return false;
    }

    const std::filesystem::path stagedPath = MakeStagedPath(finalPath);
    std::ofstream output(stagedPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        SetMessage(outMessage, "failed to open staged save file: " + stagedPath.generic_string());
        return false;
    }

    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    output.close();
    if (output.fail()) {
        RemoveIfPresent(stagedPath);
        SetMessage(outMessage, "failed while writing save slot: " + finalPath.generic_string());
        return false;
    }

    std::string commitMessage{};
    if (!IO::CommitStagedFile(stagedPath, finalPath, commitMessage)) {
        RemoveIfPresent(stagedPath);
        SetMessage(outMessage, "failed to commit save slot: " + commitMessage);
        return false;
    }

    SetMessage(outMessage, {});
    return true;
}

bool GameProjectSaveStorageService::Read(std::string_view slotName, std::span<std::byte> destination,
                                         size_t& outBytesRead, std::string* outMessage) const {
    return Read(GameProjectSaveStorageRoot::User, slotName, destination, outBytesRead, outMessage);
}

bool GameProjectSaveStorageService::Read(GameProjectSaveStorageRoot root, std::string_view slotName,
                                         std::span<std::byte> destination, size_t& outBytesRead,
                                         std::string* outMessage) const {
    outBytesRead = 0u;
    if (!IsConfigured()) {
        SetMessage(outMessage, "save storage is not configured");
        return false;
    }

    std::filesystem::path path{};
    if (!TryResolveSlotPath(root, slotName, path, outMessage)) {
        return false;
    }

    std::error_code ec{};
    if (!IsRegularFile(path, ec)) {
        SetMessage(outMessage, ec ? "failed to inspect save slot: " + ec.message() : "save slot does not exist");
        return false;
    }

    const uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize > kMaxSlotBytes || fileSize > (std::numeric_limits<size_t>::max)()) {
        SetMessage(outMessage,
                   ec ? "failed to inspect save slot size: " + ec.message() : "save slot exceeds the 1 MiB limit");
        return false;
    }

    const size_t requiredBytes = static_cast<size_t>(fileSize);
    if (requiredBytes > destination.size()) {
        outBytesRead = requiredBytes;
        SetMessage(outMessage, "save destination buffer is too small");
        return false;
    }

    std::ifstream input(path, std::ios::binary | std::ios::in);
    if (!input.is_open()) {
        SetMessage(outMessage, "failed to open save slot: " + path.generic_string());
        return false;
    }

    if (requiredBytes != 0u) {
        input.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(requiredBytes));
    }
    if (input.gcount() != static_cast<std::streamsize>(requiredBytes) || input.bad()) {
        outBytesRead = static_cast<size_t>((std::max)(std::streamsize(0), input.gcount()));
        SetMessage(outMessage, "failed while reading save slot: " + path.generic_string());
        return false;
    }

    outBytesRead = requiredBytes;
    SetMessage(outMessage, {});
    return true;
}

bool GameProjectSaveStorageService::Exists(std::string_view slotName) const {
    return Exists(GameProjectSaveStorageRoot::User, slotName);
}

bool GameProjectSaveStorageService::Exists(GameProjectSaveStorageRoot root, std::string_view slotName) const {
    if (!IsConfigured()) {
        return false;
    }
    std::filesystem::path path{};
    if (!TryResolveSlotPath(root, slotName, path, nullptr)) {
        return false;
    }
    std::error_code ec{};
    if (!IsRegularFile(path, ec)) {
        return false;
    }
    const uintmax_t fileSize = std::filesystem::file_size(path, ec);
    return !ec && fileSize <= kMaxSlotBytes;
}

size_t GameProjectSaveStorageService::Size(std::string_view slotName) const {
    return Size(GameProjectSaveStorageRoot::User, slotName);
}

size_t GameProjectSaveStorageService::Size(GameProjectSaveStorageRoot root, std::string_view slotName) const {
    if (!IsConfigured()) {
        return 0u;
    }
    std::filesystem::path path{};
    if (!TryResolveSlotPath(root, slotName, path, nullptr)) {
        return 0u;
    }
    std::error_code ec{};
    const uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize > kMaxSlotBytes || fileSize > (std::numeric_limits<size_t>::max)()) {
        return 0u;
    }
    return static_cast<size_t>(fileSize);
}

bool GameProjectSaveStorageService::Delete(std::string_view slotName, std::string* outMessage) {
    return Delete(GameProjectSaveStorageRoot::User, slotName, outMessage);
}

bool GameProjectSaveStorageService::Delete(GameProjectSaveStorageRoot root, std::string_view slotName,
                                           std::string* outMessage) {
    if (!IsConfigured()) {
        SetMessage(outMessage, "save storage is not configured");
        return false;
    }
    std::filesystem::path path{};
    if (!TryResolveSlotPath(root, slotName, path, outMessage)) {
        return false;
    }
    std::error_code ec{};
    const bool removed = std::filesystem::remove(path, ec);
    if (ec) {
        SetMessage(outMessage, "failed to delete save slot: " + ec.message());
        return false;
    }
    (void)removed;
    SetMessage(outMessage, {});
    return true;
}

bool GameProjectSaveStorageService::TryResolveSlotPath(GameProjectSaveStorageRoot root, std::string_view slotName,
                                                       std::filesystem::path& outPath, std::string* outMessage) const {
    outPath.clear();
    if (!IsStrictSlotName(slotName)) {
        SetMessage(outMessage, "save slot name contains invalid characters or is too long");
        return false;
    }
    const std::filesystem::path& saveRoot = ResolveRoot(root);
    if (saveRoot.empty()) {
        SetMessage(outMessage, "save storage root is unavailable");
        return false;
    }
    outPath = saveRoot / std::filesystem::path(std::string(slotName) + std::string(kSlotExtension));
    return true;
}

const std::filesystem::path& GameProjectSaveStorageService::ResolveRoot(
    GameProjectSaveStorageRoot root) const noexcept {
    return root == GameProjectSaveStorageRoot::Project ? projectSaveRoot_ : userSaveRoot_;
}

} // namespace HIKARI::SERVICES
