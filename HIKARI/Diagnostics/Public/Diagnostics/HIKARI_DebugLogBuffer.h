#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace HIKARI::DEBUGLOG {

void PushRenderError(std::string message);
std::vector<std::string> GetRecentRenderErrors(size_t maxCount);

} // namespace HIKARI::DEBUGLOG
