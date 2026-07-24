#pragma once

#include <string_view>

namespace HIKARI::CORE {

    enum class LogLevel {
        Info,
        Warn,
        Error,
        D3D12,
    };

    void InitializeLogger();
    void ShutdownLogger();
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
