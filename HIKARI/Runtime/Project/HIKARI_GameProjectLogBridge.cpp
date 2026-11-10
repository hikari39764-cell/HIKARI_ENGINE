#include "HIKARI/Project/ProjectLog.h"

#include <string>

#include "Core/HIKARI_Logger.h"

namespace HIKARI {

void WriteProjectLog(GameProjectLogLevel level, std::string_view category, std::string_view utf8Message) noexcept {
    try {
        std::string line{};
        line.reserve(category.size() + utf8Message.size() + 3u);
        if (!category.empty()) {
            line.append("[").append(category).append("] ");
        }
        line.append(utf8Message);
        switch (level) {
        case GameProjectLogLevel::Info: HIKARI_LOG_INFO(line); break;
        case GameProjectLogLevel::Warning: HIKARI_LOG_WARN(line); break;
        case GameProjectLogLevel::Error: HIKARI_LOG_ERROR(line); break;
        }
    } catch (...) {
    }
}

} // namespace HIKARI
