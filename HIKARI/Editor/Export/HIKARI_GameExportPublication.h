#pragma once
#include <filesystem>
#include <string>

namespace HIKARI::EDITOR {
class GameExportPublication final {
public:
    ~GameExportPublication();
    GameExportPublication() = default;
    GameExportPublication(const GameExportPublication&) = delete;
    GameExportPublication& operator=(const GameExportPublication&) = delete;
    bool Begin(const std::filesystem::path& destination, const std::filesystem::path& projectRoot,
               std::string& message);
    bool Publish(std::string& message);
    const std::filesystem::path& StagingDirectory() const { return staging_; }

private:
    std::filesystem::path destination_, staging_, previous_, lock_;
    bool ownsLock_ = false;
};
} // namespace HIKARI::EDITOR
