#include "Project/HIKARI_ProjectModules.h"

#include <algorithm>
#include <map>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Paths/HIKARI_ExecutablePath.h"

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_OutMessage.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {
namespace {

std::filesystem::path ReadRelativePath(const std::string& text) {
    return text.empty() ? std::filesystem::path{} : TEXT::PathFromUtf8(text);
}

// 一つのモジュールの在り処。
struct ModuleLocation {
    // 読む清単。
    std::filesystem::path manifest{};
    ModuleSource source = ModuleSource::Engine;
    // ソースの形で項目に置かれている時、そのフォルダ。
    std::filesystem::path sourceDirectory{};
};

// 参照からモジュールの清単を決める：明示の路径、項目の Plugins、引擎に同梱のフォルダの順。
// Plugins/<id> がソースなら、項目の構築が作った包（Library/Plugins/<id>）の清単。まだ構築されていなければ
// 元の清単（名前や依存は読めるが、DLL は無い）。
ModuleLocation LocateModule(const std::vector<ProjectModuleReference>& references, const std::string& id,
                            const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot) {
    ModuleLocation location{};
    const auto reference = std::find_if(references.begin(), references.end(),
                                        [&](const ProjectModuleReference& item) { return item.id == id; });
    if (reference != references.end() && !reference->manifest.empty()) {
        location.source = ModuleSource::ManifestPath;
        location.manifest = reference->manifest.is_absolute() ? reference->manifest
                                                              : (projectRoot / reference->manifest).lexically_normal();
        return location;
    }
    const std::filesystem::path pluginManifest = MakeModuleManifestPath(projectRoot / kProjectPluginsDirectory, id);
    std::error_code ec{};
    if (std::filesystem::is_regular_file(pluginManifest.parent_path() / kModuleSourceMarker, ec)) {
        const std::filesystem::path builtManifest =
            MakeModuleManifestPath(projectRoot / kProjectBuiltPluginsDirectory, id);
        location.source = ModuleSource::ProjectSource;
        location.sourceDirectory = pluginManifest.parent_path();
        location.manifest = std::filesystem::is_regular_file(builtManifest, ec) ? builtManifest : pluginManifest;
        return location;
    }
    if (std::filesystem::is_regular_file(pluginManifest, ec)) {
        location.source = ModuleSource::ProjectPlugins;
        location.manifest = pluginManifest;
        return location;
    }
    location.manifest = MakeModuleManifestPath(builtinRoot, id);
    return location;
}

bool LoadLocatedManifest(const ModuleLocation& location, ModuleManifest& outManifest, std::string* outMessage) {
    if (!LoadModuleManifest(location.manifest, outManifest, outMessage)) {
        return false;
    }
    outManifest.sourceDirectory = location.sourceDirectory;
    return true;
}

class ModuleResolver {
public:
    ModuleResolver(const std::vector<ProjectModuleReference>& references, std::filesystem::path projectRoot,
                   std::filesystem::path builtinRoot)
        : references_(references), projectRoot_(std::move(projectRoot)), builtinRoot_(std::move(builtinRoot)) {}

    bool Resolve(std::vector<ModuleManifest>& outModules, std::string& outMessage) {
        for (const ProjectModuleReference& reference : references_) {
            if (!Visit(reference.id, outMessage)) {
                return false;
            }
        }
        outModules = std::move(ordered_);
        return true;
    }

private:
    enum class Mark { Visiting, Done };

    // 依存を先に積む深さ優先。
    bool Visit(const std::string& id, std::string& outMessage) {
        const auto mark = marks_.find(id);
        if (mark != marks_.end()) {
            if (mark->second == Mark::Visiting) {
                outMessage = "module dependency cycle at " + id;
                return false;
            }
            return true;
        }
        marks_[id] = Mark::Visiting;
        const ModuleLocation location = LocateModule(references_, id, projectRoot_, builtinRoot_);
        ModuleManifest manifest{};
        std::string readMessage{};
        if (!LoadLocatedManifest(location, manifest, &readMessage)) {
            outMessage =
                "module " + id + " could not be read (" + TEXT::PathToUtf8(location.manifest) + "): " + readMessage;
            return false;
        }
        if (manifest.id != id) {
            outMessage = "module manifest " + TEXT::PathToUtf8(location.manifest) + " declares id " + manifest.id +
                         " but the project expects " + id;
            return false;
        }
        if (!manifest.IsBuilt()) {
            outMessage = location.source == ModuleSource::ProjectSource
                             ? "module " + id + " is in source form (" + TEXT::PathToUtf8(location.sourceDirectory) +
                                   ") and has not been built yet. Opening the project from the launcher builds it."
                             : "module manifest " + TEXT::PathToUtf8(location.manifest) +
                                   " names no runtimeLibrary: build the module with hikari_add_module, which "
                                   "writes the packaged manifest";
            return false;
        }
        for (const std::string& dependency : manifest.dependencies) {
            if (!Visit(dependency, outMessage)) {
                return false;
            }
        }
        marks_[id] = Mark::Done;
        ordered_.push_back(std::move(manifest));
        return true;
    }

    const std::vector<ProjectModuleReference>& references_;
    std::filesystem::path projectRoot_{};
    std::filesystem::path builtinRoot_{};
    std::map<std::string, Mark> marks_{};
    std::vector<ModuleManifest> ordered_{};
};

} // namespace

bool LoadModuleManifest(const std::filesystem::path& manifestPath, ModuleManifest& outManifest,
                        std::string* outMessage) {
    nlohmann::json root{};
    std::string error{};
    if (!SERIALIZATION::JSON::ReadJsonFile(manifestPath, root, &error) || !root.is_object()) {
        SetMessage(outMessage, error.empty() ? "module manifest root must be an object" : error);
        return false;
    }
    if (!JSONSTRICT::RequireFormatVersion(root, kModuleManifestFormatVersion, error)) {
        SetMessage(outMessage, error);
        return false;
    }
    ModuleManifest manifest{};
    std::string runtimeLibrary{};
    std::string shippingRuntimeLibrary{};
    std::string editorLibrary{};
    if (!JSONSTRICT::ReadString(root, "id", {}, manifest.id, error, "root") ||
        !JSONSTRICT::ReadString(root, "name", {}, manifest.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "version", {}, manifest.version, error, "root") ||
        !JSONSTRICT::ReadString(root, "runtimeLibrary", {}, runtimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "shippingRuntimeLibrary", {}, shippingRuntimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "editorLibrary", {}, editorLibrary, error, "root")) {
        SetMessage(outMessage, error);
        return false;
    }
    if (manifest.id.empty() || manifest.version.empty()) {
        SetMessage(outMessage, "module manifest requires id and version");
        return false;
    }
    if (const auto dependencies = root.find("dependencies"); dependencies != root.end()) {
        if (!dependencies->is_array()) {
            SetMessage(outMessage, "root.dependencies must be an array");
            return false;
        }
        for (const nlohmann::json& dependency : *dependencies) {
            if (!dependency.is_string() || dependency.get<std::string>().empty()) {
                SetMessage(outMessage, "root.dependencies entries must be module ids");
                return false;
            }
            manifest.dependencies.push_back(dependency.get<std::string>());
        }
    }
    if (const auto directories = root.find("runtimeDirectories"); directories != root.end()) {
        if (!directories->is_array()) {
            SetMessage(outMessage, "root.runtimeDirectories must be an array");
            return false;
        }
        for (const nlohmann::json& entry : *directories) {
            std::filesystem::path directory =
                entry.is_string() ? ReadRelativePath(entry.get<std::string>()) : std::filesystem::path{};
            if (!PATHS::IsSafeRelativePath(directory)) {
                SetMessage(outMessage, "root.runtimeDirectories entries must be folders inside the module package");
                return false;
            }
            manifest.runtimeDirectories.push_back(std::move(directory));
        }
    }
    if (const auto assetTypes = root.find("assetTypes"); assetTypes != root.end()) {
        if (!assetTypes->is_array()) {
            SetMessage(outMessage, "root.assetTypes must be an array");
            return false;
        }
        for (const nlohmann::json& entry : *assetTypes) {
            ModuleManifestAssetType type{};
            if (!entry.is_object() || !JSONSTRICT::ReadString(entry, "typeId", {}, type.typeId, error, "assetTypes") ||
                !JSONSTRICT::ReadString(entry, "extension", {}, type.extension, error, "assetTypes") ||
                !JSONSTRICT::ReadString(entry, "displayName", {}, type.displayName, error, "assetTypes")) {
                SetMessage(outMessage, error.empty() ? "root.assetTypes entries must be objects" : error);
                return false;
            }
            manifest.assetTypes.push_back(std::move(type));
        }
    }
    manifest.runtimeLibrary = ReadRelativePath(runtimeLibrary);
    manifest.shippingRuntimeLibrary = ReadRelativePath(shippingRuntimeLibrary);
    manifest.editorLibrary = ReadRelativePath(editorLibrary);
    std::error_code ec{};
    manifest.directory = std::filesystem::absolute(manifestPath, ec).parent_path().lexically_normal();
    outManifest = std::move(manifest);
    SetMessage(outMessage, {});
    return true;
}

bool SaveModuleManifest(const std::filesystem::path& manifestPath, const ModuleManifest& manifest,
                        std::string* outMessage) {
    nlohmann::json root{{"formatVersion", kModuleManifestFormatVersion},
                        {"id", manifest.id},
                        {"name", manifest.name},
                        {"version", manifest.version},
                        {"runtimeLibrary", TEXT::PathToUtf8(manifest.runtimeLibrary)},
                        {"dependencies", manifest.dependencies}};
    const auto writeOptional = [&root](const char* key, const std::filesystem::path& value) {
        if (!value.empty()) {
            root[key] = TEXT::PathToUtf8(value);
        }
    };
    writeOptional("shippingRuntimeLibrary", manifest.shippingRuntimeLibrary);
    writeOptional("editorLibrary", manifest.editorLibrary);
    if (!manifest.runtimeDirectories.empty()) {
        nlohmann::json directories = nlohmann::json::array();
        for (const std::filesystem::path& directory : manifest.runtimeDirectories) {
            directories.push_back(TEXT::PathToUtf8(directory));
        }
        root["runtimeDirectories"] = std::move(directories);
    }
    if (!manifest.assetTypes.empty()) {
        nlohmann::json assetTypes = nlohmann::json::array();
        for (const ModuleManifestAssetType& type : manifest.assetTypes) {
            assetTypes.push_back(
                {{"typeId", type.typeId}, {"extension", type.extension}, {"displayName", type.displayName}});
        }
        root["assetTypes"] = std::move(assetTypes);
    }
    return SERIALIZATION::JSON::WriteJsonFile(manifestPath, root, outMessage);
}

std::filesystem::path MakeModuleManifestPath(const std::filesystem::path& root, const std::string& id) {
    return root / TEXT::PathFromUtf8(id) / TEXT::PathFromUtf8(id + kModuleManifestExtension);
}

std::filesystem::path GetEngineModulesDirectory() {
    return PLATFORM::GetExecutableDirectory() / kEngineModulesDirectory;
}

bool IsModuleLibraryCurrent(const ModuleManifest& manifest, const std::filesystem::path& library,
                            const SdkInterfaceFiles& provided, std::string* outMessage) {
    std::string reason{};
    if (IsLibraryInterfaceCurrent(library, provided, &reason)) {
        SetMessage(outMessage, {});
        return true;
    }
    // ソースの形のモジュールは項目の構築が作り直すので、ここへ来るのは構築を通さずに開いた時か、構築が失敗した時。
    SetMessage(outMessage,
               "module " + manifest.id + " does not match this engine: " + reason + ". " +
                   (manifest.sourceDirectory.empty()
                        ? "Get a package built for this engine, or put the module's source folder into the "
                          "project's Plugins folder so that it is rebuilt automatically."
                        : "Its source is in the project's Plugins folder: open the project from the launcher, "
                          "which rebuilds it (build log: Library/Build/HIKARI_ProjectBuild.log)."));
    return false;
}

bool AreModuleLibrariesCurrent(const ModuleManifest& manifest, std::string* outMessage) {
    const SdkInterfaceFiles& host = GetHostSdkInterface();
    if (!IsModuleLibraryCurrent(manifest, manifest.directory / manifest.runtimeLibrary, host, outMessage)) {
        return false;
    }
    return manifest.editorLibrary.empty() || manifest.editorLibrary == manifest.runtimeLibrary ||
           IsModuleLibraryCurrent(manifest, manifest.directory / manifest.editorLibrary, host, outMessage);
}

std::vector<ProjectModuleOption> ListProjectModuleOptions(const ProjectDescriptor& descriptor,
                                                          const std::filesystem::path& projectRoot,
                                                          const std::filesystem::path& builtinRoot) {
    std::vector<ProjectModuleOption> options{};
    const auto find = [&options](const std::string& id) {
        return std::find_if(options.begin(), options.end(),
                            [&](const ProjectModuleOption& option) { return option.manifest.id == id; });
    };
    const auto add = [&options](const ModuleLocation& location, const std::string& id, bool enabled) {
        ProjectModuleOption option{};
        option.source = location.source;
        option.enabled = enabled;
        if (!LoadLocatedManifest(location, option.manifest, &option.problem)) {
            option.manifest.directory = location.manifest.parent_path();
        } else if (option.manifest.id != id) {
            option.problem = "the manifest declares id " + option.manifest.id;
        } else if (location.source == ModuleSource::ProjectSource) {
            // 包がまだ無い・古いのは問題にしない：有効にして項目を開き直すと、構築が今の引擎に合わせて作る。
        } else if (!option.manifest.IsBuilt()) {
            option.problem = "the package has no library: build the module with hikari_add_module";
        } else {
            AreModuleLibrariesCurrent(option.manifest, &option.problem);
        }
        option.manifest.id = id;
        options.push_back(std::move(option));
    };

    for (const ProjectModuleReference& reference : descriptor.modules) {
        if (find(reference.id) == options.end()) {
            add(LocateModule(descriptor.modules, reference.id, projectRoot, builtinRoot), reference.id, true);
        }
    }
    // 参照されていないモジュール：項目の Plugins（包かソース）、引擎に同梱のフォルダの順。
    const std::vector<ProjectModuleReference> noReferences{};
    const std::filesystem::path roots[] = {projectRoot / kProjectPluginsDirectory, builtinRoot};
    for (const std::filesystem::path& root : roots) {
        std::error_code ec{};
        for (std::filesystem::directory_iterator entry(root, ec), end; !ec && entry != end; entry.increment(ec)) {
            const std::string id = TEXT::PathToUtf8(entry->path().filename());
            const std::filesystem::path manifestPath = MakeModuleManifestPath(root, id);
            if (find(id) != options.end() || !std::filesystem::is_regular_file(manifestPath, ec)) {
                continue;
            }
            add(root == builtinRoot ? ModuleLocation{manifestPath, ModuleSource::Engine, {}}
                                    : LocateModule(noReferences, id, projectRoot, builtinRoot),
                id, false);
        }
    }

    // 有効なモジュールから依存を辿り、読み込まれるもの全部について「誰が要るか」を記す。
    std::vector<std::string> pending{};
    for (const ProjectModuleOption& option : options) {
        if (option.enabled) {
            pending.push_back(option.manifest.id);
        }
    }
    std::vector<std::string> visited{};
    while (!pending.empty()) {
        const std::string id = std::move(pending.back());
        pending.pop_back();
        const auto owner = find(id);
        if (owner == options.end() || std::find(visited.begin(), visited.end(), id) != visited.end()) {
            continue;
        }
        visited.push_back(id);
        for (const std::string& dependency : owner->manifest.dependencies) {
            if (const auto required = find(dependency); required != options.end()) {
                required->requiredBy.push_back(id);
                pending.push_back(dependency);
            } else if (owner->problem.empty()) {
                owner->problem = "requires module " + dependency +
                                 ", which is in neither the project's Plugins folder nor the engine";
            }
        }
    }

    std::sort(options.begin(), options.end(), [](const ProjectModuleOption& left, const ProjectModuleOption& right) {
        return left.manifest.id < right.manifest.id;
    });
    return options;
}

bool SetProjectModuleEnabled(const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                             const std::string& id, bool enabled, std::string* outMessage) {
    const std::filesystem::path descriptorPath = projectRoot / kProjectDescriptorFilename;
    ProjectDescriptor descriptor{};
    if (!LoadProjectDescriptor(descriptorPath, descriptor, outMessage)) {
        return false;
    }
    const std::vector<ProjectModuleOption> options = ListProjectModuleOptions(descriptor, projectRoot, builtinRoot);
    const auto option = std::find_if(options.begin(), options.end(),
                                     [&](const ProjectModuleOption& item) { return item.manifest.id == id; });
    if (option == options.end()) {
        SetMessage(outMessage, "unknown module " + id + ": put its package folder into the project's " +
                                   kProjectPluginsDirectory + " folder");
        return false;
    }
    if (option->enabled == enabled) {
        SetMessage(outMessage, {});
        return true;
    }
    if (enabled) {
        if (!option->problem.empty()) {
            SetMessage(outMessage, option->problem);
            return false;
        }
        descriptor.modules.push_back({id, {}});
    } else {
        if (!option->requiredBy.empty()) {
            SetMessage(outMessage, "module " + id + " is required by " + option->requiredBy.front());
            return false;
        }
        std::erase_if(descriptor.modules, [&](const ProjectModuleReference& reference) { return reference.id == id; });
    }
    return SaveProjectDescriptor(descriptorPath, descriptor, outMessage);
}

bool ResolveProjectModules(const std::vector<ProjectModuleReference>& references,
                           const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                           std::vector<ModuleManifest>& outModules, std::string* outMessage) {
    std::string message{};
    std::vector<ModuleManifest> resolved{};
    if (!ModuleResolver(references, projectRoot, builtinRoot).Resolve(resolved, message)) {
        SetMessage(outMessage, message);
        return false;
    }
    outModules = std::move(resolved);
    SetMessage(outMessage, {});
    return true;
}

} // namespace HIKARI
