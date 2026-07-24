#include "HIKARI_Logger.h"

#include <Windows.h>

#include <chrono>
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

        std::filesystem::path MakeLogFilePathOnce() {
            const std::filesystem::path logDir = std::filesystem::current_path() / "logs";
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
    }

    void InitializeLogger() {
        std::lock_guard lock(gLogMutex);
        if (gInitialized) {
            return;
        }

        const std::filesystem::path path = MakeLogFilePathOnce();
        gLogFile.open(path, std::ios::out | std::ios::app);
        gInitialized = true;
    }

    void ShutdownLogger() {
        std::lock_guard lock(gLogMutex);
        if (gLogFile.is_open()) {
            gLogFile.flush();
            gLogFile.close();
        }
        gInitialized = false;
    }

    void Log(LogLevel level, std::string_view message) {
        std::lock_guard lock(gLogMutex);

        std::ostringstream oss;
        oss << "[" << FormatTimestampForLine() << "] "
            << "[" << ToText(level) << "] "
            << message;
        OutputLine(oss.str());
    }

    void LogInfo(std::string_view message) {
        Log(LogLevel::Info, message);
    }

    void LogWarn(std::string_view message) {
        Log(LogLevel::Warn, message);
    }

    void LogError(std::string_view message) {
        Log(LogLevel::Error, message);
    }

    void LogD3D12(std::string_view message) {
        Log(LogLevel::D3D12, message);
    }

} // namespace HIKARI::CORE
