#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Gfx/HIKARI_ShaderCookCompiler.h"

namespace HIKARI::GFX::INTERNAL {

struct ShaderCookRoot {
    ShaderCookIncludeRoot kind = ShaderCookIncludeRoot::Project;
    std::filesystem::path physicalRoot{};
    std::string logicalPrefix{};
};

struct ShaderCookFile {
    std::filesystem::path physicalPath{};
    ShaderCookDependency dependency{};
    std::vector<uint8_t> bytes{};
};

bool BuildShaderCookRoots(const ShaderCookRequest& request, std::vector<ShaderCookRoot>& outIncludeRoots,
                          ShaderCookRoot& outSourceRoot, std::string& outError);

bool ReadShaderCookFile(const ShaderCookRoot& root, const std::filesystem::path& relativePath,
                        std::string_view requiredExtension, ShaderCookFile& outFile, std::string& outError);

bool ReadShaderCookPhysicalFile(const ShaderCookRoot& root, const std::filesystem::path& candidate,
                                std::string_view requiredExtension, ShaderCookFile& outFile, std::string& outError);

bool ReadShaderCookLogicalDependency(const ShaderCookRoot& sourceRoot, const std::vector<ShaderCookRoot>& includeRoots,
                                     const ShaderCookDependency& expected, ShaderCookFile& outFile,
                                     std::string& outError);

} // namespace HIKARI::GFX::INTERNAL
