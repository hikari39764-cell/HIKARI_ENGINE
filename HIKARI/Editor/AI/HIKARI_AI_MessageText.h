#pragma once
#include "HIKARI_AI_Types.h"
#include <string>
#include <string_view>
namespace HIKARI::EDITOR::AI_ASSISTANT {
bool HasToolMarkup(std::string_view text);
std::string TruncateUtf8(std::string_view text,size_t maxBytes);
std::string FormatCapabilityParameters(const AiCapability& capability);
std::string Trim(std::string value);
std::string ExtractAction(std::string_view json);
std::string ExtractPlanLine(std::string_view text);
std::string StripActionJsonForDisplay(std::string text);
}
