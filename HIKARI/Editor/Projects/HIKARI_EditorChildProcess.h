#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR {
struct EditorChildProcessOptions {
    bool killOnClose = true;
    // 標準入力を管道にして WriteInput で書く（false なら NUL）。
    bool writableInput = false;
    // 標準エラーの書き先。空なら標準出力と同じ log。
    std::filesystem::path errorLog{};
};

// Owns one hidden worker tree. Cancellation never targets a user process.
class EditorChildProcess final {
public:
    EditorChildProcess();
    ~EditorChildProcess();
    EditorChildProcess(const EditorChildProcess&) = delete;
    EditorChildProcess& operator=(const EditorChildProcess&) = delete;
    bool Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
               const std::filesystem::path& directory, const std::filesystem::path& log, std::string& message,
               const EditorChildProcessOptions& options = {});
    // writableInput で起動した時だけ書ける。書けなければ false。
    bool WriteInput(std::string_view bytes) noexcept;
    void CloseInput() noexcept;
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
