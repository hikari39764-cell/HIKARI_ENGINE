#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI {

inline constexpr char kProjectDescriptorFilename[] = "Project.hikari";
inline constexpr uint32_t kProjectDescriptorFormatVersion = 1u;

// 項目に入れるモジュール。manifest を省くと引擎に同梱のモジュール。項目の外に置けるので、相対路径は .. を含んでよい。
struct ProjectModuleReference {
    std::string id{};
    std::filesystem::path manifest{};
};

struct ProjectDescriptor {
    uint32_t formatVersion = kProjectDescriptorFormatVersion;
    std::string name{};
    std::filesystem::path gameProjectManifest{"CMakeLists.txt"};
    std::vector<ProjectModuleReference> modules{};
};

std::filesystem::path ResolveProjectDescriptorPath(const std::filesystem::path& projectPath);

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& startPath);

bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath, ProjectDescriptor& outDescriptor,
                           std::string* outMessage = nullptr);

bool SaveProjectDescriptor(const std::filesystem::path& descriptorPath, const ProjectDescriptor& descriptor,
                           std::string* outMessage = nullptr);

} // namespace HIKARI
