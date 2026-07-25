#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

    struct RecentProjectEntry {
        std::string name{};
        std::filesystem::path root{};
        int64_t lastOpenedUnixSeconds = 0;
    };

    class RecentProjectStore {
    public:
        bool Load(std::string* outMessage = nullptr);

        bool AddOrTouch(
            const std::filesystem::path& projectRoot,
            std::string* outMessage = nullptr);

        bool Remove(
            const std::filesystem::path& projectRoot,
            std::string* outMessage = nullptr);

        const std::vector<RecentProjectEntry>&
            GetEntries() const noexcept;

        const std::filesystem::path&
            GetStoragePath() const noexcept;

    private:
        bool Save(std::string* outMessage);

        std::filesystem::path storagePath_{};
        std::vector<RecentProjectEntry> entries_{};
    };

} // namespace HIKARI::EDITOR
