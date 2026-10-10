#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Agent/HIKARI_AgentProjectFiles.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Editor/Projects/HIKARI_ProjectSourceTemplate.h"

#include <ShlObj.h>

#include <chrono>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI::EDITOR {
namespace {

bool ValidateProjectName(const std::string& name, std::string& outMessage) {
    if (name.empty()) {
        outMessage = TEXT::Localize("launcher.nameEmpty");
        return false;
    }
    if (name == "." || name == "..") {
        outMessage = TEXT::Localize("launcher.nameInvalid");
        return false;
    }
    if (name.back() == ' ' || name.back() == '.') {
        outMessage = TEXT::Localize("launcher.nameEnding");
        return false;
    }
    constexpr std::string_view invalidCharacters{"<>:\"/\\|?*"};
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }) ||
        name.find_first_of(invalidCharacters) != std::string::npos) {
        outMessage = TEXT::Localize("launcher.nameCharacters");
        return false;
    }
    if (TEXT::IsWindowsReservedFileName(name)) {
        outMessage = TEXT::Localize("launcher.nameReserved");
        return false;
    }
    return true;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& contents, std::string& outMessage) {
    std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output.is_open()) {
        outMessage = "Could not create " + TEXT::PathToUtf8(path);
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (output.fail()) {
        outMessage = "Could not write " + TEXT::PathToUtf8(path);
        return false;
    }
    return true;
}

void RemoveStagingDirectory(const std::filesystem::path& path) noexcept {
    std::error_code ignored{};
    std::filesystem::remove_all(path, ignored);
}

} // namespace

std::filesystem::path GetDefaultProjectParentDirectory() {
    PWSTR documentsPath = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsPath);
    if (FAILED(result) || documentsPath == nullptr) {
        return {};
    }
    const std::filesystem::path projectRoot = std::filesystem::path(documentsPath) / "HIKARI Projects";
    CoTaskMemFree(documentsPath);
    return projectRoot;
}

bool ValidateProjectCreation(const ProjectCreationRequest& request, std::filesystem::path& target,
                             std::string& message) {
    target.clear();
    if (!ValidateProjectName(request.name, message))
        return false;
    if (request.parentDirectory.empty() || !request.parentDirectory.is_absolute()) {
        message = TEXT::Localize("launcher.absoluteParent");
        return false;
    }
    target = (request.parentDirectory / TEXT::PathFromUtf8(request.name)).lexically_normal();
    std::error_code ec;
    if (std::filesystem::exists(target, ec) || ec) {
        message = TEXT::Localize("launcher.targetExists");
        return false;
    }
    message.clear();
    return true;
}

bool CreateProject(const ProjectCreationRequest& request, ProjectCreationResult& outResult, std::string& outMessage) {
    outResult = {};
    std::filesystem::path validatedTarget;
    if (!ValidateProjectCreation(request, validatedTarget, outMessage))
        return false;

    std::error_code ec{};
    std::filesystem::create_directories(request.parentDirectory, ec);
    if (ec) {
        outMessage = "Could not create the project parent directory: " + ec.message();
        return false;
    }

    const std::filesystem::path projectRoot = request.parentDirectory / TEXT::PathFromUtf8(request.name);
    if (std::filesystem::exists(projectRoot, ec)) {
        outMessage = "Project directory already exists: " + TEXT::PathToUtf8(projectRoot);
        return false;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path stagingRoot = request.parentDirectory / (".hikari-create-" + std::to_string(nonce));
    std::filesystem::create_directories(stagingRoot / "Assets", ec);
    std::filesystem::create_directories(stagingRoot / "ProjectSettings", ec);
    std::filesystem::create_directories(stagingRoot / "Source" / "Runtime", ec);
    if (ec) {
        RemoveStagingDirectory(stagingRoot);
        outMessage = "Could not create the project template: " + ec.message();
        return false;
    }

    const std::string identifier = PROJECT_TEMPLATE::MakeProjectCppIdentifier(request.name);
    ProjectDescriptor descriptor{};
    descriptor.name = request.name;
    descriptor.gameProjectManifest = "CMakeLists.txt";
    if (!SaveProjectDescriptor(stagingRoot / kProjectDescriptorFilename, descriptor, &outMessage) ||
        !SERIALIZATION::JSON::WriteJsonFile(
            stagingRoot / "ProjectSettings" / "project_settings.json",
            nlohmann::json{
                {"enabledRuntimeFeatures", nlohmann::json::array({"Rendering", "Lighting", "Camera", "UI",
                                                                  "HIKARI.SceneFlow", identifier + ".Gameplay"})},
                {"startupSceneGuid", ""},
                {"version", 5}},
            &outMessage) ||
        !WriteTextFile(stagingRoot / "CMakeLists.txt", PROJECT_TEMPLATE::MakeGameProjectCMake(identifier),
                       outMessage) ||
        !WriteTextFile(stagingRoot / "CMakePresets.json", PROJECT_TEMPLATE::MakeGameProjectCMakePresets(),
                       outMessage) ||
        !WriteTextFile(stagingRoot / "Source" / "Runtime" / (identifier + "GameProjectModule.cpp"),
                       PROJECT_TEMPLATE::MakeGameProjectModule(identifier), outMessage) ||
        !WriteTextFile(stagingRoot / ".gitignore", "/Library/\n/logs/\n/out/\n/.vs/\n/imgui.ini\n*.user\n",
                       outMessage) ||
        !WriteTextFile(stagingRoot / ".gitattributes", PROJECT_TEMPLATE::MakeProjectGitAttributes(), outMessage)) {
        RemoveStagingDirectory(stagingRoot);
        return false;
    }
    std::vector<std::filesystem::path> agentFiles;
    if (!AGENT::WriteAgentProjectFiles(stagingRoot, AGENT::DetectAgentProjectFileInputs(request.name), agentFiles,
                                       outMessage)) {
        RemoveStagingDirectory(stagingRoot);
        return false;
    }

    std::filesystem::rename(stagingRoot, projectRoot, ec);
    if (ec) {
        RemoveStagingDirectory(stagingRoot);
        outMessage = "Could not publish the project directory: " + ec.message();
        return false;
    }

    outResult.projectRoot = projectRoot;
    outResult.descriptorPath = projectRoot / kProjectDescriptorFilename;
    outMessage.clear();
    return true;
}

} // namespace HIKARI::EDITOR
