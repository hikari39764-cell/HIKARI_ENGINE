#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "RenderContracts/Shader/HIKARI_ShaderCookedArtifact.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr uint32_t kHshadercFormatVersion = 2u;
inline constexpr size_t kMaximumHshadercMetadataBytes = 1024u * 1024u;

bool SerializeHshaderc(const ShaderCookedArtifact& artifact, std::vector<uint8_t>& outBytes, std::string& outError);
bool DeserializeHshaderc(std::span<const uint8_t> bytes, ShaderCookedArtifact& outArtifact, std::string& outError);
bool WriteHshadercFile(const std::filesystem::path& path, const ShaderCookedArtifact& artifact, std::string& outError);
bool ReadHshadercFile(const std::filesystem::path& path, ShaderCookedArtifact& outArtifact, std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
