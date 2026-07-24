#pragma once

#include <vector>

#include <json.hpp>

#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringTypes.h"

namespace HIKARI::EDITOR {

    bool DrawSystemSettingsFields(
        const std::vector<SystemSettingField>& fields,
        nlohmann::json& settings,
        bool advanced);

} // namespace HIKARI::EDITOR
