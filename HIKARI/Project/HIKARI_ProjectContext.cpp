#include "Project/HIKARI_ProjectContext.h"

#include <system_error>
#include <utility>

#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {
    namespace {

        ProjectContext gProjectContext{};

        void SetMessage(
            std::string* outMessage,
            std::string message) {

            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

    } // namespace

    bool ProjectContext::Open(
        const std::filesystem::path& projectPath,
        std::string* outMessage) {

        const std::filesystem::path descriptorPath =
            ResolveProjectDescriptorPath(projectPath);
        std::error_code ec{};
        if (descriptorPath.empty() ||
            !std::filesystem::is_regular_file(descriptorPath, ec) ||
            ec) {
            SetMessage(
                outMessage,
                "Project.hikari was not found: " +
                    descriptorPath.generic_string());
            return false;
        }

        ProjectDescriptor descriptor{};
        std::string loadMessage{};
        if (!LoadProjectDescriptor(
                descriptorPath,
                descriptor,
                &loadMessage)) {
            SetMessage(outMessage, std::move(loadMessage));
            return false;
        }

        const std::filesystem::path root =
            PROJECT_PATHS::NormalizeProjectRoot(
                descriptorPath.parent_path());
        if (root.empty()) {
            SetMessage(outMessage, "project root could not be resolved");
            return false;
        }

        descriptor_ = std::move(descriptor);
        descriptorPath_ = descriptorPath;
        root_ = root;
        source_ = ProjectContextSource::Descriptor;
        open_ = true;
        SetMessage(outMessage, {});
        return true;
    }

    bool ProjectContext::OpenCompatibilityRoot(
        const std::filesystem::path& projectRoot,
        ProjectContextSource source,
        std::string* outMessage) {

        if (source == ProjectContextSource::Descriptor) {
            SetMessage(
                outMessage,
                "descriptor source requires ProjectContext::Open");
            return false;
        }

        const std::filesystem::path root =
            PROJECT_PATHS::NormalizeProjectRoot(projectRoot);
        std::error_code ec{};
        if (root.empty() ||
            !std::filesystem::is_directory(root, ec) ||
            ec) {
            SetMessage(
                outMessage,
                "compatibility project root is invalid: " +
                    root.generic_string());
            return false;
        }

        descriptor_ = {};
        descriptor_.name = root.filename().string();
        descriptorPath_.clear();
        root_ = root;
        source_ = source;
        open_ = true;
        SetMessage(outMessage, {});
        return true;
    }

    void ProjectContext::Close() {
        descriptor_ = {};
        descriptorPath_.clear();
        root_.clear();
        source_ =
            ProjectContextSource::WorkingDirectoryCompatibility;
        open_ = false;
    }

    bool ProjectContext::IsOpen() const noexcept {
        return open_;
    }

    bool ProjectContext::IsDescriptorBacked() const noexcept {
        return open_ &&
            source_ == ProjectContextSource::Descriptor;
    }

    ProjectContextSource ProjectContext::GetSource() const noexcept {
        return source_;
    }

    const ProjectDescriptor&
        ProjectContext::GetDescriptor() const noexcept {
        return descriptor_;
    }

    const std::filesystem::path&
        ProjectContext::GetDescriptorPath() const noexcept {
        return descriptorPath_;
    }

    const std::filesystem::path&
        ProjectContext::GetRoot() const noexcept {
        return root_;
    }

    std::filesystem::path ProjectContext::GetAssetsRoot() const {
        return Resolve("Assets");
    }

    std::filesystem::path
        ProjectContext::GetProjectSettingsRoot() const {
        return Resolve("ProjectSettings");
    }

    std::filesystem::path ProjectContext::GetLibraryRoot() const {
        return Resolve("Library");
    }

    std::filesystem::path ProjectContext::Resolve(
        const std::filesystem::path& projectRelativePath) const {

        return PROJECT_PATHS::ResolveProjectPath(
            root_,
            projectRelativePath);
    }

    ProjectContext& GetProjectContext() {
        return gProjectContext;
    }

} // namespace HIKARI
