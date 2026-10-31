#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI {

inline constexpr char kProjectDescriptorFilename[] = "Project.hikari";
inline constexpr uint32_t kProjectDescriptorFormatVersion = 1u;

struct ProjectDescriptor {
    uint32_t formatVersion = kProjectDescriptorFormatVersion;
    std::string name{};
    std::filesystem::path gameProjectManifest{"CMakeLists.txt"};
};

std::filesystem::path ResolveProjectDescriptorPath(const std::filesystem::path& projectPath);

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& startPath);

bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath, ProjectDescriptor& outDescriptor,
                           std::string* outMessage = nullptr);

bool SaveProjectDescriptor(const std::filesystem::path& descriptorPath, const ProjectDescriptor& descriptor,
                           std::string* outMessage = nullptr);

} // namespace HIKARI
