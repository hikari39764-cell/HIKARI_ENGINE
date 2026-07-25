#include "Editor/Projects/HIKARI_ProjectCreationService.h"

#include <ShlObj.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <string_view>
#include <system_error>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI::EDITOR {
namespace {

    bool IsWindowsReservedName(std::string name) {
        std::transform(
            name.begin(),
            name.end(),
            name.begin(),
            [](unsigned char character) {
                return static_cast<char>(
                    std::toupper(character));
            });
        const size_t extension = name.find('.');
        if (extension != std::string::npos) {
            name.resize(extension);
        }
        if (name == "CON" ||
            name == "PRN" ||
            name == "AUX" ||
            name == "NUL") {
            return true;
        }
        if (name.size() == 4u &&
            (name.starts_with("COM") ||
                name.starts_with("LPT")) &&
            name[3] >= '1' &&
            name[3] <= '9') {
            return true;
        }
        return false;
    }

    bool ValidateProjectName(
        const std::string& name,
        std::string& outMessage) {

        if (name.empty()) {
            outMessage = "Project name must not be empty.";
            return false;
        }
        if (name == "." || name == "..") {
            outMessage = "Project name is invalid.";
            return false;
        }
        if (name.back() == ' ' || name.back() == '.') {
            outMessage =
                "Project name must not end with a space or period.";
            return false;
        }
        constexpr std::string_view invalidCharacters{
            "<>:\"/\\|?*"
        };
        if (name.find_first_of(invalidCharacters) !=
            std::string::npos) {
            outMessage =
                "Project name contains a Windows path character.";
            return false;
        }
        if (IsWindowsReservedName(name)) {
            outMessage =
                "Project name is reserved by Windows.";
            return false;
        }
        return true;
    }

    std::string MakeCppIdentifier(
        std::string_view name) {

        std::string identifier{};
        identifier.reserve(name.size() + 8u);
        bool previousUnderscore = false;
        for (const unsigned char character : name) {
            const bool alphaNumeric =
                std::isalnum(character) != 0;
            if (alphaNumeric) {
                identifier.push_back(
                    static_cast<char>(character));
                previousUnderscore = false;
            }
            else if (!previousUnderscore &&
                !identifier.empty()) {
                identifier.push_back('_');
                previousUnderscore = true;
            }
        }
        while (!identifier.empty() &&
            identifier.back() == '_') {
            identifier.pop_back();
        }
        if (identifier.empty()) {
            identifier = "Game";
        }
        if (std::isdigit(
                static_cast<unsigned char>(
                    identifier.front())) != 0) {
            identifier.insert(0, "Game_");
        }
        return identifier;
    }

    bool WriteTextFile(
        const std::filesystem::path& path,
        const std::string& contents,
        std::string& outMessage) {

        std::ofstream output(
            path,
            std::ios::out |
                std::ios::trunc |
                std::ios::binary);
        if (!output.is_open()) {
            outMessage =
                "Could not create " +
                TEXT::PathToUtf8(path);
            return false;
        }
        output.write(
            contents.data(),
            static_cast<std::streamsize>(
                contents.size()));
        output.close();
        if (output.fail()) {
            outMessage =
                "Could not write " +
                TEXT::PathToUtf8(path);
            return false;
        }
        return true;
    }

    std::string MakeGameProjectCMake(
        const std::string& identifier) {

        return
            "hikari_declare_game_project(\n"
            "    NAME \"" + identifier + "\"\n"
            "    SOURCES\n"
            "        Source/" + identifier +
            "GameProjectModule.cpp)\n";
    }

    std::string MakeGameProjectModule(
        const std::string& identifier) {

        return
            "#include \"Runtime/Project/"
            "HIKARI_IGameProjectModule.h\"\n\n"
            "#include <memory>\n"
            "#include <string_view>\n"
            "#include <vector>\n\n"
            "namespace " + identifier + " {\n"
            "namespace {\n\n"
            "    class GameProjectModule final\n"
            "        : public HIKARI::RUNTIME::"
            "IGameProjectModule {\n"
            "    public:\n"
            "        std::string_view GetModuleId() "
            "const noexcept override {\n"
            "            return \"" + identifier +
            "\";\n"
            "        }\n\n"
            "        std::vector<std::unique_ptr<"
            "HIKARI::IRuntimeExtension>>\n"
            "            CreateRuntimeExtensions() "
            "const override {\n"
            "            return {};\n"
            "        }\n"
            "    };\n\n"
            "} // namespace\n"
            "} // namespace " + identifier + "\n\n"
            "namespace HIKARI::RUNTIME {\n\n"
            "    std::unique_ptr<IGameProjectModule> "
            "CreateGameProjectModule() {\n"
            "        return std::make_unique<"
            "::" + identifier +
            "::GameProjectModule>();\n"
            "    }\n\n"
            "} // namespace HIKARI::RUNTIME\n";
    }

    std::string MakeProjectGitAttributes() {
        return
            "* text=auto\n\n"
            "Assets/**/*.bin filter=lfs diff=lfs merge=lfs -text\n"
            "Assets/**/*.dds filter=lfs diff=lfs merge=lfs -text\n"
            "Assets/**/*.png filter=lfs diff=lfs merge=lfs -text\n";
    }

    void RemoveStagingDirectory(
        const std::filesystem::path& path) noexcept {

        std::error_code ignored{};
        std::filesystem::remove_all(path, ignored);
    }

} // namespace

    std::filesystem::path
        GetDefaultProjectParentDirectory() {

        PWSTR documentsPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_Documents,
            KF_FLAG_DEFAULT,
            nullptr,
            &documentsPath);
        if (FAILED(result) || documentsPath == nullptr) {
            return {};
        }
        const std::filesystem::path projectRoot =
            std::filesystem::path(documentsPath) /
            "HIKARI Projects";
        CoTaskMemFree(documentsPath);
        return projectRoot;
    }

    bool CreateProject(
        const ProjectCreationRequest& request,
        ProjectCreationResult& outResult,
        std::string& outMessage) {

        outResult = {};
        if (!ValidateProjectName(request.name, outMessage)) {
            return false;
        }
        if (request.parentDirectory.empty()) {
            outMessage =
                "Project parent directory must not be empty.";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(
            request.parentDirectory,
            ec);
        if (ec) {
            outMessage =
                "Could not create the project parent directory: " +
                ec.message();
            return false;
        }

        const std::filesystem::path projectRoot =
            request.parentDirectory /
            TEXT::PathFromUtf8(request.name);
        if (std::filesystem::exists(projectRoot, ec)) {
            outMessage =
                "Project directory already exists: " +
                TEXT::PathToUtf8(projectRoot);
            return false;
        }

        const auto nonce =
            std::chrono::steady_clock::now()
                .time_since_epoch().count();
        std::filesystem::path stagingRoot =
            request.parentDirectory /
            (".hikari-create-" +
                std::to_string(nonce));
        std::filesystem::create_directories(
            stagingRoot / "Assets",
            ec);
        std::filesystem::create_directories(
            stagingRoot / "ProjectSettings",
            ec);
        std::filesystem::create_directories(
            stagingRoot / "Source",
            ec);
        if (ec) {
            RemoveStagingDirectory(stagingRoot);
            outMessage =
                "Could not create the project template: " +
                ec.message();
            return false;
        }

        const std::string identifier =
            MakeCppIdentifier(request.name);
        ProjectDescriptor descriptor{};
        descriptor.name = request.name;
        descriptor.gameProjectManifest =
            "CMakeLists.txt";
        if (!SaveProjectDescriptor(
                stagingRoot /
                    kProjectDescriptorFilename,
                descriptor,
                &outMessage) ||
            !SERIALIZATION::JSON::WriteJsonFile(
                stagingRoot /
                    "ProjectSettings" /
                    "project_settings.json",
                nlohmann::json{
                    {
                        "enabledRuntimeFeatures",
                        nlohmann::json::array({
                            "Rendering",
                            "Animation",
                            "Camera",
                            "Physics",
                            "GameplayBasic",
                            "Cinematics"
                        })
                    },
                    { "startupSceneGuid", "" },
                    { "version", 3 }
                },
                &outMessage) ||
            !WriteTextFile(
                stagingRoot / "CMakeLists.txt",
                MakeGameProjectCMake(identifier),
                outMessage) ||
            !WriteTextFile(
                stagingRoot /
                    "Source" /
                    (identifier +
                        "GameProjectModule.cpp"),
                MakeGameProjectModule(identifier),
                outMessage) ||
            !WriteTextFile(
                stagingRoot / ".gitignore",
                "/Library/\n/logs/\n/RendererLog/\n"
                "/out/\n/.vs/\n/imgui.ini\n*.user\n",
                outMessage) ||
            !WriteTextFile(
                stagingRoot / ".gitattributes",
                MakeProjectGitAttributes(),
                outMessage)) {
            RemoveStagingDirectory(stagingRoot);
            return false;
        }

        std::filesystem::rename(
            stagingRoot,
            projectRoot,
            ec);
        if (ec) {
            RemoveStagingDirectory(stagingRoot);
            outMessage =
                "Could not publish the project directory: " +
                ec.message();
            return false;
        }

        outResult.projectRoot = projectRoot;
        outResult.descriptorPath =
            projectRoot /
            kProjectDescriptorFilename;
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::EDITOR
