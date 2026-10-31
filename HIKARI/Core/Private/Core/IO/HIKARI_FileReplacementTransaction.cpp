#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/IO/HIKARI_NativeFilePath.h"

#include <chrono>
#include <unordered_set>
#include <vector>
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::IO {
namespace {

struct PreparedOperation {
    FileReplacementOperation operation{};
    std::filesystem::path backupPath{};
    bool hadOriginal = false;
    bool backupCreated = false;
    bool installed = false;
};

std::filesystem::path MakeBackupPath(const std::filesystem::path& finalPath, size_t index) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto backup = finalPath;
    backup += ".hikari-backup-" + std::to_string(nonce) + "-" + std::to_string(index);
    return backup;
}

void RemoveIfPresent(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return;
    }
    std::error_code ignored{};
    std::filesystem::remove(NativeFilePath(path), ignored);
}

void RollBack(std::vector<PreparedOperation>& prepared) noexcept {
    for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
        if (it->installed) {
            RemoveIfPresent(it->operation.finalPath);
        }
        if (it->backupCreated) {
            std::error_code ignored{};
            std::filesystem::rename(NativeFilePath(it->backupPath), NativeFilePath(it->operation.finalPath), ignored);
        }
    }
}

} // namespace

bool CommitFileReplacementTransaction(std::span<const FileReplacementOperation> operations, std::string& outMessage) {
    if (operations.empty()) {
        outMessage.clear();
        return true;
    }

    std::vector<PreparedOperation> prepared{};
    prepared.reserve(operations.size());
    std::unordered_set<std::string> destinations{};
    for (size_t index = 0u; index < operations.size(); ++index) {
        const FileReplacementOperation& operation = operations[index];
        if (operation.finalPath.empty() || (!operation.deleteFinal && operation.stagedPath.empty())) {
            outMessage = "file transaction contains an invalid operation";
            return false;
        }
        const std::string destination = TEXT::PathToUtf8(operation.finalPath.lexically_normal());
        if (!destinations.insert(destination).second) {
            outMessage = "file transaction contains a duplicate destination";
            return false;
        }
        std::error_code ec{};
        if (!operation.deleteFinal && !std::filesystem::is_regular_file(NativeFilePath(operation.stagedPath), ec)) {
            outMessage = "staged file is missing: " + TEXT::PathToUtf8(operation.stagedPath);
            return false;
        }
        PreparedOperation item{};
        item.operation = operation;
        item.backupPath = MakeBackupPath(operation.finalPath, index);
        item.hadOriginal = std::filesystem::exists(NativeFilePath(operation.finalPath), ec);
        prepared.push_back(std::move(item));
    }

    for (PreparedOperation& item : prepared) {
        if (!item.hadOriginal) {
            continue;
        }
        std::error_code ec{};
        std::filesystem::rename(NativeFilePath(item.operation.finalPath), NativeFilePath(item.backupPath), ec);
        if (ec) {
            RollBack(prepared);
            outMessage = "failed to preserve existing file: " + ec.message();
            return false;
        }
        item.backupCreated = true;
    }

    for (PreparedOperation& item : prepared) {
        if (item.operation.deleteFinal) {
            continue;
        }
        std::error_code ec{};
        std::filesystem::rename(NativeFilePath(item.operation.stagedPath), NativeFilePath(item.operation.finalPath),
                                ec);
        if (ec) {
            RollBack(prepared);
            outMessage = "failed to install staged file: " + ec.message();
            return false;
        }
        item.installed = true;
    }

    for (PreparedOperation& item : prepared) {
        RemoveIfPresent(item.backupPath);
    }
    outMessage.clear();
    return true;
}

bool CommitStagedFile(const std::filesystem::path& stagedPath, const std::filesystem::path& finalPath,
                      std::string& outMessage) {
    const FileReplacementOperation operation{
        .finalPath = finalPath,
        .stagedPath = stagedPath,
    };
    return CommitFileReplacementTransaction(std::span<const FileReplacementOperation>(&operation, 1u), outMessage);
}

} // namespace HIKARI::IO
