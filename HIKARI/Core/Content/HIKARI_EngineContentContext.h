#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::CONTENT {

class EngineContentContext {
public:
    bool Open(const std::filesystem::path& root, std::string* outMessage = nullptr);
    void Close() noexcept;

    bool IsOpen() const noexcept;
    const std::filesystem::path& GetRoot() const noexcept;

    // root からの相対パスだけを受け付ける。
    std::filesystem::path Resolve(const std::filesystem::path& relativePath) const;

private:
    std::filesystem::path root_{};
    bool open_ = false;
};

EngineContentContext& GetEngineContentContext();

bool OpenEngineContentForLaunch(std::string* outMessage = nullptr);

} // namespace HIKARI::CONTENT
