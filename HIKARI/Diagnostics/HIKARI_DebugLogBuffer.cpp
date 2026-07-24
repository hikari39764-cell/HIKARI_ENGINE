#include "HIKARI_DebugLogBuffer.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace HIKARI::DEBUGLOG {
    namespace {
        std::mutex gMutex;
        std::deque<std::string> gRecentErrors;
        constexpr size_t kMaxRecentErrors = 128;

        std::filesystem::path GetLogPath() {
            std::filesystem::path dir = std::filesystem::current_path() / "RendererLog";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return dir / "render_diagnostics.log";
        }

        std::string MakeTimestampPrefix() {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto time = system_clock::to_time_t(now);
            tm localTime{};
            localtime_s(&localTime, &time);

            char buffer[64]{};
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
            return std::string("[") + buffer + "] ";
        }
    }

    void WriteRenderLogLine(const std::string& message) {
        const std::string line = MakeTimestampPrefix() + message;
        OutputDebugStringA((line + "\n").c_str());

        std::ofstream out(GetLogPath(), std::ios::app);
        if (out.is_open()) {
            out << line << '\n';
        }
    }

    void PushRenderError(std::string message) {
        std::lock_guard lock(gMutex);
        WriteRenderLogLine(message);
        gRecentErrors.push_back(std::move(message));
        while (gRecentErrors.size() > kMaxRecentErrors) {
            gRecentErrors.pop_front();
        }
    }

    std::vector<std::string> GetRecentRenderErrors(size_t maxCount) {
        std::lock_guard lock(gMutex);
        maxCount = (std::min)(maxCount, gRecentErrors.size());
        std::vector<std::string> result;
        result.reserve(maxCount);
        const size_t start = gRecentErrors.size() - maxCount;
        for (size_t i = start; i < gRecentErrors.size(); ++i) {
            result.push_back(gRecentErrors[i]);
        }
        return result;
    }

    void ClearRenderErrors() {
        std::lock_guard lock(gMutex);
        gRecentErrors.clear();
        WriteRenderLogLine("[Diagnostics] Recent render errors cleared.");
    }
}
