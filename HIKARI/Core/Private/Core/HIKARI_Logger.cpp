#include "Core/HIKARI_Logger.h"
#include "Core/Paths/HIKARI_UserDataPath.h"

#include <Windows.h>

#include <chrono>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace HIKARI::CORE {
namespace {
std::mutex gLogMutex;
std::ofstream gLogFile;
bool gInitialized = false;
std::filesystem::path gLogPath;
std::deque<LogRecord> gHistory;
LogHistoryInfo gHistoryInfo{0, 0, 0, 4000};

std::tm ToLocalTime(std::time_t value) {
    std::tm tm{};
    localtime_s(&tm, &value);
    return tm;
}

std::string FormatTimestampForLine() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = ToLocalTime(nowTime);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::string FormatTimestampForFileName() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = ToLocalTime(nowTime);

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
    return buffer;
}

const char* ToText(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::D3D12: return "D3D12";
    default: return "INFO";
    }
}

std::filesystem::path MakeLogFilePathOnce(const std::filesystem::path& requestedDirectory) {
    // 既定の出力先は作業ディレクトリではなくユーザーデータ領域。製品フォルダやリポジトリを汚さない。
    std::filesystem::path logDir = requestedDirectory;
    if (logDir.empty()) {
        const std::filesystem::path localAppData = PLATFORM::GetLocalAppDataDirectory();
        std::error_code tempError;
        logDir =
            (localAppData.empty() ? std::filesystem::temp_directory_path(tempError) : localAppData) / "HIKARI" / "Logs";
    }
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    return logDir / ("HIKARI_" + FormatTimestampForFileName() + ".log");
}

void OutputLine(const std::string& line) {
    OutputDebugStringA(line.c_str());
    OutputDebugStringA("\n");
    if (gLogFile.is_open()) {
        gLogFile << line << '\n';
        gLogFile.flush();
    }
}
} // namespace

void InitializeLogger(const std::filesystem::path& logDirectory) {
    std::lock_guard lock(gLogMutex);
    if (gInitialized) {
        return;
    }

    const std::filesystem::path path = MakeLogFilePathOnce(logDirectory);
    gLogPath = path;
    gLogFile.open(path, std::ios::out | std::ios::app);
    gInitialized = true;
}

std::filesystem::path GetLogFilePath() {
    std::lock_guard lock(gLogMutex);
    return gLogPath;
}

void ShutdownLogger() {
    std::lock_guard lock(gLogMutex);
    if (gLogFile.is_open()) {
        gLogFile.flush();
        gLogFile.close();
    }
    gInitialized = false;
}

void LogMessage(LogLevel level, std::string_view source, std::string_view message, std::string_view resource) {
    std::lock_guard lock(gLogMutex);
    const auto timestamp = FormatTimestampForLine();
    gHistory.push_back({++gHistoryInfo.latestSequence, level, timestamp, std::string(source), std::string(message),
                        std::string(resource)});
    if (level == LogLevel::Warn)
        ++gHistoryInfo.warnings;
    if (level == LogLevel::Error)
        ++gHistoryInfo.errors;
    while (gHistory.size() > gHistoryInfo.capacity)
        gHistory.pop_front();
    std::ostringstream oss;
    oss << "[" << timestamp << "] "
        << "[" << ToText(level) << "] " << message;
    OutputLine(oss.str());
}

void Log(LogLevel level, std::string_view message) {
    std::string_view source = level == LogLevel::D3D12 ? "D3D12" : "Engine";
    if (message.starts_with('[')) {
        const auto close = message.find(']');
        if (close != std::string_view::npos && close > 1 && close < 64)
            source = message.substr(1, close - 1);
    }
    LogMessage(level, source, message);
}

std::vector<LogRecord> ReadLogHistory(uint64_t afterSequence) {
    std::lock_guard lock(gLogMutex);
    std::vector<LogRecord> records;
    if (afterSequence >= gHistoryInfo.latestSequence)
        return records;
    for (const auto& record : gHistory)
        if (record.sequence > afterSequence)
            records.push_back(record);
    return records;
}
LogHistoryInfo GetLogHistoryInfo() {
    std::lock_guard lock(gLogMutex);
    return gHistoryInfo;
}
void SetLogHistoryCapacity(size_t capacity) {
    std::lock_guard lock(gLogMutex);
    gHistoryInfo.capacity = std::clamp<size_t>(capacity, 200, 20000);
    while (gHistory.size() > gHistoryInfo.capacity)
        gHistory.pop_front();
}

void LogInfo(std::string_view message) { Log(LogLevel::Info, message); }

void LogWarn(std::string_view message) { Log(LogLevel::Warn, message); }

void LogError(std::string_view message) { Log(LogLevel::Error, message); }

void LogD3D12(std::string_view message) { Log(LogLevel::D3D12, message); }

} // namespace HIKARI::CORE
