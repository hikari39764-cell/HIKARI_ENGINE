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
    bool pinned = false;
    bool available = true;
    std::string thumbnail;
};

class RecentProjectStore {
public:
    bool Load(std::string* outMessage = nullptr);

    bool AddOrTouch(const std::filesystem::path& projectRoot, std::string* outMessage = nullptr);

    bool SetThumbnail(const std::filesystem::path& root, std::string path, std::string* message = nullptr);
    bool SetPinned(const std::filesystem::path& root, bool pinned, std::string* message = nullptr);
    bool Relocate(const std::filesystem::path& root, const std::filesystem::path& replacement,
                  std::string* message = nullptr);

    bool Remove(const std::filesystem::path& projectRoot, std::string* outMessage = nullptr);

    const std::vector<RecentProjectEntry>& GetEntries() const noexcept;

private:
    bool Save(std::string* outMessage);
    void SortAndTrim();

    std::filesystem::path storagePath_{};
    std::vector<RecentProjectEntry> entries_{};
};

} // namespace HIKARI::EDITOR
