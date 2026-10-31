#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::CORE {

enum class LogLevel {
    Info,
    Warn,
    Error,
    D3D12,
};

struct LogRecord {
    uint64_t sequence = 0;
    LogLevel level = LogLevel::Info;
    std::string timestamp;
    std::string source;
    std::string message;
    std::string resource;
};
struct LogHistoryInfo {
    uint64_t latestSequence = 0;
    uint64_t warnings = 0;
    uint64_t errors = 0;
    size_t capacity = 0;
};
// UI は差分を取得する。ファイル出力と履歴の書き込みは同一ロックで順序を保証する。
void LogMessage(LogLevel level, std::string_view source, std::string_view message, std::string_view resource = {});
std::vector<LogRecord> ReadLogHistory(uint64_t afterSequence = 0);
LogHistoryInfo GetLogHistoryInfo();
void SetLogHistoryCapacity(size_t capacity);

void InitializeLogger(const std::filesystem::path& logDirectory = {});
void ShutdownLogger();
std::filesystem::path GetLogFilePath();
void Log(LogLevel level, std::string_view message);
void LogInfo(std::string_view message);
void LogWarn(std::string_view message);
void LogError(std::string_view message);
void LogD3D12(std::string_view message);

} // namespace HIKARI::CORE

#define HIKARI_LOG_INFO(message) ::HIKARI::CORE::LogInfo(message)
#define HIKARI_LOG_WARN(message) ::HIKARI::CORE::LogWarn(message)
#define HIKARI_LOG_ERROR(message) ::HIKARI::CORE::LogError(message)
#define HIKARI_LOG_D3D12(message) ::HIKARI::CORE::LogD3D12(message)
