#pragma once
#include "HIKARI_AI_Types.h"
#include <string>
#include <string_view>
namespace HIKARI::EDITOR::AI_ASSISTANT {
std::string TruncateUtf8(std::string_view text,size_t maxBytes);
std::string Trim(std::string value);
std::string StripActionJsonForDisplay(std::string text);
}
