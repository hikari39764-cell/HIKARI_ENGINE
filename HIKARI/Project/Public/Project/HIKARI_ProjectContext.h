#pragma once

#include <filesystem>
#include <string>

#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI {

enum class ProjectContextSource {
    Descriptor,
    RuntimeConfigCompatibility,
    WorkingDirectoryCompatibility,
};

class ProjectContext {
public:
    bool Open(const std::filesystem::path& projectPath, std::string* outMessage = nullptr);

    bool OpenCompatibilityRoot(const std::filesystem::path& projectRoot, ProjectContextSource source,
                               std::string* outMessage = nullptr);

    void Close();

    bool IsOpen() const noexcept;
    bool IsDescriptorBacked() const noexcept;
    ProjectContextSource GetSource() const noexcept;

    const ProjectDescriptor& GetDescriptor() const noexcept;
    const std::filesystem::path& GetDescriptorPath() const noexcept;
    const std::filesystem::path& GetRoot() const noexcept;

    std::filesystem::path GetAssetsRoot() const;
    std::filesystem::path GetLibraryRoot() const;
    std::filesystem::path Resolve(const std::filesystem::path& projectRelativePath) const;

private:
    ProjectDescriptor descriptor_{};
    std::filesystem::path descriptorPath_{};
    std::filesystem::path root_{};
    ProjectContextSource source_ = ProjectContextSource::WorkingDirectoryCompatibility;
    bool open_ = false;
};

ProjectContext& GetProjectContext();

} // namespace HIKARI
