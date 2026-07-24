#include "Render3D/Upscaling/HIKARI_StreamlineDeployment.h"

#include <array>
#include <system_error>

namespace HIKARI::RENDER3D::UPSCALING {

    namespace {
        enum class DependencyGroup {
            Core,
            Dlss,
            FrameGeneration,
        };

        struct RuntimeDependency {
            const char* fileName = nullptr;
            DependencyGroup group = DependencyGroup::Core;
        };

        constexpr std::array<RuntimeDependency, 10> kRuntimeDependencies = {{
            { "sl.interposer.dll", DependencyGroup::Core },
            { "sl.common.dll", DependencyGroup::Core },
            { "sl.dlss.dll", DependencyGroup::Dlss },
            { "nvngx_dlss.dll", DependencyGroup::Dlss },
            { "nvngx_dlss.license.txt", DependencyGroup::Dlss },
            { "sl.dlss_g.dll", DependencyGroup::FrameGeneration },
            { "sl.reflex.dll", DependencyGroup::FrameGeneration },
            { "sl.pcl.dll", DependencyGroup::FrameGeneration },
            { "nvngx_dlssg.dll", DependencyGroup::FrameGeneration },
            { "reflex.license.txt", DependencyGroup::FrameGeneration },
        }};

        bool IsRequired(
            DependencyGroup group,
            const StreamlineDeploymentOptions& options) {
            switch (group) {
            case DependencyGroup::Core:
                return true;
            case DependencyGroup::Dlss:
                return options.requireDlss;
            case DependencyGroup::FrameGeneration:
                return options.requireFrameGeneration;
            default:
                return false;
            }
        }
    }

    bool DeployStreamlineRuntime(
        const std::filesystem::path& sourceDirectory,
        const std::filesystem::path& outputDirectory,
        const StreamlineDeploymentOptions& options,
        std::string& errorMessage) {
        std::error_code error{};
        std::filesystem::create_directories(outputDirectory, error);
        if (error) {
            errorMessage =
                "Could not create Streamline output directory: " +
                outputDirectory.string();
            return false;
        }

        for (const RuntimeDependency& dependency : kRuntimeDependencies) {
            const std::filesystem::path source =
                sourceDirectory / dependency.fileName;
            const bool present =
                std::filesystem::is_regular_file(source, error) && !error;
            error.clear();
            if (!present) {
                if (IsRequired(dependency.group, options)) {
                    errorMessage =
                        "Required Streamline runtime is missing: " +
                        source.string();
                    return false;
                }
                continue;
            }

            const std::filesystem::path destination =
                outputDirectory / dependency.fileName;
            std::filesystem::copy_file(
                source,
                destination,
                std::filesystem::copy_options::overwrite_existing,
                error);
            if (error) {
                errorMessage =
                    "Could not deploy Streamline runtime '" +
                    source.string() + "': " + error.message();
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
