#pragma once
#include <string>
#include <string_view>

namespace HIKARI::EDITOR::PROJECT_TEMPLATE {
std::string MakeProjectCppIdentifier(std::string_view name);
std::string MakeGameProjectCMake(const std::string& identifier);
std::string MakeGameProjectCMakePresets();
std::string MakeGameProjectModule(const std::string& identifier);
std::string MakeProjectGitAttributes();
} // namespace HIKARI::EDITOR::PROJECT_TEMPLATE
