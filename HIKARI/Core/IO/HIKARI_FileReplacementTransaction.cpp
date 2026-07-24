#include "Core/IO/HIKARI_FileReplacementTransaction.h"

#include <chrono>
#include <unordered_set>
#include <vector>

namespace HIKARI::IO {
    namespace {

        struct PreparedOperation {
            FileReplacementOperation operation{};
            std::filesystem::path backupPath{};
            bool hadOriginal = false;
            bool backupCreated = false;
            bool installed = false;
        };

        std::filesystem::path MakeBackupPath(
            const std::filesystem::path& finalPath,
            size_t index) {
            const auto nonce = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            return std::filesystem::path(
                finalPath.string() + ".hikari-backup-" +
                std::to_string(nonce) + "-" +
                std::to_string(index));
        }

        void RemoveIfPresent(
            const std::filesystem::path& path) noexcept {
            if (path.empty()) {
                return;
            }
            std::error_code ignored{};
            std::filesystem::remove(path, ignored);
        }

        void RollBack(
            std::vector<PreparedOperation>& prepared) noexcept {
            for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
                if (it->installed) {
                    RemoveIfPresent(it->operation.finalPath);
                }
                if (it->backupCreated) {
                    std::error_code ignored{};
                    std::filesystem::rename(
                        it->backupPath,
                        it->operation.finalPath,
                        ignored);
                }
            }
        }

    } // namespace

    bool CommitFileReplacementTransaction(
        std::span<const FileReplacementOperation> operations,
        std::string& outMessage) {
        if (operations.empty()) {
            outMessage.clear();
            return true;
        }

        std::vector<PreparedOperation> prepared{};
        prepared.reserve(operations.size());
        std::unordered_set<std::string> destinations{};
        for (size_t index = 0u; index < operations.size(); ++index) {
            const FileReplacementOperation& operation = operations[index];
            if (operation.finalPath.empty() ||
                (!operation.deleteFinal && operation.stagedPath.empty())) {
                outMessage = "file transaction contains an invalid operation";
                return false;
            }
            const std::string destination =
                operation.finalPath.lexically_normal().generic_string();
            if (!destinations.insert(destination).second) {
                outMessage =
                    "file transaction contains a duplicate destination";
                return false;
            }
            std::error_code ec{};
            if (!operation.deleteFinal &&
                !std::filesystem::is_regular_file(
                    operation.stagedPath,
                    ec)) {
                outMessage = "staged file is missing: " +
                    operation.stagedPath.generic_string();
                return false;
            }
            PreparedOperation item{};
            item.operation = operation;
            item.backupPath = MakeBackupPath(operation.finalPath, index);
            item.hadOriginal = std::filesystem::exists(
                operation.finalPath,
                ec);
            prepared.push_back(std::move(item));
        }

        for (PreparedOperation& item : prepared) {
            if (!item.hadOriginal) {
                continue;
            }
            std::error_code ec{};
            std::filesystem::rename(
                item.operation.finalPath,
                item.backupPath,
                ec);
            if (ec) {
                RollBack(prepared);
                outMessage = "failed to preserve existing file: " +
                    ec.message();
                return false;
            }
            item.backupCreated = true;
        }

        for (PreparedOperation& item : prepared) {
            if (item.operation.deleteFinal) {
                continue;
            }
            std::error_code ec{};
            std::filesystem::rename(
                item.operation.stagedPath,
                item.operation.finalPath,
                ec);
            if (ec) {
                RollBack(prepared);
                outMessage = "failed to install staged file: " +
                    ec.message();
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

    bool CommitStagedFile(
        const std::filesystem::path& stagedPath,
        const std::filesystem::path& finalPath,
        std::string& outMessage) {

        const FileReplacementOperation operation{
            .finalPath = finalPath,
            .stagedPath = stagedPath,
        };
        return CommitFileReplacementTransaction(
            std::span<const FileReplacementOperation>(&operation, 1u),
            outMessage);
    }

} // namespace HIKARI::IO
