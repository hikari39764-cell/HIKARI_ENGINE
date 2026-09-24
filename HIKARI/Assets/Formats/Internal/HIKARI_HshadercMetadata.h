#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"

namespace HIKARI::ASSETS::SHADER::INTERNAL {

struct HshadercDxilRange {
    uint64_t offset = 0u;
    uint64_t size = 0u;
};

bool SerializeHshadercMetadata(const ShaderCookedArtifact& artifact, std::span<const HshadercDxilRange> dxilRanges,
                               std::string& outJson, std::string& outError);
bool ParseHshadercMetadata(std::string_view jsonText, std::span<const uint8_t> payload,
                           ShaderCookedArtifact& outArtifact, std::string& outError);

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
