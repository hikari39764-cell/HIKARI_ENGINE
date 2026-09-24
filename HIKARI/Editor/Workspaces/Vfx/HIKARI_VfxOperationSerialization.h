#pragma once

#include <string_view>

#include <json.hpp>

namespace HIKARI::EDITOR {

class VfxWorkspaceController;

nlohmann::json WriteVfxWorkspaceSummary(const VfxWorkspaceController& controller);
nlohmann::json WriteVfxDiagnostics(const VfxWorkspaceController& controller);
bool DescribeVfxTarget(const VfxWorkspaceController& controller, std::string_view targetId,
                       nlohmann::json& outDescription);

} // namespace HIKARI::EDITOR
