#pragma once

#include <filesystem>
#include <string>

#include "Input/Assets/HIKARI_InputActionMap.h"

namespace HIKARI::INPUT {

InputActionMap CreateDefaultInputActionMap();

bool LoadInputActionMapProject(
    const std::filesystem::path& inputDirectory,
    InputActionMap& out,
    std::string* errorMessage = nullptr);

bool SaveInputActionMapProject(
    const std::filesystem::path& inputDirectory,
    const InputActionMap& map,
    std::string* errorMessage = nullptr);

} // namespace HIKARI::INPUT
