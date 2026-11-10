#include "Editor/Export/HIKARI_GameExportModules.h"

#include <Windows.h>

#include <system_error>
#include <utility>

#include "HIKARI/Project/EditorGameExport.h"
#include "HIKARI/Project/GameProjectEditorModuleApi.h"
#include "HIKARI/Project/GameProjectModuleApi.h"

#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/HIKARI_ProjectModules.h"

namespace HIKARI::EDITOR {
namespace {

// 区切りが / の UTF-8（項目の路径は日本語や中国語を含む）。
std::string GenericUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool CopyPackageFile(const std::filesystem::path& from, const std::filesystem::path& to, std::string& outMessage) {
    std::error_code ec{};
    std::filesystem::create_directories(to.parent_path(), ec);
    if (!ec) {
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        outMessage = "Could not copy " + TEXT::PathToUtf8(from) + " to " + TEXT::PathToUtf8(to) + ": " + ec.message();
        return false;
    }
    return true;
}

// Shipping の DLL は実行側の入口だけを持つ。編集器の入口が残っていたら、編集器の分を含む構成で作った DLL。
bool ValidateShippingLibrary(const ModuleManifest& module, const std::filesystem::path& library,
                             std::string& outMessage) {
    const std::string rebuild = " Build the module with its game-shipping preset (a Shipping-only build tree) "
                                "against this engine's Shipping SDK.";
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(library, ec)) {
        outMessage = "Module " + module.id + " has no Shipping library (" + TEXT::PathToUtf8(library) + ")." + rebuild;
        return false;
    }
    const HMODULE handle = LoadLibraryExW(library.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (handle == nullptr) {
        outMessage = "Module " + module.id + ": could not inspect " + TEXT::PathToUtf8(library) + ".";
        return false;
    }
    const bool runtimeEntry = GetProcAddress(handle, RUNTIME::kGameProjectModuleApiEntryPoint) != nullptr;
    const bool editorEntry = GetProcAddress(handle, EDITOR_PROJECT::kGameProjectEditorModuleApiEntryPoint) != nullptr;
    FreeLibrary(handle);
    if (!runtimeEntry) {
        outMessage = "Module " + module.id + ": " + TEXT::PathToUtf8(library) + " is not a HIKARI module library.";
        return false;
    }
    if (editorEntry) {
        outMessage = "Module " + module.id + ": the Shipping library contains Editor code (" +
                     TEXT::PathToUtf8(library) + ")." + rebuild;
        return false;
    }
    return true;
}

// モジュールの編集器側が申し出たファイルを作業フォルダへ写す。
class ModuleExportFiles final : public EDITOR_PROJECT::IGameExportFiles {
public:
    ModuleExportFiles(const std::filesystem::path& projectRoot, const std::filesystem::path& contentDirectory,
                      std::filesystem::path moduleDirectory, const std::vector<std::filesystem::path>& scenePaths)
        : projectRoot_(projectRoot), contentDirectory_(contentDirectory), moduleDirectory_(std::move(moduleDirectory)),
          projectRootUtf8_(TEXT::PathToUtf8(projectRoot)) {
        scenePathsUtf8_.reserve(scenePaths.size());
        for (const std::filesystem::path& scene : scenePaths) {
            scenePathsUtf8_.push_back(GenericUtf8(scene));
        }
    }

    std::string_view GetProjectRoot() const override { return projectRootUtf8_; }
    size_t GetSceneCount() const override { return scenePathsUtf8_.size(); }
    std::string_view GetScenePath(size_t index) const override {
        return index < scenePathsUtf8_.size() ? std::string_view(scenePathsUtf8_[index]) : std::string_view{};
    }

    bool AddModuleFile(std::string_view sourcePath, std::string_view relativePath) override {
        const std::filesystem::path relative = TEXT::PathFromUtf8(std::string(relativePath));
        if (!PATHS::IsSafeRelativePath(relative)) {
            return Reject("the package path must stay inside the module folder: " + std::string(relativePath));
        }
        return Copy(TEXT::PathFromUtf8(std::string(sourcePath)), moduleDirectory_ / relative);
    }

    bool AddProjectFile(std::string_view projectRelativePath) override {
        const std::filesystem::path relative = TEXT::PathFromUtf8(std::string(projectRelativePath)).lexically_normal();
        const std::string key = TEXT::ToLowerAsciiCopy(GenericUtf8(relative));
        if (!PATHS::IsSafeRelativePath(relative) || !key.starts_with("assets/")) {
            return Reject("project files must be below Assets: " + std::string(projectRelativePath));
        }
        return Copy(projectRoot_ / relative, contentDirectory_ / ASSETS::COOKING::MakeRuntimePayloadPath(relative));
    }

    void Fail(std::string_view message) override { (void)Reject(std::string(message)); }

    const std::string& Failure() const noexcept { return failure_; }
    size_t FileCount() const noexcept { return fileCount_; }

private:
    bool Copy(const std::filesystem::path& from, const std::filesystem::path& to) {
        std::string message{};
        if (!CopyPackageFile(from, to, message)) {
            return Reject(std::move(message));
        }
        ++fileCount_;
        return true;
    }

    // 最初の理由を残す。
    bool Reject(std::string message) {
        if (failure_.empty()) {
            failure_ = std::move(message);
        }
        return false;
    }

    const std::filesystem::path& projectRoot_;
    const std::filesystem::path& contentDirectory_;
    std::filesystem::path moduleDirectory_{};
    std::string projectRootUtf8_{};
    std::vector<std::string> scenePathsUtf8_{};
    std::string failure_{};
    size_t fileCount_ = 0u;
};

bool CollectModuleExportFiles(const ModuleManifest& module, ModuleExportFiles& files, std::string& outMessage) {
    GameProjectEditorModuleLibrary library{};
    std::string message{};
    if (!library.Load(module.directory / module.editorLibrary, message)) {
        outMessage = "Module " + module.id + ": " + message;
        return false;
    }
    const bool finished = library.CollectGameExportFiles(files);
    if (!finished || !files.Failure().empty()) {
        outMessage = "Module " + module.id + " could not prepare its files for the game: " +
                     (files.Failure().empty() ? std::string("its export step failed") : files.Failure());
        return false;
    }
    return true;
}

// 項目のモジュールを依存が先の順に解決し、どれも書き出せる状態か確かめる。
bool ResolveExportModules(const std::filesystem::path& projectRoot, std::vector<ModuleManifest>& outModules,
                          std::string& outMessage) {
    outModules.clear();
    ProjectDescriptor descriptor{};
    if (!LoadProjectDescriptor(projectRoot / kProjectDescriptorFilename, descriptor, &outMessage)) {
        return false;
    }
    if (descriptor.modules.empty()) {
        return true;
    }
    if (!ResolveProjectModules(descriptor.modules, projectRoot, GetEngineModulesDirectory(), outModules, &outMessage)) {
        outMessage = "The project's modules could not be resolved: " + outMessage +
                     ". Put the module package into the project's " + kProjectPluginsDirectory +
                     " folder or next to the Editor (" + TEXT::PathToUtf8(GetEngineModulesDirectory()) + ").";
        return false;
    }
    for (const ModuleManifest& module : outModules) {
        if (!IsModuleSdkCompatible(module, &outMessage)) {
            return false;
        }
        if (module.shippingRuntimeLibrary.empty()) {
            outMessage = "Module " + module.id + " does not declare a Shipping library.";
            return false;
        }
        if (!ValidateShippingLibrary(module, module.directory / module.shippingRuntimeLibrary, outMessage)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ValidateProjectModulesForExport(const std::filesystem::path& projectRoot, std::string& outMessage) {
    std::vector<ModuleManifest> modules{};
    return ResolveExportModules(projectRoot, modules, outMessage);
}

bool PackageProjectModules(const std::filesystem::path& projectRoot, const std::filesystem::path& stagingDirectory,
                           const std::vector<std::filesystem::path>& scenePaths, std::string& outMessage) {
    std::vector<ModuleManifest> modules{};
    if (!ResolveExportModules(projectRoot, modules, outMessage)) {
        return false;
    }
    if (modules.empty()) {
        return true;
    }
    const std::filesystem::path contentDirectory = stagingDirectory / "Content";
    const std::filesystem::path packagedModules = stagingDirectory / kEngineModulesDirectory;
    for (const ModuleManifest& module : modules) {
        const std::filesystem::path sourceLibrary = module.directory / module.shippingRuntimeLibrary;
        const std::filesystem::path targetDirectory = packagedModules / TEXT::PathFromUtf8(module.id);
        if (!CopyPackageFile(sourceLibrary, targetDirectory / sourceLibrary.filename(), outMessage)) {
            return false;
        }
        for (const std::filesystem::path& directory : module.runtimeDirectories) {
            std::error_code ec{};
            std::filesystem::copy(
                module.directory / directory, targetDirectory / directory,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                outMessage =
                    "Could not copy " + TEXT::PathToUtf8(directory) + " of module " + module.id + ": " + ec.message();
                return false;
            }
        }
        ModuleManifest packaged = module;
        packaged.runtimeLibrary = sourceLibrary.filename();
        packaged.shippingRuntimeLibrary.clear();
        packaged.editorLibrary.clear();
        if (!SaveModuleManifest(MakeModuleManifestPath(packagedModules, module.id), packaged, &outMessage)) {
            return false;
        }
        if (!module.editorLibrary.empty()) {
            ModuleExportFiles files{projectRoot, contentDirectory, targetDirectory, scenePaths};
            if (!CollectModuleExportFiles(module, files, outMessage)) {
                return false;
            }
        }
    }
    // 焼いた Project.hikari の参照を id だけにする。
    const std::filesystem::path descriptorPath = contentDirectory / kProjectDescriptorFilename;
    ProjectDescriptor descriptor{};
    if (!LoadProjectDescriptor(descriptorPath, descriptor, &outMessage)) {
        return false;
    }
    for (ProjectModuleReference& module : descriptor.modules) {
        module.manifest.clear();
    }
    return SaveProjectDescriptor(descriptorPath, descriptor, &outMessage);
}

} // namespace HIKARI::EDITOR
