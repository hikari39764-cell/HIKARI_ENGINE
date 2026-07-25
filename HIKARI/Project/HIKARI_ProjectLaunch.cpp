#include "Project/HIKARI_ProjectLaunch.h"

#include <Windows.h>

#include <array>
#include <system_error>
#include <utility>

#include "Project/HIKARI_ProjectContext.h"

namespace HIKARI {
    namespace {

        void SetMessage(
            std::string* outMessage,
            std::string message) {

            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        std::filesystem::path ExecutableDirectory() {
            std::array<wchar_t, 32768> buffer{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return {};
            }
            return std::filesystem::path(
                buffer.data(),
                buffer.data() + length).parent_path();
        }

    } // namespace

    bool OpenProjectForLaunch(
        const ProjectLaunchOptions& options,
        std::string* outMessage) {

        ProjectContext& context = GetProjectContext();
        context.Close();

        if (options.explicitProjectPath) {
            return context.Open(
                *options.explicitProjectPath,
                outMessage);
        }

        if (options.runtimeConfigProjectRoot) {
            const std::filesystem::path descriptorPath =
                ResolveProjectDescriptorPath(
                    *options.runtimeConfigProjectRoot);
            std::error_code ec{};
            if (std::filesystem::is_regular_file(
                    descriptorPath,
                    ec) &&
                !ec) {
                return context.Open(
                    descriptorPath,
                    outMessage);
            }
            return context.OpenCompatibilityRoot(
                *options.runtimeConfigProjectRoot,
                ProjectContextSource::
                    RuntimeConfigCompatibility,
                outMessage);
        }

        std::error_code ec{};
        const std::filesystem::path workingDirectory =
            std::filesystem::current_path(ec);
        if (!ec) {
            const std::filesystem::path descriptorPath =
                FindProjectDescriptor(workingDirectory);
            if (!descriptorPath.empty()) {
                return context.Open(
                    descriptorPath,
                    outMessage);
            }
        }

        const std::filesystem::path executableDirectory =
            ExecutableDirectory();
        if (!executableDirectory.empty()) {
            const std::filesystem::path descriptorPath =
                FindProjectDescriptor(executableDirectory);
            if (!descriptorPath.empty()) {
                return context.Open(
                    descriptorPath,
                    outMessage);
            }
        }

        if (ec || workingDirectory.empty()) {
            SetMessage(
                outMessage,
                "no project could be discovered and cwd is unavailable");
            return false;
        }
        return context.OpenCompatibilityRoot(
            workingDirectory,
            ProjectContextSource::
                WorkingDirectoryCompatibility,
            outMessage);
    }

    bool ApplyProjectWorkingDirectory(
        std::string* outMessage) {

        const ProjectContext& context = GetProjectContext();
        if (!context.IsOpen() || context.GetRoot().empty()) {
            SetMessage(outMessage, "no active project context");
            return false;
        }

        std::error_code ec{};
        std::filesystem::current_path(context.GetRoot(), ec);
        if (ec) {
            SetMessage(
                outMessage,
                "failed to set project working directory: " +
                    ec.message());
            return false;
        }

        SetMessage(outMessage, {});
        return true;
    }

} // namespace HIKARI
