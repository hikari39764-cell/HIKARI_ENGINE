#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxProgram.h"

namespace HIKARI::VFX {

inline constexpr uint32_t kHvfxcFormatVersion = 28u;
inline constexpr uint32_t kMaximumHvfxcPayloadBytes = 64u * 1024u * 1024u;

bool SerializeVfxProgram(const VfxProgram& program, std::string& outPayload, std::string& outError);
bool DeserializeVfxProgram(std::string_view payload, VfxProgram& outProgram, std::string& outError);
bool WriteHvfxcFile(const std::filesystem::path& path, const VfxProgram& program, std::string& outError);
bool ReadHvfxcFile(const std::filesystem::path& path, VfxProgram& outProgram, std::string& outError);

} // namespace HIKARI::VFX
