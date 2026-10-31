#pragma once

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "Core/HIKARI_Logger.h"

namespace HIKARI::CORE {

// 起動経路専用。フレーム毎のログや共有の可変状態を持たない。
class StartupTimer final {
public:
    using Clock = std::chrono::steady_clock;
    explicit StartupTimer(std::string_view stage, bool enabled = true) noexcept
        : started_(enabled ? Clock::now() : Clock::time_point{}), stage_(stage), finished_(!enabled) {}
    StartupTimer(const StartupTimer&) = delete;
    StartupTimer& operator=(const StartupTimer&) = delete;

    ~StartupTimer() { Finish(); }

    void Finish() {
        if (finished_)
            return;
        finished_ = true;
        ReportSince(stage_, started_);
    }

    static void ReportSince(std::string_view stage, Clock::time_point started) {
        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        std::ostringstream message;
        message << "[Startup] " << stage << " elapsed_ms=" << std::fixed << std::setprecision(3) << milliseconds;
        LogInfo(message.str());
    }

private:
    const Clock::time_point started_;
    std::string_view stage_;
    bool finished_ = false;
};

} // namespace HIKARI::CORE
