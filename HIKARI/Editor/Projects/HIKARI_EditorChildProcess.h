#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {
// Owns one hidden worker tree. Cancellation never targets a user process.
class EditorChildProcess final {
public:
    EditorChildProcess();
    ~EditorChildProcess();
    EditorChildProcess(const EditorChildProcess&) = delete;
    EditorChildProcess& operator=(const EditorChildProcess&) = delete;
    bool Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
               const std::filesystem::path& directory, const std::filesystem::path& log, std::string& message,
               bool killOnClose = true);
    bool Running() const noexcept;
    uint32_t ExitCode() const noexcept;
    void Wait(uint32_t milliseconds) const noexcept;
    void Cancel() noexcept;
    static std::filesystem::path PowerShell();

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::EDITOR
