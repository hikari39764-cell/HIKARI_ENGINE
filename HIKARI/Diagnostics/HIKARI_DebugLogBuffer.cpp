#include "HIKARI_DebugLogBuffer.h"
#include "Core/HIKARI_Logger.h"

#include <algorithm>
#include <deque>
#include <mutex>

namespace HIKARI::DEBUGLOG {
namespace {
std::mutex gMutex;
std::deque<std::string> gRecentErrors;
constexpr size_t kMaxRecentErrors = 128;
} // namespace

// 出力は共通ログへ一本化し、ここではエディター表示用の直近エラーだけを保持する。
void PushRenderError(std::string message) {
    CORE::LogMessage(CORE::LogLevel::Error, "Renderer", message);
    std::lock_guard lock(gMutex);
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

} // namespace HIKARI::DEBUGLOG
